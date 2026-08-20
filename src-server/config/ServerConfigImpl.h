#pragma once
#include "IServerPlayerConfig.h"
#include "config/StaticGlobalConfigHost.h"

namespace fm {
namespace server {

class ServerConfigImpl final : public StaticGlobalConfigHost, public IServerPlayerConfig {
public:
    ll::Expected<> load(const std::filesystem::path& baseDir) override;

    ll::Expected<> save(const std::filesystem::path& baseDir) override;

    void buildDefault() override;

    void buildRuntimeMap() override;

    RuntimeSingleBlockConfigPtr buildRuntimeSingleBlockConfig(SingleBlockConfig single) override;


public: /* GUI */
    void addTool(std::string const& blockType, std::string const& toolType);
    void removeTool(std::string const& blockType, std::string const& toolType);

    void addSimilarBlock(std::string const& blockType, std::string const& similarBlockType);
    void removeSimilarBlock(std::string const& blockType, std::string const& similarBlockType);

    void updateBlockConfig(std::string const& oldType, std::string const& newType, BlockConfig config);
    void addBlockConfig(std::string const& blockType, BlockConfig config);
    void removeBlockConfig(std::string const& blockType);

public:
    /* Player */
    void loadPlayerConfig();
    void savePlayerConfig();

    bool isEnabled(const mce::UUID& uuid, const std::string& key) override;
    void setEnabled(const mce::UUID& uuid, const std::string& key, bool enabled) override;
    void enable(const mce::UUID& uuid, const std::string& key) override;
    void disable(const mce::UUID& uuid, const std::string& key) override;
    bool hasPlayer(const mce::UUID& uuid) override;
    bool hasBlock(const mce::UUID& uuid, const std::string& key) override;
    void removeBlock(const mce::UUID& uuid, const std::string& key) override;
    void ensurePlayerBlockConfig() override;

private:
    PlayerBlockState playerBlockState_;
};

} // namespace server
} // namespace fm
