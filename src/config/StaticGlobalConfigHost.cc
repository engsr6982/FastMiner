#include "StaticGlobalConfigHost.h"
#include "FastMiner.h"
#include "Type.h"

#include <mc/world/level/block/Block.h>
#include <mc/world/level/block/registry/BlockTypeRegistry.h>

#include <memory>

#include "trait/ConfigImplTrait.h"

namespace fm {

BlockID StaticGlobalConfigHost::getBlockIdCached(std::string const& blockType) {
    auto iter = blockIDCacheMap.find(blockType);
    if (iter == blockIDCacheMap.end()) {
        auto blockId = BlockTypeRegistry::get().getDefaultBlockState(blockType.c_str()).getBlockItemId();

        iter = blockIDCacheMap.emplace(blockType, blockId).first;
    }
    return iter->second;
}

RuntimeSingleBlockConfigPtr StaticGlobalConfigHost::getRuntimeSingleBlockConfig(BlockID blockId) {
    auto iter = runtimeConfigMap.find(blockId);
    if (iter == runtimeConfigMap.end()) {
        return nullptr;
    }
    return iter->second;
}

RuntimeSingleBlockConfigPtr StaticGlobalConfigHost::getRuntimeSingleBlockConfig(std::string const& blockType) {
    return getRuntimeSingleBlockConfig(getBlockIdCached(blockType));
}


StaticGlobalConfigHost& StaticGlobalConfigHost::getInstance() {
    using Impl = internal::ImplType<tag::ConfigImplTag>::type;

    static auto instance = std::make_unique<Impl>();
    return *instance;
}

ll::Expected<> StaticGlobalConfigHost::load() { return this->load(FastMiner::getInstance().getSelf().getConfigDir()); }
ll::Expected<> StaticGlobalConfigHost::save() { return this->save(FastMiner::getInstance().getSelf().getConfigDir()); }

} // namespace fm