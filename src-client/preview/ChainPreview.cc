#include "preview/ChainPreview.h"

#include "config/ClientConfigImpl.h"
#include "core/DispatcherConfig.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/util/BlockUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fm::client {

bool ChainPreview::resolveAnchor(LocalPlayer const& player, BlockSource const& bs, BlockPos& anchor) const {
    HitResult res = player.traceRay(
        5.25f, // 挖掘交互距离
        false, // 不检测实体
        true,
        [](BlockSource const&, Block const& block, bool) {
            // 返回 true 允许该方块被命中；液态（水 / 熔岩）穿透
            return !BlockUtils::isLiquidSource(block);
        }
    );
    if (res.mType != HitResultType::Tile || res.mIsHitLiquid) {
        return false;
    }
    anchor = res.mBlock;
    return !bs.getBlock(anchor).isAir();
}

void ChainPreview::publish() {
    auto const& result = search_.result();

    // 内容无变化不重复发布，避免渲染侧每帧重建 mesh
    if (snapshot_ && snapshot_->generation != 0 && snapshot_->anchor == searchAnchor_ && snapshot_->dimId == searchDim_
        && result.size() == lastPublishedSize_) {
        return;
    }

    lastPublishedSize_ = result.size();

    auto snap        = std::make_shared<Snapshot>();
    snap->generation = ++generation_;
    snap->anchor     = searchAnchor_;
    snap->dimId      = searchDim_;
    snap->blocks.reserve(result.size());
    for (auto const& p : result) {
        snap->blocks.push_back(p.blockPos); // 仅取坐标（渲染只消费 BlockPos），独立于增量搜索
    }
    snapshot_           = std::move(snap);
    static bool sLogged = false;
    if (!sLogged) {
        sLogged = true;
        FM_TRACE("[FastMiner] ChainPreview published snapshot, blocks=" << result.size());
    }
}

void ChainPreview::tick(bool keyActivated) {
    auto client = ll::service::getClientInstance();
    if (!client) {
        clear();
        return;
    }
    auto* player = client->getLocalPlayer();
    if (!player) {
        clear();
        return;
    }

    auto const& outline = StaticGlobalConfigHost::model.outline;
    if (!keyActivated || !outline.enabled) {
        clear();
        return;
    }

    auto&     bs  = player->getDimensionBlockSource();
    int const dim = static_cast<int>(player->getDimensionId());

    BlockPos anchor{};
    if (!resolveAnchor(*player, bs, anchor)) {
        clear();
        return;
    }

    auto const& block    = bs.getBlock(anchor);
    auto        rtConfig = StaticGlobalConfigHost::getRuntimeSingleBlockConfig(block.getTypeName());
    if (!rtConfig) {
        // 与 ClientMinerLauncher 一致：无覆盖配置时回退全局默认配置
        rtConfig = StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().getDefault();
    }
    if (!rtConfig) {
        clear();
        return;
    }

    int const kSearchCap = 1024; // 与挖掘侧 SAFETY_CAP 一致的安全兜底
    int const budget     = outline.searchPerTick > 0 ? outline.searchPerTick : 256;

    auto onPop = [](BlockSource&, BlockBFS::Pending const&) {};
    auto match = [&block, &rtConfig](BlockSource& bs, BlockPos const& p) -> bool {
        auto const& bl = bs.getBlock(p);
        auto const  id = bl.getBlockItemId();
        return id == block.getBlockItemId() || rtConfig->similarBlock.contains(id);
    };
    auto neighbors = [&rtConfig](BlockPos const& p, auto&& emit) {
        auto const& dirs =
            rtConfig->rawConfig.destroyMode == DestroyMode::Cube ? cubeDirections() : adjacentDirections();
        for (auto d : dirs) {
            emit(BlockPos{p.x + d.dx, p.y + d.dy, p.z + d.dz});
        }
    };

    try {
        // 缓存判定：命中锚点 / 维度 / 运行配置 完全一致才复用（同类型但不连续时不复用）
        if (!searchRestartPending_ && searchDim_ == dim && searchAnchor_ == anchor && curConfig_ == rtConfig) {
            if (!search_.done()) {
                search_.step(bs, budget, onPop, match, neighbors);
                publish();
            }
            return;
        }

        // 启动 / 重启搜索
        searchRestartPending_ = false;
        searchDim_            = dim;
        searchAnchor_         = anchor;
        curConfig_            = rtConfig;

        // 防御：未配置(INT_MAX) / 非正 / 过大上限统一夹到 kSearchCap，避免超长 reserve
        int const rawLimit = rtConfig->limit.value_or(std::numeric_limits<int>::max());
        int const limit    = (rawLimit == std::numeric_limits<int>::max() || rawLimit <= 0)
                               ? kSearchCap
                               : std::min(rawLimit, kSearchCap);

        // 重建引擎（哈希桩携带当前维度；丢弃上一轮全部状态）
        search_ = BlockBFS(DimPosHasher{dim});
        // 起点哈希沿用当前维度（与挖掘侧口径一致）
        auto const startKey = miner_util::hashDimensionPosition(anchor, dim);
        search_.reset(anchor, startKey, limit);
        search_.step(bs, budget, onPop, match, neighbors);
        publish();
    } catch (...) {
        clear();
    }
}

void ChainPreview::clear() {
    snapshot_ = nullptr;
    curConfig_.reset();
    searchAnchor_         = {};
    searchDim_            = 0;
    searchRestartPending_ = true;
    lastPublishedSize_    = 0;
}

std::optional<std::vector<BlockBFS::Pending>> ChainPreview::takeForPos(BlockPos const& pos) const {
    // 交接走引擎结果集（pos+key 成对，挖掘侧零再哈希）；仅锚点精确匹配才交接
    auto const& result = search_.result();
    if (result.empty() || !(searchAnchor_ == pos)) {
        return std::nullopt;
    }
    return std::vector<BlockBFS::Pending>(result.begin(), result.end());
}

} // namespace fm::client