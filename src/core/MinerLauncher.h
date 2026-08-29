#pragma once
#include "Global.h"
#include "core/ChainTaskContext.h"
#include "core/TaskBase.h"

#include "ll/api/event/player/PlayerDestroyBlockEvent.h"

#include <memory>
#include <optional>

class Block;
class Player;

namespace fm {

class TaskDispatcher;

class MinerLauncher {
    struct Impl;
    std::unique_ptr<Impl> impl;

    void onPlayerDestroyBlock(ll::event::PlayerDestroyBlockEvent& ev, std::shared_ptr<TaskDispatcher> dispatcher);
    bool canDestroyBlockWithMcApi(Player& player, Block const& block) const;
    void prepareAndLaunchTask(ChainTaskContext ctx, TaskDispatcher& dispatcher);

public:
    FM_DISABLE_COPY_MOVE(MinerLauncher);
    explicit MinerLauncher();
    virtual ~MinerLauncher();

    [[nodiscard]] std::shared_ptr<TaskDispatcher> const& dispatcher() const noexcept;

    virtual bool isMinerEnabled(Player& player, std::string const& blockType) = 0;

    virtual bool canDestroyBlockWithConfig(Player& player, RuntimeSingleBlockConfigPtr const& rtConfig) = 0;

    virtual RuntimeSingleBlockConfigPtr loadRuntimeSingleBlockConfig(std::string const& blockType);

    virtual int calculateLimit(ChainTaskContext const& ctx);

    /**
     * @brief 挖掘前尝试接管客户端预搜索已确定的方块集合。
     * @return 默认返回 nullopt，服务端直接现场搜索；客户端在锚点匹配时返回预搜索集合。
     */
    virtual std::optional<PreSearchData> tryTakeClientPresearch(ChainTaskContext const& ctx);

protected:
    /**
     * @brief 平台子类创建并启动连锁挖掘任务。
     * @note 服务器实例化为 ChainTask<ServerChainFinisher>，客户端实例化为 ChainTask<>，避免编译期互相依赖。
     */
    virtual void
    launchChainTask(ChainTaskContext ctx, TaskDispatcher& dispatcher, std::optional<PreSearchData> preSearch) = 0;

    int calculateDurabilityLimit(ChainTaskContext const& ctx) const;
};


} // namespace fm