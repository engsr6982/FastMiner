#pragma once
#include "core/MinerLauncher.h"

#include <memory>
#include <optional>

namespace fm::client {


class ClientMinerLauncher final : public MinerLauncher {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    explicit ClientMinerLauncher();
    ~ClientMinerLauncher() override;

    bool isMinerEnabled(Player& player, const std::string& blockType) override;
    bool canDestroyBlockWithConfig(Player& player, const RuntimeSingleBlockConfigPtr& rtConfig) override;

    RuntimeSingleBlockConfigPtr loadRuntimeSingleBlockConfig(const std::string& blockType) override;

    /**
     * @brief 仅在挖下方块与预搜索锚点一致时交接集合，避免过期集合误交付。
     */
    std::optional<PreSearchData> tryTakeClientPresearch(ChainTaskContext const& ctx) override;

protected:
    void
    launchChainTask(ChainTaskContext ctx, TaskDispatcher& dispatcher, std::optional<PreSearchData> preSearch) override;
};

} // namespace fm::client