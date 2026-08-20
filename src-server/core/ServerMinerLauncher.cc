#include "ServerMinerLauncher.h"

#include "FastMiner.h"
#include "config/ServerConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"
#include "utils/McUtils.h"


#include <mc/world/item/enchanting/EnchantUtils.h>

namespace fm::server {

bool ServerMinerLauncher::isMinerEnabled(Player& player, const std::string& blockType) {
    if (!player.isSurvival()) {
        FM_TRACE("player not survival");
        return false;
    }

    auto& inst = StaticGlobalConfigHost::getInstance().as<ServerConfigImpl>();
    auto& uuid = player.getUuid();
    if (!inst.isEnabled(uuid, ServerConfigImpl::KEY_ENABLE.data())) {
        FM_TRACE("player miner disabled");
        return false;
    }

    bool sneakingRequired = inst.isEnabled(uuid, ServerConfigImpl::KEY_SNEAK.data());
    bool sneaking         = mc_utils::isSneaking(player);
    if (sneakingRequired && !sneaking) {
        FM_TRACE("sneaking required but player is not sneaking");
        return false;
    }
    return inst.isEnabled(uuid, blockType);
}

bool ServerMinerLauncher::canDestroyBlockWithConfig(Player& player, const RuntimeSingleBlockConfigPtr& rtConfig) {
    const auto& config = rtConfig->rawConfig;
    const auto& item   = player.getSelectedItem();

    if (!config.tools.empty() && !config.tools.contains(item.getTypeName())) {
        return false; // 限制了工具 && 工具不匹配
    }

    const bool hasSilkTouch = EnchantUtils::hasEnchant(Enchant::Type::SilkTouch, item);
    switch (config.silkTouchMode) {
    case SilkTouchMode::Unlimited:
        return true; // 不限制精准采集
    case SilkTouchMode::Forbid:
        return !hasSilkTouch; // 禁止精准采集
    case SilkTouchMode::Need:
        return hasSilkTouch; // 需要精准采集
    }
    return false;
}
MinerTask::NotifyFinishedHook ServerMinerLauncher::getNotifyFinishedHook(MinerTaskContext const& ctx) {
    return [](MinerTask const& task, long long cpuTime) {
        if (task.count_ <= 0) {
            return; // 没有挖掘到任何方块
        }

        auto cost = task.blockConfig_->rawConfig.cost * task.count_;
        FastMiner::getInstance().getPlatformService().as<ServerPlatformService>().getEconomy().reduce(
            task.player_.getUuid(),
            cost
        );
        mc_utils::sendText(
            task.player_,
            "本次连锁了 {} 个方块, 消耗了 {} 点耐久, 花费 {} 点经济, 总耗时 {}ms",
            task.count_,
            task.deductDamage_,
            cost,
            cpuTime
        );
    };
}
int ServerMinerLauncher::calculateLimit(const MinerTaskContext& ctx) {
    int limit = MinerLauncher::calculateLimit(ctx);
    if (ServerConfigImpl::model.economy.enabled && ctx.rtConfig->rawConfig.cost > 0) {
        // 动态约束限制为玩家经济
        limit = std::min(
            limit,
            static_cast<int>(
                FastMiner::getInstance().getPlatformService().as<ServerPlatformService>().getEconomy().get(
                    ctx.player.getUuid()
                )
                / ctx.rtConfig->rawConfig.cost
            )
        );
    }
    return limit;
}

} // namespace fm::server