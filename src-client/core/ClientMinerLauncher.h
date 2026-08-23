#pragma once
#include "core/MinerLauncher.h"

#include <optional>

namespace fm {
namespace client {

class ClientMinerLauncher final : public MinerLauncher {
public:
    bool isMinerEnabled(Player& player, const std::string& blockType) override;
    bool canDestroyBlockWithConfig(Player& player, const RuntimeSingleBlockConfigPtr& rtConfig) override;

    RuntimeSingleBlockConfigPtr loadRuntimeSingleBlockConfig(const std::string& blockType) override;

    /// 两阶段交接：挖下的方块 == 预搜索锚点时，把客户端预搜集合交给连锁任务（免重搜）
    std::optional<MinerTask::PreSearchData> tryTakeClientPresearch(MinerTaskContext const& ctx) override;
};

} // namespace client
} // namespace fm
