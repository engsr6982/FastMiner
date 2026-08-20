#pragma once
#include "Type.h"

#include "../config/ClientConfigImpl.h"

namespace fm {

template <>
struct internal::ImplType<tag::ConfigImplTag> {
    using type = client::ClientConfigImpl;
};

} // namespace fm