#pragma once
#include "core/ChainTaskContext.h"
#include "core/TaskBase.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/utils/RandomUtils.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include <algorithm>
#include <concepts>
#include <limits>
#include <optional>
#include <random>
#include <vector>

namespace fm {

/**
 * @brief 客户端默认完成策略：无额外副作用。
 */
struct NullFinishPolicy {
    template <typename Task>
    void operator()(Task const&, long long /* cpuTime */) const {}
};

/**
 * @brief 连锁挖掘任务
 *
 * 挖掘时逐块 playerDestroy + setBlock(air, flags=2)（仅广播利用子区块批量网络同步），
 * 邻居世界观模拟延后到每个 tick 批边界统- updateNeighborsAt（fl=1 的补齐）。
 */
template <typename FinishPolicy = NullFinishPolicy>
    requires std::default_initializable<FinishPolicy>
class ChainTask final : public TaskBase<ChainTask<FinishPolicy>> {
public:
    static constexpr bool needsWorldFlush = true;

    using Base    = TaskBase<ChainTask<FinishPolicy>>;
    using Element = typename Base::Element;

    inline ChainTask(
        ChainTaskContext             ctx,
        TaskDispatcher&              dispatcher,
        std::optional<PreSearchData> preSearch = std::nullopt
    )
    : Base(makeTaskContext(ctx), dispatcher),
      blockId_(ctx.blockId),
      blockConfig_(std::move(ctx.rtConfig)),
      eventBus_(ll::event::EventBus::getInstance()),
      durability_(EnchantUtils::getEnchantLevel(::Enchant::Type::Unbreaking, Base::tool_)),
      preSearchBlocks_(preSearch ? std::move(*preSearch) : PreSearchData{}) {
        blockChangeCtx_.mContextSource = ActorChangeContext{&this->player_};
        this->seeded_                  = !preSearchBlocks_.empty();

        // 按单 tick 最大可能方块数预分配，避免批边界反复 realloc
        auto const& cfg = StaticGlobalConfigHost::getDispatcherConfig();
        pendingUpdate_.reserve(std::min(this->limit_, cfg.globalBlockLimitPerTick));
    }

    FM_DISABLE_COPY_MOVE(ChainTask);

    inline void initSearch() {
        if (this->seeded_) {
            // 预搜索集合已确定，仅截断到上限后接管，不再扩散
            if (static_cast<int>(preSearchBlocks_.size()) > this->limit_) {
                preSearchBlocks_.resize(this->limit_);
            }
            this->search_.adopt(std::move(preSearchBlocks_));
        } else {
            // 起点方块由玩家亲手破坏，不计入任务计数
            this->search_.reset(this->startPos_, this->hashedStartPos_);
            this->search_.reserve(static_cast<size_t>(this->limit_) * 2, static_cast<size_t>(this->limit_) * 4);
        }
    }

    inline bool matchBlock(BlockSource& bs, BlockPos const& p) {
        auto const& block = bs.getBlock(p);
        auto const  id    = block.getBlockItemId();
        return id == blockId_ || blockConfig_->similarBlock.contains(id);
    }

    void emitNeighbors(BlockPos const& p, auto&& emit) {
        auto const& dirs =
            blockConfig_->rawConfig.destroyMode == DestroyMode::Cube ? cubeDirections() : adjacentDirections();
        for (auto d : dirs) {
            emit(BlockPos{p.x + d.dx, p.y + d.dy, p.z + d.dz});
        }
    }

    inline void consumeElement(BlockSource& bs, Element const& element) {
        auto const& [pos, hashed] = element;

        auto const& block = bs.getBlock(pos);
        if (block.isAir()) {
            return;
        }

        // 跳过起点方块：某些边缘场景下，Minecraft 处理延迟导致 isAir 判空失败，导致重复计费
        if (hashed == this->hashedStartPos_ || pos == this->startPos_) [[unlikely]] {
            return;
        }

        bool cancelled = false;
        this->withProcessingLock(hashed, [&] {
            auto event = ll::event::PlayerDestroyBlockEvent{this->player_, pos};
            eventBus_.publish(event);
            cancelled = event.isCancelled();
        });
        if (cancelled) {
            return;
        }

        ++this->count_;
        block.playerDestroy(this->player_, pos);
        static auto& air = BlockTypeRegistry::get().getDefaultBlockState("minecraft:air");
        this->blockSource_.setBlock(pos, air, 2, nullptr, blockChangeCtx_);

        pendingUpdate_.push_back(pos);
    }

    /**
     * @brief 批边界补上 setBlock(flags=2) 阶段跳过的邻居更新。
     * @note 保证液体/红石/附着方块正确响应。
     */
    inline void flushWorldUpdate() {
        for (auto const& pos : pendingUpdate_) {
            this->blockSource_.updateNeighborsAt(pos);
        }
        pendingUpdate_.clear();
    }

    inline void onTaskCompleted(long long cpuTime) {
        Base::scheduleFinish([this, cpuTime] {
            calculateDurabilityDeduction();
            if (this->tool_.isDamageableItem()) {
                short damage = this->tool_.getDamageValue() + deductDamage_;
                this->tool_.setDamageValue(damage);
                this->player_.refreshInventory();
            }
            FinishPolicy{}(*this, cpuTime);
        });
    }

    [[nodiscard]] inline RuntimeSingleBlockConfigPtr const& blockConfig() const noexcept { return blockConfig_; }
    [[nodiscard]] inline int                                deductDamage() const noexcept { return deductDamage_; }

private:
    inline static TaskContext makeTaskContext(ChainTaskContext const& ctx) {
        return TaskContext{ctx.player, ctx.blockSource, ctx.tiggerPos, ctx.hashedPos, ctx.limit, ctx.tiggerDimid};
    }

    inline void calculateDurabilityDeduction() {
        if (this->count_ <= 0) {
            return;
        }
        if (durability_ == 0) {
            deductDamage_ = this->count_;
            return;
        }
        struct LeviRngAdapter {
            using result_type = uint64_t;
            static constexpr result_type min() { return 0; }
            static constexpr result_type max() { return std::numeric_limits<uint64_t>::max(); }
            result_type                  operator()() { return ll::random_utils::rand<uint64_t>(); }
        } rng;

        double                          p = 1.0 / (static_cast<double>(durability_) + 1.0);
        std::binomial_distribution<int> dist(this->count_, p);
        deductDamage_ = dist(rng);
    }

    unsigned short const        blockId_;
    RuntimeSingleBlockConfigPtr blockConfig_;
    BlockChangeContext          blockChangeCtx_;
    ll::event::EventBus&        eventBus_;
    int const                   durability_;
    int                         deductDamage_{0};
    PreSearchData               preSearchBlocks_{}; // 两阶段交接：客户端预搜索确定的目标集合
    std::vector<BlockPos>       pendingUpdate_{};   // 本批次已挖掘但尚未 updateNeighborsAt 的方块
};


} // namespace fm