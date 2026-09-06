#include "sim/grid.hpp"
#include "sim/terrain_generation.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("grid rejects out-of-bounds access and revises changed chunks", "[grid]") {
  ant::sim::Grid grid;
  CHECK_FALSE(grid.in_bounds({-1, 0}));
  CHECK_FALSE(grid.in_bounds({ant::sim::Grid::kWidth, 0}));
  CHECK_FALSE(grid.in_bounds({0, ant::sim::Grid::kHeight}));
  CHECK_THROWS_AS(grid.at({-1, 0}), std::out_of_range);

  const auto navigation_before = grid.navigation_revision();
  const auto chunk_before = grid.chunk_revision(0, 0);
  grid.set({4, 4}, ant::sim::Material::Air);
  CHECK(grid.navigation_revision() == navigation_before + 1);
  CHECK(grid.chunk_revision(0, 0) == chunk_before + 1);
  CHECK(grid.passable({4, 4}));
}

TEST_CASE("terrain material layout is deterministic by seed", "[terrain]") {
  const auto first = ant::sim::generate_terrain(42);
  const auto second = ant::sim::generate_terrain(42);
  const auto other = ant::sim::generate_terrain(43);

  CHECK(first.grid.material_hash() == second.grid.material_hash());
  CHECK(first.source_positions == second.source_positions);
  CHECK(first.grid.material_hash() != other.grid.material_hash());
}
