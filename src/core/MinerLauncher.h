#pragma once
#include "Global.h"
#include "MinerTask.h"
#include "core/MinerTaskContext.h"

#include "ll/api/event/player/PlayerDestroyBlockEvent.h"

#include <memory>
#include <optional>

class Block;
class Player;

namespace fm {


class MinerLauncher {
    struct Impl;
    std::unique_ptr<Impl> impl;

    void onPlayerDestroyBlock(ll::event::PlayerDestroyBlockEvent& ev);
    bool canDestroyBlockWithMcApi(Player& player, Block const& block) const;
    void prepareAndLaunchTask(MinerTaskContext ctx);

public:
    FM_DISABLE_COPY_MOVE(MinerLauncher);
    explicit MinerLauncher();
    virtual ~MinerLauncher();

    virtual bool isMinerEnabled(Player& player, std::string const& blockType) = 0;

    virtual bool canDestroyBlockWithConfig(Player& player, RuntimeSingleBlockConfigPtr const& rtConfig) = 0;

    virtual RuntimeSingleBlockConfigPtr loadRuntimeSingleBlockConfig(std::string const& blockType);

    virtual MinerTask::NotifyFinishedHook getNotifyFinishedHook(MinerTaskContext const& ctx);

    virtual int calculateLimit(MinerTaskContext const& ctx);

    /**
     * @brief 两阶段交接：挖掘前从客户端预搜索中取已确定的连锁集合。
     * 默认返回 nullopt（服务端无预搜索，挖掘后直接搜索提交，行为不变）；
     * 客户端覆写为当 ctx.tiggerPos 与预搜索锚点一致时返回集合。
     */
    virtual std::optional<MinerTask::PreSearchData> tryTakeClientPresearch(MinerTaskContext const& ctx);

    int calculateDurabilityLimit(MinerTaskContext const& ctx) const;
};


} // namespace fm