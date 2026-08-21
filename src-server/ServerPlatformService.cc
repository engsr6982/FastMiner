#include "ServerPlatformService.h"

#include "command/FastMinerCommand.h"
#include "config/ServerConfigImpl.h"
#include "config/ServerConfigModel.h"

#include "econbridge/detail/LegacyMoneyEconomy.h"
#include "econbridge/detail/NullEconomy.h"
#include "econbridge/detail/ScoreboardEconomy.h"

namespace fm {
namespace server {

struct ServerPlatformService::Impl {
    std::unique_ptr<econbridge::IEconomy> mEconomy{nullptr};

    void initEconomy() {
        if (!ServerConfigImpl::model.economy.enabled) {
            mEconomy = std::make_unique<econbridge::detail::NullEconomy>();
            return;
        }
        switch (ServerConfigImpl::model.economy.kit) {
        case ConfigModel::EconomyConfig::EconomyKit::LegacyMoney:
            mEconomy = std::make_unique<econbridge::detail::LegacyMoneyEconomy>();
            break;
        case ConfigModel::EconomyConfig::EconomyKit::ScoreBoard:
            mEconomy =
                std::make_unique<econbridge::detail::ScoreboardEconomy>(ServerConfigImpl::model.economy.scoreboardName);
            break;
        }
    }
};

ServerPlatformService::ServerPlatformService() : impl(std::make_unique<Impl>()) {}

ServerPlatformService::~ServerPlatformService() = default;

bool ServerPlatformService::init() {
    FastMinerCommand::setup();
    impl->initEconomy();
    StaticGlobalConfigHost::getInstance().buildRuntimeMap();
    return true;
}

bool ServerPlatformService::destroy() {
    impl->mEconomy.reset();
    return true;
}

econbridge::IEconomy& ServerPlatformService::getEconomy() const { return *impl->mEconomy; }


} // namespace server
} // namespace fm