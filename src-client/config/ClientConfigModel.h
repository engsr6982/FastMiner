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

// 连锁范围透视描边配置
struct OutlineConfig {
    bool enabled{true};      // 描边总开关
    int  maxBlocks{1024};    // 描边数量上限（防止超长集合拖渲染帧）
    int  searchPerTick{256}; // 预搜索每 tick 配额（调度预算，不卡线程）
};

struct ClientConfigModel {
    static int constexpr SchemaVersion = 4;

    int version = SchemaVersion;

    DispatcherConfig dispatcher;

    bool telemetry{true};

    int bindKey{86}; // Windows VK Codes

    OutlineConfig outline;

    BlockDefault   blockDefault;
    BlockOverrides overrides; // override default block settings
};

} // namespace fm::client