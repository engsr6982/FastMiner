#pragma once
#include "Global.h"
#include "core/BFS.h"
#include "core/MinerUtil.h"
#include "core/TaskControl.h"
#include "core/TaskDispatcher.h"
#include "core/TaskPermitAwaiter.h"

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include <chrono>
#include <concepts>
#include <type_traits>
#include <utility>
#include <vector>

namespace fm {

class TaskDispatcher;

/// 客户端预搜索产出的已确定方块集合，挖掘任务接管后免扩散直接消费。
using PreSearchData = std::vector<BlockBFS::Pending>;

/**
 * @brief 派生任务公共上下文。
 */
struct TaskContext {
    Player&      player;
    BlockSource& blockSource;
    BlockPos     startPos;
    HashedDimPos hashedStartPos;
    int          limit{0};
    int          dimension{0};
};

/**
 * @brief 派生任务处理桩。
 *
 * needsWorldFlush 为 false 时基类用 if constexpr 在编译期剔除 flushWorldUpdate 调用路径。
 */
template <typename D>
concept TaskHandlerConcept =
    requires(D& t, BlockSource& bs, BlockBFS::Pending const& elem, BlockPos const& p, long long cpuTime) {
        { D::needsWorldFlush } -> std::convertible_to<bool>;
        { t.initSearch() } -> std::same_as<void>;
        { t.matchBlock(bs, p) } -> std::convertible_to<bool>;
        {
            t.emitNeighbors(p, [](BlockPos const&) {})
        } -> std::same_as<void>;
        { t.consumeElement(bs, elem) } -> std::same_as<void>;
        { t.flushWorldUpdate() } -> std::same_as<void>;
        { t.onTaskCompleted(cpuTime) } -> std::same_as<void>;
    };

template <typename Derived>
class TaskBase : public TaskControl {
public:
    using Element = BlockBFS::Pending;

    TaskBase(TaskContext ctx, TaskDispatcher& dispatcher)
    : TaskControl(ctx.player),
      tool_(const_cast<ItemStack&>(ctx.player.getSelectedItem())),
      blockSource_(ctx.blockSource),
      startPos_(std::move(ctx.startPos)),
      hashedStartPos_(ctx.hashedStartPos),
      limit_(ctx.limit),
      dimension_(ctx.dimension),
      search_(DimPosHasher{ctx.dimension}),
      dispatcher_(dispatcher) {}

    FM_DISABLE_COPY_MOVE(TaskBase);

    /**
     * @brief 启动任务协程主循环。
     *
     * 配额耗尽时通过 TaskPermitAwaiter 挂起，下一 tick 由调度器恢复；
     * needsWorldFlush 为 true 时在每 tick 批边界提交世界更新。
     */
    inline void execute() {
        static_assert(TaskHandlerConcept<Derived>, "handler-less derived invalid: missing TaskHandlerConcept stubs");
        static_assert(std::is_base_of_v<TaskBase, Derived>, "TaskBase must be subclassed via CRTP static polymorphism");
        static_assert(
            !std::is_polymorphic_v<Derived>,
            "derived tasks must not introduce virtual functions (LTO inlining)"
        );

        state_ = State::Running;

        ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
            auto& self    = *static_cast<Derived*>(this);
            auto  awaiter = TaskPermitAwaiter{this, dispatcher_};

            auto totalCpuTime = std::chrono::milliseconds::zero();
            auto begin        = std::chrono::high_resolution_clock::now();

            self.initSearch();

            auto consume   = [&self](BlockSource& bs, Element const& e) { self.consumeElement(bs, e); };
            auto match     = [&self](BlockSource& bs, BlockPos const& p) { return self.matchBlock(bs, p); };
            auto neighbors = [&self](BlockPos const& p, auto&& emit) { self.emitNeighbors(p, emit); };

            while (count_ < limit_ && !search_.exhausted() && canContinue()) {
                if (quota_ == 0) {
                    auto end      = std::chrono::high_resolution_clock::now();
                    totalCpuTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
                    co_await awaiter;
                    if (!canContinue()) {
                        break;
                    }
                    begin = std::chrono::high_resolution_clock::now();
                }

                while (quota_ > 0 && count_ < limit_ && !search_.exhausted() && canContinue()) {
                    --quota_;
                    if (seeded_) {
                        // 预搜索集合已确定，直接消费不再扩散
                        search_.nextPop(blockSource_, consume);
                    } else {
                        search_.next(blockSource_, consume, match, neighbors);
                    }
                }
                // 每 tick 批边界统一提交世界更新，避免逐元素广播
                if constexpr (Derived::needsWorldFlush) {
                    self.flushWorldUpdate();
                }
            }
            auto end      = std::chrono::high_resolution_clock::now();
            totalCpuTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

            // 任务结束（含打断）时清空残留的世界更新，防止悬空状态
            if constexpr (Derived::needsWorldFlush) {
                self.flushWorldUpdate();
            }

            if (canContinue()) {
                state_ = State::Finished;
                self.onTaskCompleted(totalCpuTime.count());
            }
            co_return;
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    }

protected:
    ItemStack&      tool_;
    BlockSource&    blockSource_;
    BlockPos        startPos_;
    HashedDimPos    hashedStartPos_;
    int             limit_{0};
    int             dimension_;
    BlockBFS        search_;
    TaskDispatcher& dispatcher_;
    bool            seeded_{false};

    /**
     * @brief 消费期间将该坐标标记为处理中。
     * @note 事件重入路径在调度器处短路，避免同一坐标重复处理。
     */
    template <typename F>
    void withProcessingLock(HashedDimPos pos, F&& fn) {
        struct ProcessingGuard final {
            TaskDispatcher& d;
            HashedDimPos    p;

            explicit ProcessingGuard(TaskDispatcher& d, HashedDimPos p) : d(d), p(p) { d.insertProcessing(p); }
            ~ProcessingGuard() { d.eraseProcessing(p); }
        } guard{dispatcher_, pos};
        fn();
    }

    template <typename F>
        requires std::invocable<F>
    void scheduleFinish(F&& fn) {
        ll::coro::keepThis([this, fn = std::forward<F>(fn)]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks{1};
            fn();
            dispatcher_.onTaskFinished(this);
            co_return;
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    }
};


} // namespace fm