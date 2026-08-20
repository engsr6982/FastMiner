#pragma once
#include "Global.h"
#include "MinerTask.h"
#include "core/MinerTaskContext.h"

#include "ll/api/event/player/PlayerDestroyBlockEvent.h"

#include <memory>

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

    int calculateDurabilityLimit(MinerTaskContext const& ctx) const;
};


} // namespace fm