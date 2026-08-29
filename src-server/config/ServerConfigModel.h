#pragma once
#include "core/DispatcherConfig.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fm::server {

enum class SilkTouchMode : int {
    Unlimited = 0, // 无限制
    Forbid    = 1, // 禁止精准附魔
    Need      = 2  // 需要精准附魔
};

using MinerTools   = std::unordered_set<std::string>;
using SimilarBlock = std::unordered_set<std::string>;

struct BlockConfig {
    std::string   name;
    int           cost{0};
    int           limit{256};
    DestroyMode   destroyMode{DestroyMode::Default};
    SilkTouchMode silkTouchMode{SilkTouchMode::Unlimited};
    MinerTools    tools{};
    SimilarBlock  similarBlock{};
};

using Blocks = std::unordered_map<std::string, BlockConfig>;

struct ServerConfigModel {
    static constexpr int SchemaVersion = 8;

    int version = SchemaVersion;

    DispatcherConfig dispatcher;

    bool telemetry{true};

    struct EconomyConfig {
        enum class EconomyKit { LegacyMoney, ScoreBoard };

        bool        enabled        = false;
        EconomyKit  kit            = EconomyKit::LegacyMoney;
        std::string scoreboardName = "Scoreboard";
    } economy;

    Blocks blocks;
};

} // namespace fm::server
