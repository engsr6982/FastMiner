#pragma once
#include "Type.h"

#include "../config/ClientConfigModel.h"

namespace fm {


template <>
struct internal::ImplType<tag::ConfigModelTag> {
    using type = client::ClientConfigModel;
};

template <>
struct internal::ImplType<tag::SingleBlockConfigTag> {
    using type = client::BlockOverride;
};


} // namespace fm