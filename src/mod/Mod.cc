#include "mod/Mod.h"

#include <memory>

#include "config/Config.h"
#include "ll/api/mod/RegisterHelper.h"


namespace fm {

static std::unique_ptr<Mod> instance;

Mod& Mod::getInstance() { return *instance; }

bool Mod::load() {
    getSelf().getLogger().info("Loading...");

    config::ConfImpl::load();
    config::ConfImpl::loadDisabledBlocks();

    return true;
}

bool Mod::enable() {
    getSelf().getLogger().info("Enabling...");
    // Code for enabling the plugin goes here.
    return true;
}

bool Mod::disable() {
    getSelf().getLogger().info("Disabling...");
    // Code for disabling the plugin goes here.
    return true;
}

} // namespace fm

LL_REGISTER_MOD(fm::Mod, fm::instance);
