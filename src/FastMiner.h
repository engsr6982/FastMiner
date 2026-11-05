#pragma once
#include "ll/api/mod/NativeMod.h"

#include <memory>

#include "Type.h"

#include "trait/PlatformServiceTrait.h"

namespace fm {

class MinerLauncher;

class FastMiner {
public:
    static FastMiner& getInstance();

    FastMiner();

    [[nodiscard]] ll::mod::NativeMod& getSelf();

    bool load();

    bool enable();

    bool disable();

    bool unload();

    using PlatformServiceImpl = internal::ImplType<tag::PlatformServiceTag>::type;
    PlatformServiceImpl& getPlatformService() const;

    MinerLauncher* getMinerLauncher() const;

    static_assert(std::is_final_v<PlatformServiceImpl>, "PlatformServiceImpl must be final");

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace fm
