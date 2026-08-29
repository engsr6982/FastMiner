#pragma once
#include "core/BFS.h"
#include "core/MinerUtil.h"
#include "core/TaskBase.h"

#include "Type.h"
#include "mc/common/FacingID.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <optional>
#include <string>

class Player;
class ItemStack;

namespace fm::client {

struct UseTaskContext {
    Player&      player;
    BlockSource& blockSource;
    BlockPos     anchor; // 原版已对其使用过一次，任务内部跳过
    int          dimension{0};
    int          limit{0};
    BlockID      targetBlockId{}; // 原版使用前的方块类型快照，作为 BFS 匹配目标
    FacingID     face;
};

/**
 * @brief 右键使用物品任务：TaskBase 的静态多态特化（客户端专属）
 *
 * 玩家右键命中白名单物品时启动：对“与触发方块同类型”的邻域做 BFS，
 * 逐个调用玩家 GameMode::useItemOn 出队元素（与玩家原始右键同-条权威路径：
 * 完整交互/事件/判定/耐久/同步）。**目标是何物、此次使用是否有意义全由
 * Minecraft 自身判定**——锄头对 dirt 会翻耕、斧头对原木会去皮、种子对耕地会
 * 种植，代码层面不硬编码任何“物品 -> 目标方块”映射。
 *
 * needsWorldFlush=false —— 不用在批边界补提交，原版 useItemOn 自身完成世界观模拟，
 * if constexpr 直接剔除批量提交调用。
 */
class UseTask final : public TaskBase<UseTask> {
public:
    static constexpr bool needsWorldFlush = true;

    using Base    = TaskBase<UseTask>;
    using Element = Base::Element;

    UseTask(UseTaskContext ctx, TaskDispatcher& dispatcher);

    FM_DISABLE_COPY_MOVE(UseTask);

    void        initSearch();
    bool        matchBlock(BlockSource& bs, BlockPos const& p);
    inline void emitNeighbors(BlockPos const& p, auto&& emit) {
        auto const& dirs = adjacentDirections();
        for (auto d : dirs) {
            emit(BlockPos{p.x + d.dx, p.y + d.dy, p.z + d.dz});
        }
    }
    void consumeElement(BlockSource& bs, Element const& element);
    void flushWorldUpdate();
    void onTaskCompleted(long long cpuTime);

    static bool isWhitelisted(ItemStack const& item);
    static int  computeLimit(Player& player);

private:
    BlockID  targetBlockId_{0};
    FacingID face_{FacingID::Up};
};

static_assert(TaskHandlerConcept<UseTask>, "UseTask must satisfy the TaskBase handler contract");

} // namespace fm::client