#pragma once
#include "config/StaticGlobalConfigHost.h"
#include "core/MinerUtil.h"

#include "mc/world/level/BlockPos.h"

class Player;
class BlockSource;

namespace fm {

/**
 * @brief DTO 对象，用于辅助传参
 */
struct MinerTaskContext {
    Player&                     player;      // 玩家对象
    unsigned short const        blockId;     // 方块ID
    BlockPos                    tiggerPos;   // 触发位置
    int                         tiggerDimid; // 触发维度
    HashedDimPos                hashedPos;   // 触发位置的哈希值
    BlockSource&                blockSource; // 方块源
    RuntimeSingleBlockConfigPtr rtConfig;    // 方块配置

    int limit{0};
};


} // namespace fm