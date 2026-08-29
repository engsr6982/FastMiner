#pragma once

#include "absl/types/span.h"

#include "mc/world/level/BlockPos.h"

#include <cstdint>
#include <memory>
#include <vector>

class BaseActorRenderContext;

namespace mce {
class Mesh;
} // namespace mce

namespace fm::client {

/**
 * @brief 通用体素集合外壳轮廓渲染器（两阶段阶段一的渲染侧）
 *
 * 输入一个体素集合（世界坐标锚点 + 成员坐标），绘制其体积外壳轮廓：
 * - 灰线 pass：关闭材质深度测试/深度写（真透视）整体提交 -> 被地形/体积遮挡的边
 *   显示为灰色（透过遮挡可见，提供景深信息）；
 * - 白线 pass：保持深度测试原状提交（仅关深度写）-> 被遮挡边被深度缓冲裁剪，
 *   只留真正可见的白色轮廓（颜色语义完全由深度测试决定）；
 * - 两条 mesh 顶点均为锚点本地坐标，相机相对位置由世界矩阵平移完成；
 * - 几何（线段集 + mesh）仅在集合版本（generation）变化时重建，
 *   站立/移动/转头零 CPU 开销。
 */
class VoxelOutlineRenderer {
public:
    /**
     * @brief 体素集合输入视图。
     *
     * blocks 不持有数据，仅本次 update() 调用内有效；渲染器在版本变化时同步消费。
     */
    struct VoxelSet {
        uint64_t                   generation{0}; // 集合版本号：变化才重建几何
        BlockPos                   anchor{};      // 世界坐标锚点（本地坐标原点）
        absl::Span<BlockPos const> blocks{};      // 集合成员坐标（不含锚点）
    };

    /**
     * @brief 提交新集合视图；generation/anchor 均未变化则零拷贝返回。
     *
     * 版本变化时立即重建线段几何（无需渲染上下文），mesh 延后到 draw() 重建。
     * 须与 draw() 同线程（渲染线程）成对调用。
     */
    void update(VoxelSet const& view);

    /**
     * @brief 清空全部几何与缓存状态（集合失效/停止渲染/卸载时调用）。
     */
    void clear();

    /**
     * @brief 渲染两遍轮廓（灰 + 白）；update 后几何过期则先重建 mesh 再提交。
     *
     * 仅可渲染线程调用（读取 BaseActorRenderContext 的 tessellator/世界矩阵）。
     */
    void draw(BaseActorRenderContext& ctx);

    VoxelOutlineRenderer();
    ~VoxelOutlineRenderer();

private:
    VoxelOutlineRenderer(VoxelOutlineRenderer const&)            = delete;
    VoxelOutlineRenderer& operator=(VoxelOutlineRenderer const&) = delete;

    /**
     * @brief 体积外轮廓线段集（渲染线程独占）。
     *
     * 两端点为锚点本地坐标，法线为外侧朝向（白线沿其做表面外偏移）。
     */
    struct Edge {
        float ax, ay, az;
        float bx, by, bz;
        float nx, ny, nz;
    };

    void rebuildEdges(absl::Span<BlockPos const> blocks, BlockPos const& anchor);
    void buildOutlineMesh(BaseActorRenderContext& ctx);

    // 已消费的集合版本（几何重建判定基准；UINT64_MAX = 尚未消费任何集合）
    uint64_t edgeGen_{UINT64_MAX};
    BlockPos edgeAnchor_{};

    // 渲染线程独占的几何缓存
    std::vector<Edge>          edges_;
    std::unique_ptr<mce::Mesh> meshGray_;
    std::unique_ptr<mce::Mesh> meshWhite_;
    int                        grayVerts_{0};
    int                        whiteVerts_{0};
    bool                       meshValid_{false};
    bool                       meshDirty_{false};
};

} // namespace fm::client