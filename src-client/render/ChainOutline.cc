#include "render/ChainOutline.h"

#include "Global.h"
#include "preview/ChainPreview.h"
#include "render/VoxelOutlineRenderer.h"

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"

namespace fm::client {
namespace {

VoxelOutlineRenderer sRenderer;

} // namespace

// 实现注意：
// - $renderBlockEntities 在 SDK 中标记 MCFOLD，Hook 会同时拦截 LevelRendererShadowCamera 的折叠实现，
//   因此只在透明 pass（renderAlphaLayer==true）绘制。
// - 透明 pass 内提交是实测稳定路径；移到不透明 pass 会导致轮廓不显示。
LL_TYPE_INSTANCE_HOOK(
    ChainOutlineRenderHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderBlockEntities,
    void,
    ::BaseActorRenderContext& renderContext,
    bool                      renderAlphaLayer
) {
    origin(renderContext, renderAlphaLayer);
    if (renderAlphaLayer) {
        ::fm::client::ChainOutline::draw(renderContext);
    }
}

static bool gHooked = false;

void ChainOutline::install() {
    if (!gHooked) {
        gHooked = ChainOutlineRenderHook::hook() >= 0;
        FM_TRACE("[FastMiner] ChainOutline hook installed: " << (gHooked ? "true" : "false"));
    }
}

void ChainOutline::uninstall() {
    if (gHooked) {
        ChainOutlineRenderHook::unhook();
        gHooked = false;
    }
    sRenderer.clear();
}

void ChainOutline::draw(BaseActorRenderContext& ctx) {
    auto* preview = ChainPreview::active();
    if (!preview) return;

    auto snap = preview->getSnapshot();
    if (!snap || snap->blocks.empty()) {
        sRenderer.clear();
        return;
    }

    // 适配：业务快照 -> 通用渲染器输入视图（blocks 不拷贝，渲染器在版本变化时同步消费）
    VoxelOutlineRenderer::VoxelSet view{snap->generation, snap->anchor, snap->blocks};
    sRenderer.update(view);
    sRenderer.draw(ctx);
}

} // namespace fm::client