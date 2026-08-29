#include "ServerMinerLauncher.h"

#include "FastMiner.h"
#include "ServerPlatformService.h"
#include "config/ServerConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/ChainTask.h"
#include "utils/McUtils.h"

#include <mc/world/item/enchanting/EnchantUtils.h>

#include <memory>
#include <optional>

namespace fm::server {

/**
 * @brief 服务器完成策略：经济扣费 + 结算提示。
 * @note 编译期注入 ChainTask<ServerChainFinisher>。
 */
struct ServerChainFinisher {
    template <typename Task>
    void operator()(Task const& task, long long cpuTime) const {
        if (task.count_ <= 0) {
            return;
        }

        auto cost = task.blockConfig()->rawConfig.cost * task.count_;
        FastMiner::getInstance().getPlatformService().as<ServerPlatformService>().getEconomy().reduce(
            task.player_.getUuid(),
            cost
        );
        mc_utils::sendText(
            task.player_,
            "本次连锁了 {} 个方块, 消耗了 {} 点耐久, 花费 {} 点经济, 总耗时 {}ms",
            task.count_,
            task.deductDamage(),
            cost,
            cpuTime
        );
    }
};

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
        return false;
    }

    const bool hasSilkTouch = EnchantUtils::hasEnchant(Enchant::Type::SilkTouch, item);
    switch (config.silkTouchMode) {
    case SilkTouchMode::Unlimited:
        return true;
    case SilkTouchMode::Forbid:
        return !hasSilkTouch;
    case SilkTouchMode::Need:
        return hasSilkTouch;
    }
    return false;
}

int ServerMinerLauncher::calculateLimit(const ChainTaskContext& ctx) {
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

void ServerMinerLauncher::launchChainTask(
    ChainTaskContext             ctx,
    TaskDispatcher&              dispatcher,
    std::optional<PreSearchData> preSearch
) {
    auto task = std::make_shared<ChainTask<ServerChainFinisher>>(std::move(ctx), dispatcher, std::move(preSearch));
    dispatcher.launch(task);
}

} // namespace fm::server