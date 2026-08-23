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


MinerTask::MinerTask(
    MinerTaskContext             ctx,
    MinerDispatcher&             dispatcher,
    NotifyFinishedHook           finishedHook,
    std::optional<PreSearchData> preSearch
)
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
  search_(DimPosHasher{ctx.tiggerDimid}),
  dispatcher_(dispatcher),
  preSearchBlocks_(preSearch ? std::move(*preSearch) : std::vector<BlockBFS::Pending>{}),
  seeded_(!preSearchBlocks_.empty()),
  notifyFinishedHook_(finishedHook) {
    blockChangeCtx_.mContextSource = ActorChangeContext{&player_};
}

void MinerTask::execute() {
    state_ = State::Running;
    ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
        auto awaiter = MinerPermitAwaiter{this, dispatcher_};

        auto totalCpuTime = std::chrono::milliseconds::zero(); // 总 CPU 耗时
        auto begin        = std::chrono::high_resolution_clock::now();

        if (seeded_) {
            // 预搜索交接：结果集已确定（含哈希键），无损接管、不再扩散搜索
            if (static_cast<int>(preSearchBlocks_.size()) > limit_) {
                preSearchBlocks_.resize(limit_);
            }
            search_.adopt(std::move(preSearchBlocks_));
        } else {
            // 起点方块入队作为搜索起点，玩家自己破坏方块不算一次
            search_.reset(startPos_, hashedStartPos_);
            // 无界搜索（挖掘停止由 count_ < limit_ 控制）：不限制搜索量，提前扩容减少 rehash
            search_.reserve(static_cast<size_t>(limit_) * 2, static_cast<size_t>(limit_) * 4);
        }

        // onPop     -- 出队元素消费（破坏），直接透传 Pending 引用
        // match     -- 方块匹配谓词（主方块 + 相似方块）
        // neighbors -- 方向扩展（Default 6 相邻 / Cube 3x3x3，方向组单一来源）
        auto onPop = [this](BlockSource&, BlockBFS::Pending const& element) { tryBreakBlock(element); };
        auto match = [this](BlockSource& bs, BlockPos const& p) -> bool {
            auto const& block = bs.getBlock(p);
            auto const  id    = block.getBlockItemId();
            return id == blockId_ || blockConfig_->similarBlock.contains(id);
        };
        auto neighbors = [this](BlockPos const& p, auto&& emit) {
            auto const& dirs =
                blockConfig_->rawConfig.destroyMode == DestroyMode::Cube ? cubeDirections() : adjacentDirections();
            for (auto d : dirs) {
                emit(BlockPos{p.x + d.dx, p.y + d.dy, p.z + d.dz});
            }
        };

        // 主循环：
        // 外层按「挖掘限额 + 队列非空 + 未打断」推进；内层以调度许可配额逐个消费元素，
        // 每个元素消费前后都复查 配额 / count 上限 / 打断 -- 挖掘达到 limit 立即停止
        // ，打断响应及时，已确定的交接集合同样受配额节流。
        while (count_ < limit_ && !search_.exhausted() && canContinue()) {
            if (quota_ == 0) {
                auto end      = std::chrono::high_resolution_clock::now();
                totalCpuTime += std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
                co_await awaiter;                                  // 等待许可额度
                if (!canContinue()) break;                         // 检查是否可以继续
                begin = std::chrono::high_resolution_clock::now(); // 重置开始时间
            }

            while (quota_ > 0 && count_ < limit_ && !search_.exhausted() && canContinue()) {
                quota_--;
                if (seeded_) {
                    // 交接：集合已确定，仅按序挖掘，不再扩散
                    search_.nextPop(blockSource_, onPop);
                } else {
                    // 边挖边搜：出队元素先消费（破坏）再方向扩展
                    search_.next(blockSource_, onPop, match, neighbors);
                }
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

    // 跳过起点方块
    // 某些边缘场景下，Minecraft 处理延迟导致 isAir 判空失败，导致重复计费
    if (hashed == hashedStartPos_ || pos == startPos_) [[unlikely]] {
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
    if (count_ <= 0) {
        return; // 没有破坏方块
    }
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
    ll::coro::keepThis([queue = search_.releaseQueue(), &bs = blockSource_]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks{1};
        for (auto const& element : queue) {
            bs.neighborChanged(element.blockPos, element.blockPos);
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