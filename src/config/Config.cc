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
std::unordered_map<string, std::unordered_map<string, bool>> ConfImpl::playerSetting{};

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


void ConfImpl::loadPlayerSetting() {
    auto& mod    = Mod::getInstance().getSelf();
    auto& logger = mod.getLogger();
    auto  path   = mod.getModDir() / "PlayerSetting.json";
    if (!fs::exists(path)) {
        savePlayerSetting();
    }

    try {
        auto a = std::ifstream(path);
        auto j = JSON::parse(a);

        for (auto& [uuid, obj] : j.items()) {
            if (playerSetting.find(uuid) == playerSetting.end()) {
                playerSetting[uuid] = std::unordered_map<string, bool>{}; // init
            }
            for (auto& [type, bol] : obj.items()) {
                playerSetting[uuid][type] = bol;
            }
        }
    } catch (JSON::exception& err) {
        logger.error("Failed to load PlayerSetting.json: {}", err.what());
    } catch (std::exception& serr) {
        logger.error("Failed to load PlayerSetting.json: {}", serr.what());
    }
}
void ConfImpl::savePlayerSetting() {
    auto& mod    = Mod::getInstance().getSelf();
    auto& logger = mod.getLogger();
    auto  path   = mod.getModDir() / "PlayerSetting.json";
    try {
        JSON j;
        for (auto& [uuid, map] : playerSetting) {
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

void ConfImpl::savePlayerSettingOnNewThread() {
    std::thread([]() { ConfImpl::savePlayerSetting(); }).detach();
}


bool ConfImpl::isEnable(const string& uuid, const string& typeName) {
    auto fn = playerSetting.find(uuid);
    if (fn != playerSetting.end()) {
        auto dt = fn->second.find(typeName);
        if (dt != fn->second.end()) {
            return dt->second;
        }
    }
    return false;
}
bool ConfImpl::disable(const string& uuid, const string& typeName) {
    auto fn = playerSetting.find(uuid);
    if (fn == playerSetting.end()) {
        playerSetting[string(uuid)] = std::unordered_map<string, bool>{};
    }
    playerSetting[uuid][string(typeName)] = false;
    savePlayerSettingOnNewThread();
    return true;
}
bool ConfImpl::enable(const string& uuid, const string& typeName) {
    auto fn = playerSetting.find(uuid);
    if (fn == playerSetting.end()) {
        playerSetting[string(uuid)] = std::unordered_map<string, bool>{};
    }
    playerSetting[uuid][string(typeName)] = true;
    savePlayerSettingOnNewThread();
    return true;
}
bool ConfImpl::setEnable(const string& uuid, const string& typeName, bool isEnable) {
    auto fn = playerSetting.find(uuid);
    if (fn == playerSetting.end()) {
        playerSetting[string(uuid)] = std::unordered_map<string, bool>{};
    }
    playerSetting[uuid][string(typeName)] = isEnable;
    savePlayerSettingOnNewThread();
    return true;
}


} // namespace fm::config