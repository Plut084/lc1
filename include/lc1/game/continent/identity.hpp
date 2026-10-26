#pragma once

#include <cstdint>
#include <string>

namespace lc1 {

// Content identity and revision, independent of the checkpoint format version.
struct ContinentIdentity {
    std::string id;
    std::uint32_t version;
    constexpr auto operator<=>(ContinentIdentity const &) const = default;
};

} // namespace lc1
