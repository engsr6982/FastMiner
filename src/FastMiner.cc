#include "FastMiner.h"
#include "config/StaticGlobalConfigHost.h"


#include "ll/api/mod/RegisterHelper.h"

#include <memory>

#include "BuildInfo.h"
#include "ll/api/thread/ThreadPoolExecutor.h"
#include "telemetry/Telemetry.h"


#include "trait/MinerLauncherTrait.h"

namespace fm {

using LauncherImpl = internal::ImplType<tag::MinerLauncherTag>::type;

struct FastMiner::Impl {
    ll::mod::NativeMod&                  mSelf;
    std::unique_ptr<LauncherImpl>        mLauncher{nullptr};
    std::unique_ptr<PlatformServiceImpl> mPlatformService{nullptr};

    std::unique_ptr<ll::thread::ThreadPoolExecutor> mThreadPoolExecutor{nullptr};
    std::unique_ptr<Telemetry>                      mTelemetry{nullptr};

    Impl() : mSelf(*ll::mod::NativeMod::current()) {}
};

FastMiner::FastMiner() : mImpl(std::make_unique<Impl>()) {}

ll::mod::NativeMod& FastMiner::getSelf() { return mImpl->mSelf; }

FastMiner& FastMiner::getInstance() {
    static FastMiner instance;
    return instance;
}

bool FastMiner::load() { return true; }

bool FastMiner::enable() {
    auto& instance = StaticGlobalConfigHost::getInstance();
    instance.buildDefault();

    if (auto ok = instance.load(); !ok) {
        ok.error().log(getSelf().getLogger());
        return false;
    }

    // 解耦合，不同平台初始化时序不同，需要拆分到平台特定的初始化逻辑中
    // 客户端侧如果在 enable 阶段调用 buildRuntimeMap，会导致游戏崩溃
    // 因此 enable 阶段只能是初始化一些全局的配置，不涉及平台特定的初始化
    // instance.buildRuntimeMap();

    mImpl->mPlatformService = std::make_unique<PlatformServiceImpl>();
    mImpl->mPlatformService->init();

    mImpl->mLauncher = std::make_unique<LauncherImpl>();

    mImpl->mThreadPoolExecutor = std::make_unique<ll::thread::ThreadPoolExecutor>("FastMiner", 1);
    mImpl->mTelemetry          = std::make_unique<Telemetry>(30641, BuildInfo::Tag.data());
    if (instance.isTelemetryEnabled()) {
        mImpl->mTelemetry->launch(*mImpl->mThreadPoolExecutor);
    }

    return true;
}

bool FastMiner::disable() {
    (void)StaticGlobalConfigHost::getInstance().save();

    mImpl->mTelemetry->shutdown();
    mImpl->mTelemetry.reset();
    mImpl->mThreadPoolExecutor->destroy();
    mImpl->mThreadPoolExecutor.reset();

    mImpl->mLauncher.reset();
    mImpl->mPlatformService->destroy();
    mImpl->mPlatformService.reset();

    return true;
}

bool FastMiner::unload() { return true; }

FastMiner::PlatformServiceImpl& FastMiner::getPlatformService() const { return *mImpl->mPlatformService; }

} // namespace fm

LL_REGISTER_MOD(fm::FastMiner, fm::FastMiner::getInstance());
