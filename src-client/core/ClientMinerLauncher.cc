#include "ClientMinerLauncher.h"

#include "FastMiner.h"
#include "config/ClientConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/ChainTask.h"
#include "preview/ChainPreview.h"

#include <memory>

namespace fm::client {

struct ClientMinerLauncher::Impl {
};

ClientMinerLauncher::ClientMinerLauncher() : impl(std::make_unique<Impl>()) {
}

ClientMinerLauncher::~ClientMinerLauncher() = default; // 先析构 useLauncher（移除监听），再析构基类（关停调度器）


bool ClientMinerLauncher::isMinerEnabled(Player& /* player */, const std::string& /* blockType */) {
    return FastMiner::getInstance().getPlatformService().as<ClientPlatformService>().isKeyActivated();
}

bool ClientMinerLauncher::
    canDestroyBlockWithConfig(Player& /* player */, const RuntimeSingleBlockConfigPtr& /* rtConfig */) {
    return true;
}

std::optional<PreSearchData> ClientMinerLauncher::tryTakeClientPresearch(ChainTaskContext const& ctx) {
    // 仅当挖下的方块 == 预搜索锚点时交接；否则 nullopt，走服务端默认直接搜索提交
    auto* preview = ChainPreview::active();
    if (!preview) return std::nullopt;
    return preview->takeForPos(ctx.tiggerPos);
}

RuntimeSingleBlockConfigPtr ClientMinerLauncher::loadRuntimeSingleBlockConfig(const std::string& blockType) {
    auto overrideCfg = MinerLauncher::loadRuntimeSingleBlockConfig(blockType);
    if (!overrideCfg) {
        overrideCfg = ClientConfigImpl::getInstance().as<ClientConfigImpl>().getDefault();
    }
    return overrideCfg;
}

void ClientMinerLauncher::launchChainTask(
    ChainTaskContext             ctx,
    TaskDispatcher&              dispatcher,
    std::optional<PreSearchData> preSearch
) {
    // 客户端无经济结算：接入默认空完成策略
    dispatcher.launch(std::make_shared<ChainTask<>>(std::move(ctx), dispatcher, std::move(preSearch)));
}

} // namespace fm::client