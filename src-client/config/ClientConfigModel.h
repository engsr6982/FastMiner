#pragma once
#include "core/DispatcherConfig.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fm::client {

struct BlockOverride {
    std::string                     name;
    std::optional<int>              limit{std::nullopt};
    DestroyMode                     destroyMode{DestroyMode::Default};
    std::unordered_set<std::string> similarBlock{};
};

using BlockOverrides = std::unordered_map<std::string, BlockOverride>;

using BlockDefault = BlockOverride;

struct OutlineConfig {
    bool enabled{true};
    int  maxBlocks{1024};    // 防止超长集合拖渲染帧
    int  searchPerTick{256}; // 预搜索每 tick 预算
};

struct ClientConfigModel {
    static int constexpr SchemaVersion = 5;

    int version = SchemaVersion;

    DispatcherConfig dispatcher;

    bool telemetry{true};

    int bindKey{86}; // Windows 虚拟键码

    OutlineConfig outline;

    BlockDefault   blockDefault;
    BlockOverrides overrides;

    // UseTask 仅对白名单内物品生效；BFS 匹配触发方块类型后逐个走原版 useItemOn。
    std::vector<std::string> useItems{};
};

} // namespace fm::client