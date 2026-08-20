#pragma once
#include "Type.h"

#include "../config/ServerConfigModel.h"


namespace fm {

template <>
struct internal::ImplType<tag::ConfigModelTag> {
    using type = server::ServerConfigModel;
};

template <>
struct internal::ImplType<tag::SingleBlockConfigTag> {
    using type = server::BlockConfig;
};

} // namespace fm