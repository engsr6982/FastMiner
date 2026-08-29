#pragma once
#include "Global.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include <memory>

namespace fm {

class TaskDispatcher;

namespace client {

/**
 * @brief 批量右键使用任务启动器（客户端专属）
 *
 * 监听 PlayerInteractBlockEvent（服务端线程，本地/LAN 主机权威路径），
 * 仅在连锁键（v）按住且命中 useItems 白名单时创建 UseTask，交由与连锁挖掘共享的
 * TaskDispatcher 同-配额池调度。
 *
 * 由于 Win10 客户端单次右键会在服务端连续激发多次该事件，启动器用 pendingPlayers
 * 对同一玩家做去重，避免同一右键产生多个 UseTask。
 *
 * 生命周期：由 ClientMinerLauncher 持有，析构早于调度器关停
 * （先移除监听 + 停止延时启动，再轮到 MinerLauncher 析构关停调度器）。
 */
class UseLauncher {
    struct Impl;
    std::unique_ptr<Impl> impl;

    void onInteract(ll::event::PlayerInteractBlockEvent& ev, std::shared_ptr<TaskDispatcher> const& dispatcher);

public:
    FM_DISABLE_COPY_MOVE(UseLauncher);
    explicit UseLauncher(std::shared_ptr<TaskDispatcher> dispatcher);
    ~UseLauncher();
};

} // namespace client
} // namespace fm