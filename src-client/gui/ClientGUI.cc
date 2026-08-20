#include "ClientGUI.h"

#include "config/ClientConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/ModalForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/deps/input/Keyboard.h"
#include "utils/JsonUtils.h"
#include "utils/McUtils.h"


namespace fm {
namespace client {

using namespace ll::form;

void ClientGUI::sendTo(Player& player) {
    SimpleForm fm{};
    fm.setTitle("FastMiner(Client) - Config");
    fm.setContent("请选择一个操作");

    fm.appendButton("添加方块", [](Player& player) { _handleAddItemBlock(player); });

    for (auto const& [blockType, blockConfig] : ClientConfigImpl::model.overrides) {
        fm.appendButton(blockConfig.name, [&blockType](Player& player) { _sendBlockViewer(player, blockType); });
    }

    fm.sendTo(player);
}
void ClientGUI::_sendBlockViewer(Player& player, std::string const& typeName) {
    auto const& block = ClientConfigImpl::model.overrides[typeName];

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
            "编辑相似方块",
            "textures/ui/book_edit_hover",
            "path",
            [typeName](Player& pl) { _sendEditSimilarBlock(pl, typeName); }
        )
        .appendButton(
            "删除",
            "textures/ui/icon_trash",
            "path",
            [typeName](Player& pl) {
                StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().removeBlockConfig(typeName);
                sendTo(pl);
            }
        )
        .appendButton("返回", "textures/ui/icon_import", "path", [](Player& pl) { sendTo(pl); })
        .sendTo(player);
}
void ClientGUI::_sendEditSimilarBlock(Player& player, std::string const& typeName) {
    auto& similarBlock = ClientConfigImpl::model.overrides[typeName].similarBlock;

    SimpleForm f{PLUGIN_NAME};
    f.appendButton("返回", "textures/ui/icon_import", "path", [typeName](Player& pl) { sendTo(pl); });
    f.appendButton("添加手持方块", "textures/ui/color_plus", "path", [typeName](Player& pl) {
        auto const& item = pl.getSelectedItem();
        if (item.isNull() || !item.isBlock()) {
            mc_utils::sendText<mc_utils::LogLevel::Error>(pl, "请手持一个方块!");
            return;
        }
        StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().addSimilarBlock(
            typeName,
            item.mBlock->getTypeName()
        );
        _sendEditSimilarBlock(pl, typeName);
    });
    f.appendDivider();
    for (auto const& similar : similarBlock) {
        f.appendButton(fmt::format("{}\n点击移除方块", similar), [similar, typeName]([[maybe_unused]] Player& pl) {
            StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().removeSimilarBlock(typeName, similar);
            _sendEditSimilarBlock(pl, typeName);
        });
    }
    f.sendTo(player);
}
void ClientGUI::_handleAddItemBlock(Player& player) {
    auto const& item = player.getSelectedItem();
    if (item.isNull()) {
        mc_utils::sendText<mc_utils::LogLevel::Error>(player, "请手持一个方块!");
    }

    if (!item.isBlock()) {
        mc_utils::sendText<mc_utils::LogLevel::Error>(player, "当前手持物品没有对应方块实例!");
        return;
    }
    auto block = item.mBlock;

    StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().addBlockConfig(
        block->getTypeName(),
        {.name = item.getName()}
    );
    _sendEditBlockConfig(player, block->getTypeName());
}

inline std::vector<std::string> const                     DestroyModeType = {"默认(相邻的6个方块)", "立方体(3x3x3)"};
inline std::unordered_map<std::string, DestroyMode> const DestroyModeMap  = {
    {DestroyModeType[0], DestroyMode::Default},
    {DestroyModeType[1], DestroyMode::Cube   }
};
void ClientGUI::_sendEditBlockConfig(Player& player, std::string const& typeName) {
    auto const& block = ClientConfigImpl::model.overrides[typeName];
    CustomForm  f{PLUGIN_NAME};

    f.appendInput("typeName", "命名空间", "string", typeName);
    f.appendInput("name", "名称", "string", block.name);
    f.appendInput("limit", "最大连锁数量\n-1 则无限制", "int", std::to_string(block.limit.value_or(-1)));

    f.appendDropdown("destroyMode", "破坏模式", DestroyModeType);

    f.sendTo(player, [last = typeName](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) return;
        try {
            std::string typeName = std::get<std::string>(res->at("typeName"));
            std::string name     = std::get<std::string>(res->at("name"));
            int         limit    = std::stoi(std::get<std::string>(res->at("limit")));
            DestroyMode dmod     = DestroyModeMap.at(std::get<std::string>(res->at("destroyMode")));

            std::optional<int> finalLimit = limit == -1 ? std::nullopt : std::optional<int>{limit};
            StaticGlobalConfigHost::getInstance().as<ClientConfigImpl>().updateBlockConfig(
                last,
                typeName,
                {name, finalLimit, dmod}
            );
            _sendBlockViewer(pl, typeName);
        } catch (...) {}
    });
}

} // namespace client
} // namespace fm