#pragma once

#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"

#include <limits>


// v26.40 起 LeviLamina 导出的 SDK 不再声明下列接口（原函数被内联或收起），
// 此处按「成员 + 语义」复原。
//
// 说明：以下结论均在 IDA v1.21.133 (edu) 上逐条反编译核对，但**不复制任何反汇编偏移**。
// 该 IDB 是 macOS 构建，与 Windows 目标的类布局不同：
//   - libc++ 的 std::deque 为 48B，MSVC 为 40B -> MatrixStack 72B vs 64B
//   - std::variant 32B vs 48B -> BaseActorRenderContext 成员整体位移
// 因此 IDA 中的立即数只用于确认「访问的是哪个成员」，实际取值一律走 SDK 成员名。
namespace fm::client::helper {


/// RectangleArea::isEmpty
/// IDA: (y1 - y0) * (x1 - x0) < FLT_EPSILON
[[nodiscard]] inline bool isEmpty(RectangleArea const& area) {
    return (area._y1 - area._y0) * (area._x1 - area._x0) < std::numeric_limits<float>::epsilon();
}

/// RectangleArea::maxY —— v26.20 为 MCFOLD 内联访问器（故二进制中无符号），v26.40 被删除。
/// 由 isEmpty 取 (y1 - y0) 可知 _y1 为上边界，故 maxY() 即 _y1。
[[nodiscard]] inline float maxY(RectangleArea const& area) { return area._y1; }

/// RectangleArea::minX —— 同上，minX() 即 _x0。
[[nodiscard]] inline float minX(RectangleArea const& area) { return area._x0; }


/// FontHandle::isValid
/// IDA: mIsDummyHandle ? true : (mControlBlock 非空 && mControlBlock->mIsValid && mDefaultFont != nullptr)
[[nodiscard]] inline bool isValid(FontHandle const& h) {
    if (h.mIsDummyHandle) {
        return true;
    }
    auto& control = h.mFontRepository->mControlBlock;
    return control && control->mIsValid && h.mDefaultFont != nullptr;
}


/// BaseActorRenderContext::getTessellator
/// IDA: return *(mScreenContext + offsetof(ScreenContext, tessellator));
inline Tessellator& getTessellator(ScreenContext const& ctx) { return ctx.tessellator; }
inline Tessellator& getTessellator(BaseActorRenderContext const& ctx) { return getTessellator(ctx.mScreenContext); }

/// BaseActorRenderContext::getWorldMatrix
/// IDA: return *(mScreenContext + offsetof(mce::MeshContext, camera)).worldMatrixStack;
///   ScreenContext : UIScreenContext, mce::MeshContext，UIScreenContext 无虚表且占 12B，
///   故 MeshContext 起于 +16、camera 位于 +24。
///   交叉验证：同一路径下 tessellator 落在 +184，与 SDK 头文件按成员逐项算出的
///   偏移完全一致（frameBufferObject 136 / viewport 144 / guiData 152 / clock 176 /
///   tessellator 184），可确认 ScreenContext 布局在 v26.20 与 v26.40 之间未变。
[[nodiscard]] inline MatrixStack& getWorldMatrix(BaseActorRenderContext const& ctx) {
    return ctx.mScreenContext.camera.worldMatrixStack;
}

/// BaseActorRenderContext::getProjectionMatrix
/// IDA: 同 getWorldMatrix，成员为 camera.projectionMatrixStack。
[[nodiscard]] inline MatrixStack& getProjectionMatrix(BaseActorRenderContext const& ctx) {
    return ctx.mScreenContext.camera.projectionMatrixStack;
}


} // namespace fm::client::helper