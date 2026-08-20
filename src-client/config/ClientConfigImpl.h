#pragma once
#include "config/StaticGlobalConfigHost.h"

namespace fm::client {

class ClientConfigImpl final : public StaticGlobalConfigHost {
public:
    ll::Expected<> load(const std::filesystem::path& baseDir) override;

    ll::Expected<> save(const std::filesystem::path& baseDir) override;

    void buildDefault() override;

    void buildRuntimeMap() override;

    RuntimeSingleBlockConfigPtr buildRuntimeSingleBlockConfig(SingleBlockConfig single) override;

public:
    /* GUI */
    void addSimilarBlock(std::string const& blockType, std::string const& similarBlockType);
    void removeSimilarBlock(std::string const& blockType, std::string const& similarBlockType);
    void updateBlockConfig(std::string const& oldType, std::string const& newType, BlockOverride config);
    void addBlockConfig(std::string const& blockType, BlockOverride config);
    void removeBlockConfig(std::string const& blockType);
};

} // namespace fm::client