#pragma once
#include "Type.h"

#include "../config/ServerConfigImpl.h"

#include <memory>

namespace fm {

template <>
struct internal::ImplType<tag::ConfigImplTag> {
    using type = server::ServerConfigImpl;
};

} // namespace fm