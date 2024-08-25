#include "Command.h"
#include "GUI.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/SimpleForm.h"
#include "magic_enum.hpp"
#include "mc/world/level/block/Block.h"

#include "mc/nbt/CompoundTag.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/registry/ItemStack.h"


using ConfImpl = fm::config::ConfImpl;

namespace fm::command {


struct StatusOption {
    enum class Status : bool { off = false, on = true } state;
};

void registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance().getOrCreateCommand("fm", "FastMiner - 连锁采集");

    // fm
    cmd.overload().execute([](CommandOrigin const& ori, CommandOutput& out) {
        if (ori.getOriginType() != CommandOriginType::Player) {
            return utils::sendText<utils::Level::Error>(out, "此命令仅限玩家使用");
        }
        Player& pl = *static_cast<Player*>(ori.getEntity());
        gui::sendSettingGUI(pl);
    });

    // fm <on/off>
    cmd.overload<StatusOption>().required("state").execute(
        [](CommandOrigin const& ori, CommandOutput& out, StatusOption const& opt) {
            if (ori.getOriginType() != CommandOriginType::Player) {
                return utils::sendText<utils::Level::Error>(out, "此命令仅限玩家使用");
            }
            Player& pl = *static_cast<Player*>(ori.getEntity());

            ConfImpl::setEnable(pl.getUuid().asString(), "enable", (bool)opt.state);
            utils::sendText(pl, "设置已保存");
        }
    );

    // fm manager
    cmd.overload().text("manager").execute([](CommandOrigin const& ori, CommandOutput& out) {
        if (ori.getOriginType() != CommandOriginType::Player) {
            return utils::sendText<utils::Level::Error>(out, "此命令仅限玩家使用");
        }
        Player& pl = *static_cast<Player*>(ori.getEntity());
        if (!pl.isOperator()) {
            return utils::sendText<utils::Level::Error>(out, "无权限");
        }
        gui::sendBlockManager(pl);
    });

#ifdef DEBUG
    // fm debug unbreakable
    cmd.overload().text("debug").text("unbreakable").execute([](CommandOrigin const& ori, CommandOutput& out) {
        if (ori.getOriginType() != CommandOriginType::Player) {
            return utils::sendText<utils::Level::Error>(out, "此命令仅限玩家使用");
        }
        Player& pl = *static_cast<Player*>(ori.getEntity());

        auto& item = const_cast<ItemStack&>(pl.getSelectedItem());
        if (item.isNull()) {
            return utils::sendText<utils::Level::Error>(out, "请先选择一个物品");
        }

        auto nbt = item.save();
        auto tag = nbt->getCompound("tag");
        if (!tag) {
            nbt->putCompound("tag", CompoundTag{});
        }
        nbt->getCompound("tag")->putByte("Unbreakable", (byte) true);

        item.load(*nbt);
        pl.refreshInventory();

        utils::sendText(pl, "设置已保存");
    });

#endif
}


} // namespace fm::command