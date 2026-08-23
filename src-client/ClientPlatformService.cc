#include "ClientPlatformService.h"

#include "Global.h"
#include "command/FastMinerCommand.h"
#include "config/ClientConfigImpl.h"

#include "config/StaticGlobalConfigHost.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/input/KeyRegistry.h"

#include "ll/api/event/render/UIRenderEvent.h"

#include "preview/ChainPreview.h"
#include "render/ChainOutline.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace fm {
namespace client {

namespace {

// 在原版 HUD 左上角文本块（坐标 / 游玩天数，位于纸娃娃下方）的候选控件名里，
// 找出布局最靠下且可见的那个，把其真实屏幕区域写入 out，返回 true；无可见控件时返回 false。
// 控件名取自 vanilla hud_screen.json，随版本可能变动，这里用候选列表逐个查询以实现兼容。
// 注意：RectangleArea 的拷贝构造不可链接（LL 生成头 prevent），只能赋值或原地构造，
// 因此用 out-param 返回值，避免拷贝。
bool resolveVanillaTopLeftAnchor(ScreenView const& screenView, RectangleArea& out) {
    // chat_stack 纵向堆叠（自上而下）：paper_doll_padding -> non_centered_gui_padding
    // -> player_position -> number_of_days_played。坐标与游玩天数各有独立显示开关
    // （#player_position_visible / #number_of_days_played_visible），关闭时控件在
    // 布局中塌缩、区域为空。取「最靠下且非空」者作锚点，横幅落在其正下方，
    // 天然兼容任意开关组合。
    static std::array<std::string_view, 4> const kCandidates{
        "number_of_days_played", // 最下：游玩天数
        "player_position",       // 中间：坐标
        "paper_doll_padding",    // 最上：纸娃娃占位（纸娃娃显示时启用，实测可命中）
        "paper_doll",            // 兜底：纸娃娃渲染控件名（当前版本未观察到命中）
    };

    bool found = false;
    for (auto const name : kCandidates) {
        auto const area = screenView.getAreaOfControlByName(std::string{name});
        if (area.isEmpty()) continue;
        if (!found || area.maxY() > out.maxY()) out = area; // 取最靠下的，即文本块底部
        found = true;
    }
    return found;
}

// 在原版 HUD 左上角布局中绘制「连锁已启用」横幅：整体随原版
// 「纸娃娃 -> 坐标/游玩天数」文本块对齐，横幅放在该文本块正下方，避免覆盖原版信息，
// 且不依赖自算视口尺寸。
// 样式：黑底 + 原版聊天黄阴影文本，水平居中。
void drawToggleBanner(ll::event::AfterUIRenderEvent const& event) {
    auto& ctx        = event.uiRenderContext();
    auto& screenView = event.screenView();

    // 仅在 HUD 屏幕绘制。暂停/设置等其它屏幕同样触发 UI 渲染事件，
    // 若不排除，会在这些屏幕上多画出一个左上角 fallback 横幅。
    if (screenView.getScreenName() != "hud_screen") return;

    auto const& fontHandle = ctx.mClient.getFontHandle(); // 绑定临时对象，避免拷贝 FontHandle
    if (!fontHandle.isValid()) return;
    auto& font = fontHandle.getFont();

    std::string const text = "连锁已启用";

    float const fontSize   = 1.0f;
    float const lineHeight = 9.0f; // MC 默认字体行高约 9
    float const padX       = 6.0f;
    float const padY       = 3.0f;

    float const textWidth = static_cast<float>(ctx.getLineLength(font, text, fontSize, false));
    float const bannerW   = textWidth + padX * 2.0f;
    float const bannerH   = lineHeight + padY * 2.0f;

    RectangleArea anchor{};
    float         x0 = 0.0f;
    float         y0 = 0.0f;
    if (resolveVanillaTopLeftAnchor(screenView, anchor)) {
        x0 = anchor.minX();
        y0 = anchor.maxY() + 2.0f; // 紧随原版文本块之下
    } else {
        // 纸娃娃/坐标/游玩天数全隐藏时左上角已无原版信息，退化为固定留白，
        // 保证连锁状态始终可见
        x0 = 4.0f;
        y0 = 4.0f;
    }

    RectangleArea const bgRect{x0, y0, x0 + bannerW, y0 + bannerH, true};
    RectangleArea const textRect{x0 + padX, y0 + padY, x0 + bannerW - padX, y0 + bannerH - padY, true};

    // 黑底。178 ≈ alpha 0.7（同 vanilla 位置/天数控件的 textures/ui/Black），
    ctx.fillRectangle(bgRect, mce::Color(0, 0, 0, 178), 0.7f);

    // 原版聊天黄文本（renderShadow=true 带阴影），水平居中
    auto const yellow{mce::Color(0xFFFF55u)};
    ctx.drawText(
        font,
        textRect,
        std::string{text},
        yellow,
        1.0f,
        ui::TextAlignment::Center,
        TextMeasureData{fontSize, 1.0f, true, false, false, ui::TextAlignment::Center},
        CaretMeasureData{-1, false}
    );

    // AfterUIRenderEvent 在引擎 flush 之后触发，绘制项必须自行提交。
    // 先刷背景、再刷文本，保证文本绘制在背景之上。
    ctx.flushImages(mce::Color::WHITE(), 1.0f, "ui_fillColor");
    ctx.flushText(0.0f, std::nullopt);
}

} // namespace

struct ClientPlatformService::Impl {
    ll::event::ListenerPtr        mClientJoinLevelListener{nullptr};
    ll::event::ListenerPtr        mKeyInputListener{nullptr};
    ll::event::ListenerPtr        mLevelTickListener{nullptr};
    ll::event::ListenerPtr        mUIRenderListener{nullptr};
    std::unique_ptr<ChainPreview> mPreview;
    std::atomic<bool>             mKeyActivated{false};
};

ClientPlatformService::ClientPlatformService() : impl(std::make_unique<Impl>()) {}

ClientPlatformService::~ClientPlatformService() = default;

bool ClientPlatformService::init() {
    impl->mClientJoinLevelListener =
        ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>(
            [](ll::event::ClientJoinLevelEvent&) {
                // 由于初始化时序问题，客户端侧需要等待玩家进入世界后再初始化运行时数据
                FM_TRACE("Client joined level, building runtime map...");
                StaticGlobalConfigHost::getInstance().buildRuntimeMap();
                FM_TRACE("Client joined level, building runtime map... done");

                FM_TRACE("Client joined level, registering commands...");
                FastMinerCommand::setup();
                FM_TRACE("Client joined level, registering commands... done");
            }
        );

    auto& key_registry = ll::input::KeyRegistry::getInstance();
    key_registry.getOrCreateKey("FastMiner Toggle", {ClientConfigImpl::model.bindKey});

    impl->mKeyInputListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
        [this](ll::event::KeyInputEvent& event) {
            if (event.keyCode() == ClientConfigImpl::model.bindKey) {
                bool const down = event.isDown();
                impl->mKeyActivated.store(down, std::memory_order_relaxed);
                ChainPreview::setKeyHeld(down);
            }
        }
    );

    impl->mUIRenderListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
        [this](ll::event::AfterUIRenderEvent& event) {
            if (!impl->mKeyActivated.load(std::memory_order_relaxed)) return;
            drawToggleBanner(event);
        }
    );

    impl->mPreview = std::make_unique<ChainPreview>();
    ChainPreview::setActive(impl->mPreview.get());
    ChainOutline::install();

    impl->mLevelTickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>(
        [this](ll::event::ClientLevelTickEvent&) {
            if (impl->mPreview) {
                impl->mPreview->tick(impl->mKeyActivated.load(std::memory_order_relaxed));
            }
        }
    );

    return true;
}

bool ClientPlatformService::destroy() {
    ll::event::EventBus::getInstance().removeListener(impl->mLevelTickListener);
    impl->mLevelTickListener = nullptr;
    ChainOutline::uninstall();
    ChainPreview::setActive(nullptr);
    ChainPreview::setKeyHeld(false);
    impl->mPreview.reset();
    ll::event::EventBus::getInstance().removeListener(impl->mUIRenderListener);
    ll::event::EventBus::getInstance().removeListener(impl->mClientJoinLevelListener);
    ll::event::EventBus::getInstance().removeListener(impl->mKeyInputListener);
    return true;
}

bool ClientPlatformService::isKeyActivated() const { return impl->mKeyActivated.load(std::memory_order_relaxed); }


} // namespace client
} // namespace fm