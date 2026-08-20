#pragma once
#include "Type.h"
#include "trait/ConfigTrait.h"

#include "ll/api/Expected.h"

#include "absl/container/flat_hash_set.h"
#include <absl/container/flat_hash_map.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace fm {

struct DispatcherConfig;

using ConfigModel       = internal::ImplType<tag::ConfigModelTag>::type;
using SingleBlockConfig = internal::ImplType<tag::SingleBlockConfigTag>::type;

// Performance Optimization
struct RuntimeSingleBlockConfig {
    SingleBlockConfig            rawConfig;
    std::optional<int>           limit{std::nullopt};
    absl::flat_hash_set<BlockID> similarBlock{};

    explicit RuntimeSingleBlockConfig(SingleBlockConfig config) : rawConfig(std::move(config)) {}
};
using RuntimeSingleBlockConfigPtr = std::shared_ptr<RuntimeSingleBlockConfig>;


struct StaticGlobalConfigHost {
    using BlockIDCacheMap             = absl::flat_hash_map<std::string, BlockID>;
    using RuntimeSingleBlockConfigMap = absl::flat_hash_map<BlockID, RuntimeSingleBlockConfigPtr>;

    static_assert(std::is_aggregate_v<ConfigModel>);
    static_assert(std::is_aggregate_v<SingleBlockConfig>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(ConfigModel::SchemaVersion)>, int>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<ConfigModel>().version)>, int>);

    inline static ConfigModel                 model{};
    inline static BlockIDCacheMap             blockIDCacheMap{};
    inline static RuntimeSingleBlockConfigMap runtimeConfigMap{};

    /// static api

    [[nodiscard]] static BlockID                     getBlockIdCached(std::string const& blockType);
    [[nodiscard]] static RuntimeSingleBlockConfigPtr getRuntimeSingleBlockConfig(BlockID blockId);
    [[nodiscard]] static RuntimeSingleBlockConfigPtr getRuntimeSingleBlockConfig(std::string const& blockType);

    /// inline api

    [[nodiscard]] inline static bool isTelemetryEnabled() {
        static_assert(std::same_as<std::remove_cvref_t<decltype(std::declval<ConfigModel>().telemetry)>, bool>);
        return model.telemetry;
    }

    [[nodiscard]] inline static DispatcherConfig const& getDispatcherConfig() {
        static_assert(
            std::same_as<std::remove_cvref_t<decltype(std::declval<ConfigModel>().dispatcher)>, DispatcherConfig>
        );
        return model.dispatcher;
    }

    /// cross platform api

    [[nodiscard]] static StaticGlobalConfigHost& getInstance();

    StaticGlobalConfigHost()          = default;
    virtual ~StaticGlobalConfigHost() = default;

    StaticGlobalConfigHost(StaticGlobalConfigHost const&)            = delete;
    StaticGlobalConfigHost& operator=(StaticGlobalConfigHost const&) = delete;
    StaticGlobalConfigHost(StaticGlobalConfigHost&&)                 = delete;
    StaticGlobalConfigHost& operator=(StaticGlobalConfigHost&&)      = delete;

    [[nodiscard]] virtual ll::Expected<> load();
    [[nodiscard]] virtual ll::Expected<> save();
    [[nodiscard]] virtual ll::Expected<> load(std::filesystem::path const& baseDir) = 0;
    [[nodiscard]] virtual ll::Expected<> save(std::filesystem::path const& baseDir) = 0;

    virtual void buildDefault() = 0;

    virtual void buildRuntimeMap() = 0;

    [[nodiscard]] virtual RuntimeSingleBlockConfigPtr buildRuntimeSingleBlockConfig(SingleBlockConfig single) = 0;

    template <std::derived_from<StaticGlobalConfigHost> T>
    [[nodiscard]] inline T& as() {
        return static_cast<T&>(*this);
    }
};


} // namespace fm