#pragma once

class BaseActorRenderContext;

namespace fm::client {

/**
 * @brief 连锁范围轮廓渲染适配层（两阶段阶段一的渲染侧）
 *
 * 挂 LevelRendererPlayer::renderBlockEntities（透明 pass），把 ChainPreview 发布的
 * 集合快照适配为通用 VoxelOutlineRenderer::VoxelSet 并驱动绘制；轮廓的线段生成、
 * 双 pass mesh、深度语义等渲染逻辑全部由 VoxelOutlineRenderer 承担。
 */
class ChainOutline {
public:
    static void install();
    static void uninstall();
    static void draw(BaseActorRenderContext& ctx);

private:
    ChainOutline() = delete;
};

} // namespace fm::client