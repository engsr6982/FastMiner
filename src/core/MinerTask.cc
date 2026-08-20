#include "MinerTask.h"
#include "FastMiner.h"
#include "core/MinerDispatcher.h"
#include "core/MinerPermitAwaiter.h"
#include "core/MinerTaskContext.h"
#include "core/MinerUtil.h"
#include "utils/McUtils.h"

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/utils/RandomUtils.h"

#include "mc/server/ServerLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include <cstddef>
#include <vector>

// BlockChangeContext::BlockChangeContext() = default;

namespace fm {


inline static std::vector<MinerTask::Direction> AdjacentDirections = {
    {1,  0,  0 },
    {-1, 0,  0 },
    {0,  1,  0 },
    {0,  -1, 0 },
    {0,  0,  1 },
    {0,  0,  -1}
};

inline static std::vector<MinerTask::Direction> CubeDirections = {
    {1,  0,  0 },
    {-1, 0,  0 },
    {0,  1,  0 },
    {0,  -1, 0 },
    {0,  0,  1 },
    {0,  0,  -1},
    {1,  0,  1 },
    {-1, 0,  1 },
    {0,  1,  1 },
    {0,  -1, 1 },
    {1,  1,  1 },
    {-1, -1, 1 },
    {1,  -1, 1 },
    {-1, 1,  1 },
    {1,  0,  -1},
    {-1, 0,  -1},
    {0,  1,  -1},
    {0,  -1, -1},
    {1,  1,  -1},
    {-1, -1, -1},
    {1,  -1, -1},
    {-1, 1,  -1},
    {1,  1,  0 },
    {-1, -1, 0 },
    {1,  -1, 0 },
    {-1, 1,  0 }
};


MinerTask::MinerTask(MinerTaskContext ctx, MinerDispatcher& dispatcher, NotifyFinishedHook finishedHook)
: player_(ctx.player),
  tool_(const_cast<ItemStack&>(player_.getSelectedItem())),
  blockId_(ctx.blockId),
  startPos_(std::move(ctx.tiggerPos)),
  hashedStartPos_(ctx.hashedPos),
  blockConfig_(std::move(ctx.rtConfig)),
  blockSource_(ctx.blockSource),
  limit_(ctx.limit),
  dimension_(ctx.tiggerDimid),
  durability_(EnchantUtils::getEnchantLevel(::Enchant::Type::Unbreaking, tool_)),
  //   blockChangeCtx_(ActorChangeContext{&player_}),
  eventBus_(ll::event::EventBus::getInstance()),
  // TODO: 不再硬编码访问 rawConfig，抽离向量方向为传入配置
  directions_(blockConfig_->rawConfig.destroyMode == DestroyMode::Cube ? CubeDirections : AdjacentDirections),
  dispatcher_(dispatcher),
  notifyFinishedHook_(finishedHook) {
    blockChangeCtx_.mContextSource = ActorChangeContext{&player_};
}

void MinerTask::execute() {
    queue_.reserve(limit_);
    visited_.reserve(limit_ * 2);

    state_ = State::Running;
    ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
        auto awaiter = MinerPermitAwaiter{this, dispatcher_};

        auto totalCpuTime = std::chrono::milliseconds::zero(); // 总 CPU 耗时
        auto begin        = std::chrono::high_resolution_clock::now();

        queue_.emplace_back(startPos_, hashedStartPos_);
        visited_.insert(hashedStartPos_);
        count_++; // 任务启动时玩家自己破坏方块也算一次(任务对齐)

        size_t head = 0; // 队列头
        while (count_ < limit_ && head < queue_.size() && canContinue()) {
            if (quota_ == 0) {
                auto end      = std::chrono::high_resolution_clock::now();
                totalCpuTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
                co_await awaiter;                                  // 等待许可额度
                if (!canContinue()) break;                         // 检查是否可以继续
                begin = std::chrono::high_resolution_clock::now(); // 重置开始时间
            }

            // 消费许可额度
            while (quota_ > 0 && count_ < limit_ && canContinue()) {
                quota_--;
                auto const& element = queue_[head++];
                tryBreakBlock(element);
                searchAdjacentBlocks(element);
            }
        }
        auto end      = std::chrono::high_resolution_clock::now();
        totalCpuTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

        if (canContinue()) {
            notifyFinished(totalCpuTime.count());
        }

        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

void MinerTask::tryBreakBlock(QueueElement const& element) {
    auto const& [pos, hashed] = element;

    auto const& block = blockSource_.getBlock(pos);
    if (block.isAir()) {
        return;
    }

    dispatcher_.insertProcessing(hashed);

    auto event = ll::event::PlayerDestroyBlockEvent{player_, pos};
    eventBus_.publish(event);

    dispatcher_.eraseProcessing(hashed);
    if (event.isCancelled()) {
        return;
    }

    count_++; // 累计破坏方块数量
    block.playerDestroy(player_, pos);
    // blockSource_.removeBlock(pos, blockChangeCtx_);
    static auto& air = BlockTypeRegistry::get().getDefaultBlockState("minecraft:air");
    blockSource_.setBlock(pos, air, 2, nullptr, blockChangeCtx_);
}


void MinerTask::calculateDurabilityDeduction() {
    if (durability_ == 0) {
        deductDamage_ = count_; // 无耐久附魔，按照破坏数量扣除耐久
        return;
    }
    struct LeviRngAdapter {
        using result_type = uint64_t;
        static constexpr result_type min() { return 0; }
        static constexpr result_type max() { return std::numeric_limits<uint64_t>::max(); }
        result_type                  operator()() { return ll::random_utils::rand<uint64_t>(); }
    } rng;

    // 二项分布
    double p = 1.0 / (static_cast<double>(durability_) + 1.0);

    std::binomial_distribution<int> dist(count_, p);

    deductDamage_ = dist(rng);
}

void MinerTask::searchAdjacentBlocks(QueueElement const& element) {
    auto const& pos = element.blockPos;
    for (auto [dx, dy, dz] : directions_) {
        BlockPos adjacent{pos.x + dx, pos.y + dy, pos.z + dz};
        auto     hashed = miner_util::hashDimensionPosition(adjacent, dimension_);
        if (visited_.insert(hashed).second) {
            auto const& block = blockSource_.getBlock(adjacent);
            auto const  id    = block.getBlockItemId();
            if (id == blockId_ || blockConfig_->similarBlock.contains(id)) {
                queue_.emplace_back(std::move(adjacent), std::move(hashed)); // 加入搜索队列
            }
        }
    }
}

void MinerTask::notifyFinished(long long cpuTime) {
    ll::coro::keepThis([this, cpuTime]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks{1};
        calculateDurabilityDeduction();
        if (!miner_util::hasUnbreakable(tool_)) {
            short damage = tool_.getDamageValue() + deductDamage_;
            tool_.setDamageValue(damage);
            player_.refreshInventory();
        }

        if (notifyFinishedHook_) {
            notifyFinishedHook_(*this, cpuTime);
        }

        notifyClientBlockUpdate();
        state_ = State::Finished;
        dispatcher_.onTaskFinished(this);
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

void MinerTask::notifyClientBlockUpdate() {
    // 任务已完成，可以把资源转移走
    // 对于 Client Side，MinerLauncher 拦截了客户端本地请求，所以这里是服务端侧资源，向客户端更新
    ll::coro::keepThis([queue = std::move(queue_), &bs = blockSource_]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks{1};
        for (auto const& [pos, _] : queue) {
            bs.neighborChanged(pos, pos);
        }
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

void MinerTask::interrupt() { state_ = State::Interrupted; }
bool MinerTask::isInterrupted() const { return state_ == State::Interrupted; }
bool MinerTask::isFinished() const { return state_ == State::Finished; }
bool MinerTask::isRunning() const { return state_ == State::Running; }
bool MinerTask::isPending() const { return state_ == State::Pending; }
bool MinerTask::canContinue() const { return state_ == State::Running || state_ == State::Pending; }

} // namespace fm