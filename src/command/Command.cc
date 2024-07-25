#include "Command.h"
#include "config/Config.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/registry/ItemStack.h"
#include "utils/JsonHelper.h"
#include "utils/Text.h"
#include <cstdint>
#include <fmt/core.h>
#include <initializer_list>
#include <ll/api/Logger.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/Optional.h>
#include <ll/api/command/Overload.h>
#include <ll/api/i18n/I18n.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/service/PlayerInfo.h>
#include <ll/api/service/Service.h>
#include <ll/api/utils/HashUtils.h>
#include <mc/entity/utilities/ActorType.h>
#include <mc/network/packet/LevelChunkPacket.h>
#include <mc/network/packet/TextPacket.h>
#include <mc/server/ServerLevel.h>
#include <mc/server/ServerPlayer.h>
#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOriginType.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandParameterOption.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandRegistry.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/world/actor/Actor.h>
#include <mc/world/actor/player/Player.h>
#include <sstream>


using JSON = nlohmann::json;
using namespace ll::form;
using ConfImpl = fm::config::ConfImpl;

namespace fm::command {

static JSON blockJson;

void sendSettingGUI(Player& player) {
    if (blockJson.empty()) {
        blockJson = utils::JsonHelper::structToJson(config::ConfImpl::cfg.blocks);
    }

    auto uuid = player.getUuid().asString();

    CustomForm f{PLUGIN_TITLE};
    f.appendToggle("enable", "启用连锁采集", ConfImpl::isEnable(uuid, "enable"));
    f.appendToggle("sneak", "仅潜行时启用", ConfImpl::isEnable(uuid, "sneak"));

    f.appendLabel("----- 方块配置 -----");

    for (auto& [k, v] : blockJson.items()) {
        f.appendToggle(k, v["name"], ConfImpl::isEnable(uuid, k));
    }

    f.sendTo(player, [](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) return;

        auto uuid = pl.getUuid().asString();

        bool enable = std::get<uint64_t>(res->at("enable"));
        bool sneak  = std::get<uint64_t>(res->at("sneak"));

        ConfImpl::setEnable(uuid, "enable", enable);
        ConfImpl::setEnable(uuid, "sneak", sneak);

        // Block
        for (auto& [k, v] : blockJson.items()) {
            ConfImpl::setEnable(uuid, k, std::get<uint64_t>(res->at(k)));
        }

        utils::sendText(pl, "设置已保存");
    });
}


struct StatusOption {
    enum class Status : int { off = 0, on = 1 } state;
    CommandSelector<Player> player;
};

void registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance().getOrCreateCommand("fm", "FastMiner - 连锁采集");

    // fm
    cmd.overload().execute([](CommandOrigin const& ori, CommandOutput& out) {
        if (ori.getOriginType() != CommandOriginType::Player) {
            return utils::sendText<utils::Level::Error>(out, "此命令仅限玩家使用");
        }
        Player& pl = *static_cast<Player*>(ori.getEntity());
        sendSettingGUI(pl);
    });

    // fm <on/off> [player]
    cmd.overload<StatusOption>().execute([](CommandOrigin const& ori, CommandOutput& out, StatusOption const& opt) {
        auto pls = opt.player.results(ori);

        auto       oriType   = ori.getOriginType();
        bool const isConsole = oriType == CommandOriginType::DedicatedServer;
        bool const isPlayer  = oriType == CommandOriginType::Player;

        if (isPlayer) {
            Player& pl = *static_cast<Player*>(ori.getEntity());
            if (pls.empty()) {
                ConfImpl::setEnable(pl.getUuid().asString(), "enable", (bool)opt.state);
                utils::sendText(pl, "设置已保存");
                return;
            }

            if (!pl.isOperator()) return;
        }

        if (!isConsole || !isPlayer || pls.empty()) return;

        for (auto pl : *pls.data) {
            if (pl != nullptr) ConfImpl::setEnable(pl->getUuid().asString(), "enable", (bool)opt.state);
        }

        utils::sendText(out, "设置已保存");
    });
}


} // namespace fm::command