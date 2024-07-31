#include "config/Config.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "magic_enum.hpp"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/registry/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "utils/JsonHelper.h"
#include "utils/Text.h"
#include <mc/world/actor/Actor.h>
#include <mc/world/actor/player/Player.h>


using JSON = nlohmann::json;
using namespace ll::form;
using ConfImpl = fm::config::ConfImpl;


namespace fm::gui {

inline static JSON   blockJson;
inline static size_t blockSize;

inline static const std::vector<string> blockDestroyType = {
    string(magic_enum::enum_name(config::DestroyMod::Default)),
    string(magic_enum::enum_name(config::DestroyMod::Cube))
};
inline static const std::vector<string> blockSilkTouchType = {
    string(magic_enum::enum_name(config::SilkTouschMod::Unlimited)),
    string(magic_enum::enum_name(config::SilkTouschMod::Forbid)),
    string(magic_enum::enum_name(config::SilkTouschMod::Need))
};
inline void updateStaticCache(bool force = false) {
    if (blockJson.empty() || ConfImpl::cfg.blocks.size() != blockSize || force) {
        blockJson = utils::JsonHelper::structToJson(ConfImpl::cfg.blocks);
        blockSize = ConfImpl::cfg.blocks.size();
    }
}

// 声明
inline void sendBlockManager(Player& player);


// 实现
inline void _addBlock(Player& player, string typeName, config::BlockItem block) {
    CustomForm f{PLUGIN_TITLE};

    f.appendInput("typeName", "命名空间", "string", typeName);
    f.appendInput("name", "名称", "string", block.name);
    f.appendInput("cost", "消耗经济", "int", std::to_string(block.cost));
    f.appendInput("limit", "最大连锁数量", "int", std::to_string(block.limit));

    f.appendDropdown("destroyType", "破坏模式", blockDestroyType);
    f.appendDropdown("silkTouchType", "精准采集模式", blockSilkTouchType);

    f.sendTo(player, [](Player& pl, CustomFormResult const& res, FormCancelReason) {
        if (!res) return;
        try {
            string typeName = std::get<string>(res->at("typeName"));
            string name     = std::get<string>(res->at("name"));
            int    cost     = std::stoi(std::get<string>(res->at("cost")));
            int    limit    = std::stoi(std::get<string>(res->at("limit")));

            config::DestroyMod dmod =
                magic_enum::enum_cast<config::DestroyMod>(std::get<string>(res->at("destroyType")))
                    .value_or(config::DestroyMod::Default);
            config::SilkTouschMod smod =
                magic_enum::enum_cast<config::SilkTouschMod>(std::get<string>(res->at("silkTouchType")))
                    .value_or(config::SilkTouschMod::Unlimited);

            ConfImpl::cfg.blocks[typeName] = config::BlockItem{name, cost, limit, dmod, smod};

            ConfImpl::save();
            updateStaticCache(true);
            sendBlockManager(pl);
        } catch (std::exception& e) {
        } catch (...) {}
    });
}
inline void _addBlock(Player& player) {
    auto const& item  = player.getSelectedItem();
    auto        block = item.getBlock();
    if (!block) {
        utils::sendText(player, "获取方块失败!");
        return;
    }
    _addBlock(player, block->getTypeName(), config::BlockItem{item.getName()});
}

inline void _editBlockType(Player& player, string typeName, string content) {
    auto& block = ConfImpl::cfg.blocks[typeName];

    SimpleForm f{PLUGIN_TITLE};
    f.setContent(content);

    f.appendButton("编辑", "texture/ui/book_edit_hover", "path", [typeName, block](Player& pl) {
        _addBlock(pl, typeName, block);
    });
    f.appendButton("删除", "texture/ui/icon_trash", "path", [typeName](Player& pl) {
        ConfImpl::cfg.blocks.erase(typeName);
        ConfImpl::save();
        updateStaticCache(true);
        sendBlockManager(pl);
    });
    f.appendButton("返回", "texture/ui/icon_import", "path", [](Player& pl) { sendBlockManager(pl); });
    f.sendTo(player);
}

inline void sendBlockManager(Player& player) {
    updateStaticCache();

    SimpleForm f{PLUGIN_TITLE};
    f.setContent("FastMiner - 管理面板");

    f.appendButton("添加手持方块", "texture/ui/color_plus", "path", [](Player& pl) { _addBlock(pl); });

    for (auto& [k, v] : blockJson.items()) {
        f.appendButton(v["name"], [k, v](Player& pl) { _editBlockType(pl, k, v.dump(2)); });
    }

    f.sendTo(player);
}


inline void sendSettingGUI(Player& player) {
    updateStaticCache();

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


} // namespace fm::gui