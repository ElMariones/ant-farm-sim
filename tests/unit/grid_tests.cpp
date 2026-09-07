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


TEST_CASE("terrain revision changes on any material edit", "[grid]") {
  // The renderer caches baked terrain against this value, so it has to move even when passability
  // does not — otherwise a dug cell would keep drawing as solid ground.
  ant::sim::Grid grid(ant::sim::Material::Soil);
  const std::uint64_t initial = grid.terrain_revision();

  grid.set({4, 4}, ant::sim::Material::Clay);
  const std::uint64_t after_clay = grid.terrain_revision();
  CHECK(after_clay != initial);
  // Clay and soil are both impassable, so navigation is unchanged while terrain is not.
  CHECK(grid.navigation_revision() == 1);

  grid.set({4, 4}, ant::sim::Material::Clay);
  CHECK(grid.terrain_revision() == after_clay);

  grid.set({4, 4}, ant::sim::Material::Air);
  CHECK(grid.terrain_revision() != after_clay);
  CHECK(grid.navigation_revision() == 2);

  // An edit in a different chunk also moves it.
  const std::uint64_t before_far = grid.terrain_revision();
  grid.set({200, 100}, ant::sim::Material::Air);
  CHECK(grid.terrain_revision() != before_far);
}
