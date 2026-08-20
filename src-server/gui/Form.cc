#include "gui/Form.h"

#include "config/ServerConfigImpl.h"
#include "config/ServerConfigModel.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/DispatcherConfig.h"
#include "utils/JsonUtils.h"
#include "utils/McUtils.h"


#include "fmt/format.h"

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include <mc/world/actor/Actor.h>
#include <mc/world/actor/player/Player.h>

#include <string>
#include <vector>


using namespace ll::form;

namespace fm::server::gui {

inline std::vector<std::string> const                     DestroyModeType = {"默认(相邻的6个方块)", "立方体(3x3x3)"};
inline std::unordered_map<std::string, DestroyMode> const DestroyModeMap  = {
    {DestroyModeType[0], DestroyMode::Default},
    {DestroyModeType[1], DestroyMode::Cube   }
};

inline std::vector<std::string> const                       SilkTouchType = {"无限制", "禁用精准采集", "仅限精准采集"};
inline std::unordered_map<std::string, SilkTouchMode> const SilkTouchMap  = {
    {SilkTouchType[0], SilkTouchMode::Unlimited},
    {SilkTouchType[1], SilkTouchMode::Forbid   },
    {SilkTouchType[2], SilkTouchMode::Need     }
};

void __sendEditBlockTools(Player& player, std::string const& typeName) {
    auto& tools = ServerConfigImpl::model.blocks[typeName].tools;

    SimpleForm f{PLUGIN_NAME};
    f.appendButton("返回", "textures/ui/icon_import", "path", [typeName](Player& pl) {
        _sendBlockViewer(pl, typeName);
    });
    f.appendButton("添加手持工具", "textures/ui/color_plus", "path", [typeName](Player& pl) {
        auto const& item = pl.getSelectedItem();
        if (item.isNull() || mc_utils::isBlock(item)) {
            mc_utils::sendText<mc_utils::LogLevel::Error>(pl, "请手持一个工具!");
            return;
        }
        StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().addTool(typeName, item.getTypeName());
        __sendEditBlockTools(pl, typeName);
    });
    f.appendDivider();
    for (auto const& tool : tools) {
        f.appendButton(fmt::format("{}\n点击移除工具", tool), [tool, typeName]([[maybe_unused]] Player& pl) {
            StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().removeTool(typeName, tool);
            __sendEditBlockTools(pl, typeName);
        });
    }
    f.sendTo(player);
}

void __sendEditSimilarBlock(Player& player, std::string const& typeName) {
    auto& similarBlock = ServerConfigImpl::model.blocks[typeName].similarBlock;

    SimpleForm f{PLUGIN_NAME};
    f.appendButton("返回", "textures/ui/icon_import", "path", [typeName](Player& pl) {
        _sendBlockViewer(pl, typeName);
    });
    f.appendButton("添加手持方块", "textures/ui/color_plus", "path", [typeName](Player& pl) {
        auto const& item = pl.getSelectedItem();
        if (item.isNull() || !mc_utils::isBlock(item)) {
            mc_utils::sendText<mc_utils::LogLevel::Error>(pl, "请手持一个方块!");
            return;
        }
        StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().addSimilarBlock(
            typeName,
            item.mBlock->getTypeName()
        );
        __sendEditSimilarBlock(pl, typeName);
    });
    f.appendDivider();
    for (auto const& similar : similarBlock) {
        f.appendButton(fmt::format("{}\n点击移除方块", similar), [similar, typeName]([[maybe_unused]] Player& pl) {
            StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().removeSimilarBlock(typeName, similar);
            __sendEditSimilarBlock(pl, typeName);
        });
    }
    f.sendTo(player);
}

void _sendEditBlockConfig(Player& player, std::string const& typeName) {
    auto const& block = ServerConfigImpl::model.blocks[typeName];
    CustomForm  f{PLUGIN_NAME};

    f.appendInput("typeName", "命名空间", "string", typeName);
    f.appendInput("name", "名称", "string", block.name);
    f.appendInput("cost", "消耗经济", "int", std::to_string(block.cost));
    f.appendInput("limit", "最大连锁数量", "int", std::to_string(block.limit));

    f.appendDropdown("destroyMode", "破坏模式", DestroyModeType);
    f.appendDropdown("silkTouchMode", "精准采集模式", SilkTouchType);

    f.sendTo(player, [last = typeName](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) return;
        try {
            std::string   typeName = std::get<std::string>(res->at("typeName"));
            std::string   name     = std::get<std::string>(res->at("name"));
            int           cost     = std::stoi(std::get<std::string>(res->at("cost")));
            int           limit    = std::stoi(std::get<std::string>(res->at("limit")));
            DestroyMode   dmod     = DestroyModeMap.at(std::get<std::string>(res->at("destroyMode")));
            SilkTouchMode smod     = SilkTouchMap.at(std::get<std::string>(res->at("silkTouchMode")));

            StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().updateBlockConfig(
                last,
                typeName,
                {name, cost, limit, dmod, smod}
            );
            _sendBlockViewer(pl, typeName);
        } catch (...) {}
    });
}

void _addHandheldItemBlock(Player& player) {
    auto const& item = player.getSelectedItem();
    if (item.isNull()) {
        mc_utils::sendText<mc_utils::LogLevel::Error>(player, "请手持一个方块!");
    }

    if (!mc_utils::isBlock(item)) {
        mc_utils::sendText<mc_utils::LogLevel::Error>(player, "当前手持物品没有对应方块实例!");
        return;
    }
    auto block = item.mBlock;

    StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().addBlockConfig(
        block->getTypeName(),
        {.name = item.getName()}
    );
    _sendEditBlockConfig(player, block->getTypeName());
}

void _sendBlockViewer(Player& player, std::string const& typeName) {
    auto const& block = ServerConfigImpl::model.blocks[typeName];

    SimpleForm{PLUGIN_NAME}
        .setContent(json_utils::struct2json(block).dump(2))
        .appendDivider()
        .appendButton(
            "编辑基础信息",
            "textures/ui/book_edit_hover",
            "path",
            [typeName](Player& pl) { _sendEditBlockConfig(pl, typeName); }
        )
        .appendButton(
            "编辑连锁工具",
            "textures/ui/book_edit_hover",
            "path",
            [typeName](Player& pl) { __sendEditBlockTools(pl, typeName); }
        )
        .appendButton(
            "编辑相似方块",
            "textures/ui/book_edit_hover",
            "path",
            [typeName](Player& pl) { __sendEditSimilarBlock(pl, typeName); }
        )
        .appendButton(
            "删除",
            "textures/ui/icon_trash",
            "path",
            [typeName](Player& pl) {
                StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().removeBlockConfig(typeName);
                StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().removeBlock(pl.getUuid(), typeName);
                StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>().savePlayerConfig();
                sendOpBlockManager(pl);
            }
        )
        .appendButton("返回", "textures/ui/icon_import", "path", [](Player& pl) { sendOpBlockManager(pl); })
        .sendTo(player);
}

void sendOpBlockManager(Player& player) {
    SimpleForm f{PLUGIN_NAME};
    f.setContent("FastMiner - 管理面板");

    f.appendButton("添加手持方块", "textures/ui/color_plus", "path", [](Player& pl) { _addHandheldItemBlock(pl); });
    f.appendDivider();

    for (auto& [k, v] : ServerConfigImpl::model.blocks) {
        f.appendButton(v.name, [k](Player& pl) { _sendBlockViewer(pl, k); });
    }

    f.sendTo(player);
}


void sendPlayerConfigGUI(Player& player) {
    auto const& uuid = player.getUuid();

    auto& impl = StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>();

    CustomForm f{PLUGIN_NAME};
    f.appendToggle(
        ServerConfigImpl::KEY_ENABLE.data(),
        "启用连锁采集",
        impl.isEnabled(uuid, ServerConfigImpl::KEY_ENABLE.data())
    );
    f.appendToggle(
        ServerConfigImpl::KEY_SNEAK.data(),
        "仅潜行时启用",
        impl.isEnabled(uuid, ServerConfigImpl::KEY_SNEAK.data())
    );

    f.appendDivider();
    f.appendLabel(">>>  方块配置");
    for (auto const& [key, item] : ServerConfigImpl::model.blocks) {
        f.appendToggle(key, item.name, impl.isEnabled(uuid, key));
    }

    f.sendTo(player, [&impl](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) return;

        auto const& uuid = pl.getUuid();

        bool const enable = std::get<uint64_t>(res->at(ServerConfigImpl::KEY_ENABLE.data()));
        bool const sneak  = std::get<uint64_t>(res->at(ServerConfigImpl::KEY_SNEAK.data()));
        impl.setEnabled(uuid, ServerConfigImpl::KEY_ENABLE.data(), enable);
        impl.setEnabled(uuid, ServerConfigImpl::KEY_SNEAK.data(), sneak);

        for (auto& [k, v] : ServerConfigImpl::model.blocks) {
            if (res->contains(k)) {
                impl.setEnabled(uuid, k, std::get<uint64_t>(res->at(k)));
            }
        }
        impl.savePlayerConfig();
        mc_utils::sendText(pl, "设置已保存");
    });
}


} // namespace fm::server::gui