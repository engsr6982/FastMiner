#pragma once
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"

#include <cstddef>
#include <cstdint>
#include <string_view>


namespace fm {


using HashedDimPos = size_t; // Hashed dimension position

namespace miner_util {

template <typename T>
    requires requires(T const& t) {
        { t.x } -> std::convertible_to<int>;
        { t.y } -> std::convertible_to<int>;
        { t.z } -> std::convertible_to<int>;
    }
inline constexpr HashedDimPos hashDimensionPosition(T const& pos, int dim) {
    static_assert(std::is_same_v<HashedDimPos, uint64_t>);
    static_assert(sizeof(uint64_t) == 8); // 64-bit

    static constexpr size_t prime1 = 73856093;
    static constexpr size_t prime2 = 19349663;
    static constexpr size_t prime3 = 83492791;

    size_t const h = (static_cast<size_t>(static_cast<uint32_t>(pos.x)) * prime1)
                   ^ (static_cast<size_t>(static_cast<uint32_t>(pos.y)) * prime2)
                   ^ (static_cast<size_t>(static_cast<uint32_t>(pos.z)) * prime3)
                   ^ (static_cast<size_t>(static_cast<uint32_t>(dim)) << 16);

    // splitmix64 finalizer：让相邻坐标差异扩散到全部位。
    size_t h2  = h;
    h2        ^= h2 >> 33;
    h2        *= 0xff51afd7ed558ccdull;
    h2        ^= h2 >> 33;
    h2        *= 0xc4ceb9fe1a85ec53ull;
    h2        ^= h2 >> 33;
    return static_cast<HashedDimPos>(h2);
}

inline bool hasUnbreakable(ItemStack const& item) {
    static constexpr std::string_view unbreakable = "Unbreakable";

    auto& nbt = item.mUserData;
    if (!nbt) {
        return false;
    }
    if (nbt->contains(unbreakable)) {
        return (*nbt)[unbreakable].get<ByteTag>();
    }
    return false;
}

} // namespace miner_util


} // namespace fm