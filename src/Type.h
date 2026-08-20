#pragma once


namespace fm {

using BlockID = short;

namespace tag {

struct CommandTag;

struct ConfigModelTag;
struct SingleBlockConfigTag;
struct ConfigImplTag;

struct MinerLauncherTag;

struct PlatformServiceTag;

} // namespace tag

namespace internal {

template <typename T>
struct ImplType;

}

} // namespace fm