#pragma once

#include <cstdint>
#include <tuple>

namespace ant::sim {

using EntityId = std::uint64_t;
using Tick = std::uint64_t;

inline constexpr int kSubcellsPerCell = 256;
inline constexpr int kTicksPerSecond = 20;

struct GridPos {
  int x{};
  int y{};

  friend constexpr bool operator==(GridPos, GridPos) = default;
  friend constexpr auto operator<=>(GridPos lhs, GridPos rhs) {
    return std::tie(lhs.y, lhs.x) <=> std::tie(rhs.y, rhs.x);
  }
};

} // namespace ant::sim
