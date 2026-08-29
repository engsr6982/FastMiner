#include "use/UseLauncher.h"

#include "ClientPlatformService.h"
#include "FastMiner.h"
#include "core/MinerUtil.h"
#include "core/TaskDispatcher.h"
#include "use/UseTask.h"

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include <absl/container/flat_hash_set.h>
#include <atomic>
#include <memory>
#include <utility>

namespace fm::client {

struct UseLauncher::Impl {
    std::shared_ptr<TaskDispatcher> dispatcher;

    struct Loop {
        std::atomic<bool>              stopper{false}; // 进入关停流程后不再启动新任务
        absl::flat_hash_set<mce::UUID> pendingPlayers; // 已调度但尚未启动的玩家，防止 Win10 客户端单次右键连发多个事件
    };
    std::shared_ptr<Loop>  loop{std::make_shared<Loop>()};
    ll::event::ListenerPtr interactListener{nullptr};
};

UseLauncher::UseLauncher(std::shared_ptr<TaskDispatcher> dispatcher) : impl(std::make_unique<Impl>()) {
    impl->dispatcher = std::move(dispatcher);

    auto& bus = ll::event::EventBus::getInstance();

    impl->interactListener = bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
        [this, loop = impl->loop, dispatcher = impl->dispatcher](auto& ev) {
            if (loop->stopper) {
                return; // 已进入关停流程，不再批量
            }
            onInteract(ev, dispatcher);
        },
        ll::event::EventPriority::Low
    );
}
UseLauncher::~UseLauncher() {
    impl->loop->stopper = true;
    ll::event::EventBus::getInstance().removeListener(impl->interactListener);
}


void UseLauncher::onInteract(
    ll::event::PlayerInteractBlockEvent&   ev,
    std::shared_ptr<TaskDispatcher> const& dispatcher
) {
    auto& player = ev.self();

    if (player.isClientSide()) {
        return; // 忽略本地主机预测路径事件
    }
    if (ev.isCancelled()) {
        return; // 其它模组已取消交互则不再批量
    }

    auto const& platform = FastMiner::getInstance().getPlatformService().as<ClientPlatformService>();
    if (!platform.isKeyActivated()) {
        return; // 未按下快捷键
    }

    auto& item = ev.item();
    if (!UseTask::isWhitelisted(item)) {
        return; // 非白名单物品
    }

    auto const rawPos    = ev.blockPos();
    int const  dimId     = player.getDimensionId();
    auto const hashedPos = miner_util::hashDimensionPosition(rawPos, dimId);
    if (dispatcher->isProcessing(hashedPos)) {
        return; // 该坐标正在被批处理（重入短路）
    }
    if (!dispatcher->canLaunchTask(player)) {
        return; // 玩家已有任务在途（挖掘 / 使用互斥）
    }

    // 触发方块当前类型作为批量 BFS 的目标类型快照；空气/无内容不触发
    auto&       blockSource = player.getDimensionBlockSource();
    auto const& anchorBlock = blockSource.getBlock(rawPos);
    if (anchorBlock.isAir()) {
        return;
    }

    int const limit = UseTask::computeLimit(player);
    if (limit <= 1) {
        return; // 与连锁挖掘一致：1次性上限<=1 不批量
    }

    // Win10 客户端单次右键会在服务端连续激发多次 PlayerInteractBlockEvent，
    // 用 pendingPlayers 去重：同一玩家已安排尚未启动的延迟任务不再重复调度
    auto const uuid = player.getUuid();
    auto       loop = impl->loop;
    if (!loop->pendingPlayers.insert(uuid).second) {
        return;
    }

    auto uctx = UseTaskContext{
        player,
        blockSource,
        rawPos,
        dimId,
        limit,
        static_cast<BlockID>(anchorBlock.getBlockItemId()),
        ev.face()
    };

    // 1 tick 延后启动（镜像连锁挖掘节奏，避免在事件帧内立刻排队）
    ll::coro::keepThis([loop, dispatcher, ctx = std::move(uctx), uuid]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks{1};
        if (loop->stopper) {
            loop->pendingPlayers.erase(uuid);
            co_return; // 已关停则不再启动延时任务
        }
        if (!dispatcher->canLaunchTask(ctx.player)) {
            loop->pendingPlayers.erase(uuid);
            co_return; // 期间玩家已开启新任务（如连锁挖掘），放弃本次使用批量
        }
        dispatcher->launch(std::make_shared<UseTask>(std::move(ctx), *dispatcher));
        loop->pendingPlayers.erase(uuid);
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

} // namespace fm::client