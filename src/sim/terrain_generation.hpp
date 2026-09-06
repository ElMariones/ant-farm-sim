#pragma once

#include "sim/grid.hpp"

#include <array>
#include <cstdint>

namespace ant::sim {

struct GeneratedTerrain {
  Grid grid;
  GridPos home;
  GridPos entrance;
  std::array<GridPos, 2> source_positions;
};

[[nodiscard]] GeneratedTerrain generate_terrain(std::uint64_t seed);

} // namespace ant::sim
