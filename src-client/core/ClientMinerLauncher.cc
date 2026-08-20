#include "ClientMinerLauncher.h"

#include "FastMiner.h"

namespace fm {
namespace client {


bool ClientMinerLauncher::isMinerEnabled(Player& player, const std::string& blockType) {
    return FastMiner::getInstance().getPlatformService().as<ClientPlatformService>().isKeyActivated();
}

bool ClientMinerLauncher::canDestroyBlockWithConfig(Player& player, const RuntimeSingleBlockConfigPtr& rtConfig) {
    return true;
}

RuntimeSingleBlockConfigPtr ClientMinerLauncher::loadRuntimeSingleBlockConfig(const std::string& blockType) {
    auto overrideCfg = MinerLauncher::loadRuntimeSingleBlockConfig(blockType);
    if (!overrideCfg) {
        // TODO: load default config
        // overrideCfg =
    }
    return overrideCfg;
}

} // namespace client
} // namespace fm