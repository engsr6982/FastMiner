#pragma once
#include "core/MinerLauncher.h"
#include "absl/container/flat_hash_set.h"
#include "config/StaticGlobalConfigHost.h"
#include "core/MinerDispatcher.h"
#include "core/MinerTask.h"
#include "core/MinerTaskContext.h"
#include "core/MinerUtil.h"
#include "utils/McUtils.h"


#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/coro/InterruptableSleep.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace fm {

struct MinerLauncher::Impl {
    std::unique_ptr<MinerDispatcher> dispatcher; // 调度器
    ll::event::ListenerPtr           playerDestroyBlockListener;
    ll::event::ListenerPtr           playerDisconnectListener;
    std::atomic<bool>                abort; // 是否需要中止
    ll::coro::InterruptableSleep     sleep; // 中断等待
};

MinerLauncher::MinerLauncher() : impl(std::make_unique<Impl>()) {
    impl->dispatcher = std::make_unique<MinerDispatcher>();

    auto& bus                        = ll::event::EventBus::getInstance();
    impl->playerDestroyBlockListener = bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(
        [&](auto& ev) { onPlayerDestroyBlock(ev); },
        ll::event::EventPriority::Low
    );
    impl->playerDisconnectListener = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([&](auto& ev) {
        auto& player = ev.self();
        impl->dispatcher->interruptPlayerTask(player);
    });

    impl->abort = false;
    ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
        while (!impl->abort) {
            co_await impl->sleep.sleepFor(ll::chrono::ticks{1});
            if (impl->abort) {
                break;
            }
            impl->dispatcher->tick();
        }
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

MinerLauncher::~MinerLauncher() {
    impl->abort = true;
    impl->sleep.interrupt(true);
    ll::event::EventBus::getInstance().removeListener(impl->playerDestroyBlockListener);
    ll::event::EventBus::getInstance().removeListener(impl->playerDisconnectListener);
}

void MinerLauncher::onPlayerDestroyBlock(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& rawPos = ev.pos();
    auto& player = ev.self();
    auto  dimId  = player.getDimensionId();

    // 对于本地主机玩家，忽略本地事件，因为本地事件的 BlockSource 没有权限破坏方块
    // TODO: 对于本地联机、加入服务器，需要特殊处理，向服务端请求
    if (player.isClientSide()) {
        FM_TRACE("Client side player destroy block event ignored");
        return;
    }

    auto hashedPos = miner_util::hashDimensionPosition(rawPos, dimId);
    if (ev.isCancelled() || impl->dispatcher->isProcessing(hashedPos)) {
        FM_TRACE("event cancelled or processing");
        return; // 已处理 / 正在处理
    }
    if (!impl->dispatcher->canLaunchTask(player)) {
        FM_TRACE("The player has unfinished tasks.");
        return;
    }

    auto& blockSource = player.getDimensionBlockSource();
    auto& block       = blockSource.getBlock(rawPos);
    auto& blockType   = block.getTypeName();
    if (!isMinerEnabled(player, blockType)) {
        FM_TRACE("isMinerEnabled return false");
        return; // 玩家未启用该方块类型 / 未开启连锁
    }
    if (!canDestroyBlockWithMcApi(player, block)) {
        FM_TRACE("player can not destroy block with mc api");
        return; // 玩家无法破坏该方块
    }

    auto rtConfig = this->loadRuntimeSingleBlockConfig(blockType);
    if (!rtConfig) [[unlikely]] {
        FM_TRACE("block type not found in rtConfig");
        return; // 配置文件中没有该方块类型
    }
    if (!canDestroyBlockWithConfig(player, rtConfig)) {
        FM_TRACE("player can not destroy block with config");
        return; // 玩家无法破坏该方块
    }

    MinerTaskContext ctx{
        .player      = player,
        .blockId     = block.getBlockItemId(),
        .tiggerPos   = rawPos,
        .tiggerDimid = dimId,
        .hashedPos   = hashedPos,
        .blockSource = blockSource,
        .rtConfig    = std::move(rtConfig)
    };
    ll::coro::keepThis([this, ctx = std::move(ctx)]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks{1};
        this->prepareAndLaunchTask(std::move(ctx));
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

bool MinerLauncher::canDestroyBlockWithMcApi(Player& player, Block const& block) const {
    // TODO: use BlockSource::checkBlockDestroyPermissions ?
    return player.canDestroyBlock(block) || mc_utils::CanDestroyBlock(player.getSelectedItem(), block)
        || mc_utils::CanDestroySpecial(player.getSelectedItem(), block);
}

RuntimeSingleBlockConfigPtr MinerLauncher::loadRuntimeSingleBlockConfig(std::string const& blockType) {
    return StaticGlobalConfigHost::getRuntimeSingleBlockConfig(blockType);
}

void MinerLauncher::prepareAndLaunchTask(MinerTaskContext ctx) {
    auto& block = ctx.blockSource.getBlock(ctx.tiggerPos);
    if (!block.isAir()) {
        FM_TRACE("block is not air");
        return; // 方块不是空气代表着玩家没有破坏它
    }

    int limit = calculateLimit(ctx);
    if (limit <= 1) {
        FM_TRACE("limit <= 1");
        return; // 限制为1或以下则不进行连锁
    }
    ctx.limit = limit;

    FM_TRACE("limit: " << limit);
    FM_TRACE("Task preparation complete");

    auto task = std::make_shared<MinerTask>(std::move(ctx), *impl->dispatcher, getNotifyFinishedHook(ctx));
    impl->dispatcher->launch(task);
}

MinerTask::NotifyFinishedHook MinerLauncher::getNotifyFinishedHook(MinerTaskContext const& ctx) { return nullptr; }

int MinerLauncher::calculateLimit(MinerTaskContext const& ctx) { return calculateDurabilityLimit(ctx); }

int MinerLauncher::calculateDurabilityLimit(MinerTaskContext const& ctx) const {
    constexpr int UNLIMITED  = std::numeric_limits<int>::max();
    constexpr int SAFETY_CAP = 1024;

    int configLimit = ctx.rtConfig->limit.value_or(UNLIMITED);

    int   toolLimit = UNLIMITED;
    auto& itemStack = ctx.player.getSelectedItem();

    // 物品会损耗，且没有不可破坏属性时 => 计算耐久
    if (itemStack.isDamageableItem() && !miner_util::hasUnbreakable(itemStack)) {
        if (auto item = itemStack.getItem()) {
            // 保留 1 点耐久不爆
            int remaining = item->getMaxDamage() - itemStack.getDamageValue() - 1;
            toolLimit     = std::max(0, remaining);
        }
    }
    int finalLimit = std::min(configLimit, toolLimit);
    if (finalLimit == UNLIMITED) {
        return SAFETY_CAP; // 防止连锁爆炸
    }
    return finalLimit;
}


} // namespace fm