#include "ClientMinerLauncher.h"

#include "FastMiner.h"
#include "config/ClientConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"
#include "preview/ChainPreview.h"

#include <memory>

namespace fm {
namespace client {


bool ClientMinerLauncher::isMinerEnabled(Player& /* player */, const std::string& /* blockType */) {
    return FastMiner::getInstance().getPlatformService().as<ClientPlatformService>().isKeyActivated();
}

bool ClientMinerLauncher::
    canDestroyBlockWithConfig(Player& /* player */, const RuntimeSingleBlockConfigPtr& /* rtConfig */) {
    return true;
}

std::optional<MinerTask::PreSearchData> ClientMinerLauncher::tryTakeClientPresearch(MinerTaskContext const& ctx) {
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

} // namespace client
} // namespace fm