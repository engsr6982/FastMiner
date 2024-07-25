#include "Config.h"
#include "ll/api/Config.h"
#include "mod/Mod.h"
#include "nlohmann/json.hpp"
#include <exception>
#include <filesystem>
#include <fstream>
#include <unordered_map>


using JSON = nlohmann::json;
namespace fm::config {

namespace fs = std::filesystem;


Config                                                       ConfImpl::cfg{};
std::unordered_map<string, std::unordered_map<string, bool>> ConfImpl::disabledBlocks{};

void ConfImpl::load() {
    auto path = Mod::getInstance().getSelf().getModDir() / "Config.json";

    if (!fs::exists(path)) {
        save();
    }

    bool ok = ll::config::loadConfig(cfg, path);
    if (!ok) {
        save(); // rewrite
    }
}
void ConfImpl::save() {
    auto path = Mod::getInstance().getSelf().getModDir() / "Config.json";

    ll::config::saveConfig(cfg, path);
}


void ConfImpl::loadDisabledBlocks() {
    auto& mod    = Mod::getInstance().getSelf();
    auto& logger = mod.getLogger();
    auto  path   = mod.getModDir() / "PlayerSetting.json";
    if (!fs::exists(path)) {
        saveDisabledBlocks();
    }

    try {
        auto a = std::ifstream(path);
        auto j = JSON::parse(a);

        for (auto& [uuid, obj] : j.items()) {
            if (disabledBlocks.find(uuid) == disabledBlocks.end()) {
                disabledBlocks[uuid] = std::unordered_map<string, bool>{}; // init
            }
            for (auto& [type, bol] : obj.items()) {
                disabledBlocks[uuid][type] = bol;
            }
        }
    } catch (JSON::exception& err) {
        logger.error("Failed to load PlayerSetting.json: {}", err.what());
    } catch (std::exception& serr) {
        logger.error("Failed to load PlayerSetting.json: {}", serr.what());
    }
}
void ConfImpl::saveDisabledBlocks() {
    auto& mod    = Mod::getInstance().getSelf();
    auto& logger = mod.getLogger();
    auto  path   = mod.getModDir() / "PlayerSetting.json";
    try {
        JSON j;
        for (auto& [uuid, map] : disabledBlocks) {
            JSON obj;
            for (auto& [type, bol] : map) {
                obj[type] = bol;
            }
            j[uuid] = obj;
        }
        std::ofstream(path) << j.dump(4);
    } catch (JSON::exception& err) {
        logger.error("Failed to save PlayerSetting.json: {}", err.what());
    } catch (std::exception& serr) {
        logger.error("Failed to save PlayerSetting.json: {}", serr.what());
    }
}

void ConfImpl::saveDisabledBlockOnNewThread() {
    std::thread([]() { ConfImpl::saveDisabledBlocks(); }).detach();
}


bool ConfImpl::isBlockDisabled(const string& uuid, const string& typeName) {
    auto fn = disabledBlocks.find(uuid);
    if (fn != disabledBlocks.end()) {
        auto dt = fn->second.find(typeName);
        if (dt != fn->second.end()) {
            return dt->second;
        }
    }
    return false;
}
bool ConfImpl::disableBlock(const string& uuid, const string& typeName) {
    auto fn = disabledBlocks.find(uuid);
    if (fn == disabledBlocks.end()) {
        disabledBlocks[string(uuid)] = std::unordered_map<string, bool>{};
    }
    disabledBlocks[uuid][typeName] = true;
    saveDisabledBlockOnNewThread();
    return true;
}
bool ConfImpl::enableBlock(const string& uuid, const string& typeName) {
    auto fn = disabledBlocks.find(uuid);
    if (fn == disabledBlocks.end()) {
        disabledBlocks[string(uuid)] = std::unordered_map<string, bool>{};
    }
    disabledBlocks[uuid][typeName] = false;
    saveDisabledBlockOnNewThread();
    return true;
}


} // namespace fm::config