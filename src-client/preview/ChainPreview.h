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
        std::vector<BlockPos> blocks{}; // 已搜到的连锁方块（不含起点，渲染侧需自行并入锚点）
    };

    /// 由 ClientPlatformService 在 init/destroy 注册 / 注销当前实例（供挖掘交接与渲染读取）
    static ChainPreview* active() { return sActive; }
    static void          setActive(ChainPreview* p) { sActive = p; }

    /// 连锁键按住状态（按键事件写入，tick 驱动读取）
    static bool keyHeld() { return sKeyHeld; }
    static void setKeyHeld(bool v) { sKeyHeld = v; }

    /// 每个 client tick 推进；keyActivated 为连锁键按住状态
    void tick(bool keyActivated);

    /// 清空预搜索（键松开 / 离开世界 / 描边关闭）
    void clear();

    /// 读取当前快照（渲染钩子 / 挖掘交接同线程直接读）
    std::shared_ptr<Snapshot const> getSnapshot() const { return snapshot_; }

    /// 挖掘交接：仅当 pos == 当前锚点才返回结果集（pos+key 成对，挖掘侧零再哈希），
    /// 否则 nullopt（走服务端默认搜索）
    std::optional<std::vector<BlockBFS::Pending>> takeForPos(BlockPos const& pos) const;

    ChainPreview() = default;

private:
    ChainPreview(ChainPreview const&)            = delete;
    ChainPreview& operator=(ChainPreview const&) = delete;

    /// 沿视线步进找到第一个非空气方块作为锚点
    bool resolveAnchor(LocalPlayer const& player, BlockSource const& bs, BlockPos& anchor) const;

    /// 搜索内容有变化时发布快照
    void publish();

    std::shared_ptr<const Snapshot> snapshot_;
    BlockBFS                        search_;    // 当前增量搜索（通用 BFS 引擎，行为由桩注入）
    RuntimeSingleBlockConfigPtr     curConfig_; // 持有的 rtConfig，保证 similarBlock/limit 生命周期
    BlockPos                        searchAnchor_{};
    int                             searchDim_{0};
    uint64_t                        generation_{0};
    bool                            searchRestartPending_{true};
    size_t                          lastPublishedSize_{0};

    static inline ChainPreview* sActive{nullptr};
    static inline bool          sKeyHeld{false};
};

} // namespace fm::client