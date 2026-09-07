#pragma once

#include "sim/grid.hpp"
#include "sim/nest_plan.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::sim {

struct GeneratedTerrain {
  Grid grid;
  GridPos home;
  GridPos entrance;
  std::array<GridPos, 2> source_positions;
  // The rooms the founding queen's nest already has: a brood room below her and two granaries to
  // either side. The colony extends the same set rather than digging a different kind of space.
  std::vector<Room> rooms;
};

[[nodiscard]] GeneratedTerrain generate_terrain(std::uint64_t seed);

} // namespace ant::sim
