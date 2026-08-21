#pragma once
#include "core/DispatcherConfig.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fm::client {

struct BlockOverride {
    std::string                     name;
    std::optional<int>              limit{std::nullopt};
    DestroyMode                     destroyMode{DestroyMode::Default};
    std::unordered_set<std::string> similarBlock{};
};

using BlockOverrides = std::unordered_map<std::string, BlockOverride>;

using BlockDefault = BlockOverride;

struct ClientConfigModel {
    static int constexpr SchemaVersion = 3;

    int version = SchemaVersion;

    DispatcherConfig dispatcher;

    bool telemetry{true};

    int bindKey{86}; // Windows VK Codes

    BlockDefault   blockDefault;
    BlockOverrides overrides; // override default block settings
};

} // namespace fm::client