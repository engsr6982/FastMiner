#include "use/UseTask.h"

#include "config/StaticGlobalConfigHost.h"
#include "core/MinerUtil.h"
#include "core/TaskDispatcher.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include <absl/container/flat_hash_set.h>
#include <algorithm>
#include <string>
#include <vector>

namespace fm::client {

namespace {

/// 以方块中心向使用面方向偏移半个边长：等价于点击该面正中央
Vec3 hitPos(BlockPos const& p, FacingID face) {
    float const cx = static_cast<float>(p.x) + 0.5f;
    float const cy = static_cast<float>(p.y) + 0.5f;
    float const cz = static_cast<float>(p.z) + 0.5f;
    switch (face) {
    case FacingID::Down:
        return Vec3{cx, cy - 0.5f, cz};
    case FacingID::Up:
        return Vec3{cx, cy + 0.5f, cz};
    case FacingID::North:
        return Vec3{cx, cy, cz - 0.5f};
    case FacingID::South:
        return Vec3{cx, cy, cz + 0.5f};
    case FacingID::West:
        return Vec3{cx - 0.5f, cy, cz};
    case FacingID::East:
        return Vec3{cx + 0.5f, cy, cz};
    default:
        return Vec3{cx, cy, cz};
    }
}

} // namespace

UseTask::UseTask(UseTaskContext ctx, TaskDispatcher& dispatcher)
: Base(
      // 哈希锚点沿用当前维度（与连锁挖掘口径-致）
      TaskContext{
          ctx.player,
          ctx.blockSource,
          ctx.anchor,
          miner_util::hashDimensionPosition(ctx.anchor, ctx.dimension),
          ctx.limit,
          ctx.dimension
      },
      dispatcher
  ),
  targetBlockId_(ctx.targetBlockId),
  face_(ctx.face) {}

void UseTask::initSearch() {
    // 锚点入队作搜索起点；玩家原版右键已对该方块使用过-次，消费时跳过
    this->search_.reset(this->startPos_, this->hashedStartPos_);
    // 无界搜索（停止由 count_ < limit_ 控制）：提前扩容减少 rehash
    this->search_.reserve(static_cast<size_t>(this->limit_) * 2, static_cast<size_t>(this->limit_) * 4);
}

bool UseTask::matchBlock(BlockSource& bs, BlockPos const& p) {
    // 与触发方块同类型即入队（具体能否使用由 useItemOn 判定）
    return bs.getBlock(p).getBlockItemId() == targetBlockId_;
}

void UseTask::consumeElement(BlockSource& bs, Element const& element) {
    auto const& [pos, hashed] = element;

    // 跳过锚点：原版路径已经使用过它，避免重复触发行为
    if (hashed == this->hashedStartPos_ || pos == this->startPos_) [[unlikely]] {
        return;
    }

    // 跨 tick 恢复后防御：方块状态可能已改变（如已被连锁作用过）
    auto const& block = bs.getBlock(pos);
    if (block.isAir() || block.getBlockItemId() != targetBlockId_) {
        return;
    }

    ++this->count_;
    // 处理集去重下调用原版 useItemOn：批内逐块引发的事件重入路径在此短路
    this->withProcessingLock(hashed, [this, &block, &pos] {
        // 与玩家原始右键同-条权威路径（完整交互/事件/判定/耐久/同步）；
        // 目标方块是否适配该物品（翻耕/去皮/种植）由 Minecraft 自行判定
        (void)this->player_.getGameMode()
            .useItemOn(this->tool_, pos, static_cast<unsigned char>(face_), hitPos(pos, face_), &block, false);
    });
}

void UseTask::flushWorldUpdate() {
    this->player_.refreshInventory(); // 刷新容器，确保耐久、方块数量同步
}

void UseTask::onTaskCompleted(long long /* cpuTime */) {
    // 耐久已由 useItemOn 逐次扣减（若有），无附加后处理；1 tick 延后自调度器移除
    Base::scheduleFinish([&] { this->flushWorldUpdate(); });
}

bool UseTask::isWhitelisted(ItemStack const& item) {
    auto const& whitelist = StaticGlobalConfigHost::model.useItems;
    auto const& type      = item.getTypeName();
    return std::find(whitelist.begin(), whitelist.end(), type) != whitelist.end();
}

int UseTask::computeLimit(Player& player) {
    constexpr int kDefaultUseLimit = 256; // 与连锁挖掘默认上限-致的安全兜底

    int   limit = kDefaultUseLimit;
    auto& item  = player.getSelectedItem();
    // 物品会损耗时才 clamp（防爆工具）
    if (item.isDamageableItem()) {
        if (auto it = item.getItem()) {
            int remaining = it->getMaxDamage() - item.getDamageValue() - 1; // 保留 1 点耐久不爆
            limit         = std::min(limit, std::max(0, remaining));
        }
    }
    return limit;
}

} // namespace fm::client