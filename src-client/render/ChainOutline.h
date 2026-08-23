#pragma once

class BaseActorRenderContext;

namespace fm::client {

/**
 * @brief 连锁范围透视轮廓渲染器（两阶段阶段一的渲染侧）
 *
 * 挂 LevelRendererPlayer::renderBlockEntities（透明 pass），读取 ChainPreview 活跃实例
 * 快照，绘制整个连锁区域的体积外壳轮廓（只画外缘一圈，内部不画线）：
 * - 灰线 pass：关闭材质深度测试/深度写（真透视）整体提交 -> 被地形/体积遮挡的边
 *   显示为灰色（透过遮挡可见，提供景深信息）；
 * - 白线 pass：保持深度测试原状提交（仅关深度写）-> 被遮挡边被深度缓冲裁剪，
 *   只留真正可见的白色轮廓（颜色语义完全由深度测试决定，不做手写朝向判定）；
 * - 两条 mesh 顶点均为锚点本地坐标，相机相对位置由世界矩阵平移完成；
 *   仅集合变化（generation）时重建，站立/移动/转头零 CPU 开销。
 */
class ChainOutline {
public:
    /// 安装渲染钩子（由 ClientPlatformService 调用）
    static void install();

    /// 卸载渲染钩子
    static void uninstall();

    /// 渲染钩子回调入口（透明 pass，游戏主线程）
    static void draw(BaseActorRenderContext& ctx);

private:
    ChainOutline() = delete;
};

} // namespace fm::client