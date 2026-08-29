#pragma once
#include "Global.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/BFS.h"
#include "core/MinerUtil.h"

#include "mc/world/level/BlockPos.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class BlockSource;
class LocalPlayer;

namespace fm::client {

/**
 * @brief 客户端预搜索控制器（两阶段阶段一）
 *
 * 由客户端游戏 tick（ClientLevelTickEvent）驱动：连锁键按住时用原版 traceRay 拾取
 * 目标方块，用通用 BFS 引擎（core/BFS.h）按预算增量搜索连锁方块集合，并发布快照供
 * ChainOutline 绘制外壳轮廓；挖掘时由 takeForPos 把集合交接给连锁任务
 * （仅锚点精确匹配才交接，避免把过期集合误交付给其它位置）。
 */
class ChainPreview {
public:
    struct Snapshot {
        uint64_t              generation{0};
        BlockPos              anchor{};
        int                   dimId{0};
        std::vector<BlockPos> blocks{}; // 不含起点
    };

    static ChainPreview* active() { return sActive; }
    static void          setActive(ChainPreview* p) { sActive = p; }

    static bool keyHeld() { return sKeyHeld; }
    static void setKeyHeld(bool v) { sKeyHeld = v; }

    void tick(bool keyActivated);

    void clear();

    std::shared_ptr<Snapshot const> getSnapshot() const { return snapshot_; }

    /**
     * @brief 仅当 pos 与当前锚点一致时才交接预搜索集合，避免误交付给其它位置。
     */
    std::optional<std::vector<BlockBFS::Pending>> takeForPos(BlockPos const& pos) const;

    ChainPreview() = default;

private:
    ChainPreview(ChainPreview const&)            = delete;
    ChainPreview& operator=(ChainPreview const&) = delete;

    bool resolveAnchor(LocalPlayer const& player, BlockSource const& bs, BlockPos& anchor) const;

    void publish();

    std::shared_ptr<const Snapshot> snapshot_;
    BlockBFS                        search_;
    RuntimeSingleBlockConfigPtr     curConfig_; // 持有 rtConfig 以保证 similarBlock/limit 生命周期覆盖搜索过程
    BlockPos                        searchAnchor_{};
    int                             searchDim_{0};
    uint64_t                        generation_{0};
    bool                            searchRestartPending_{true};
    size_t                          lastPublishedSize_{0};

    static inline ChainPreview* sActive{nullptr};
    static inline bool          sKeyHeld{false};
};

} // namespace fm::client