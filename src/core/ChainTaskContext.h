#pragma once
#include "Type.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/MinerUtil.h"

#include "mc/world/level/BlockPos.h"

class Player;
class BlockSource;

namespace fm {

/**
 * @brief 连锁挖掘任务启动时的一次性上下文。
 */
struct ChainTaskContext {
    Player&                     player;
    BlockID                     blockId;
    BlockPos                    tiggerPos;
    int                         tiggerDimid;
    HashedDimPos                hashedPos;
    BlockSource&                blockSource;
    RuntimeSingleBlockConfigPtr rtConfig;

    int limit{0};
};


} // namespace fm