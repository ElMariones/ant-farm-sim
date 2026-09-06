#include "sim/navigation.hpp"
#include "sim/terrain_generation.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("starter home and both food sources are connected across one hundred seeds",
          "[terrain][navigation]") {
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    const auto terrain = ant::sim::generate_terrain(seed);
    CAPTURE(seed);
    CHECK(terrain.grid.passable(terrain.home));
    CHECK(ant::sim::is_connected(terrain.grid, terrain.home, terrain.entrance));
    CHECK(ant::sim::is_connected(terrain.grid, terrain.home, terrain.source_positions[0]));
    CHECK(ant::sim::is_connected(terrain.grid, terrain.home, terrain.source_positions[1]));
  }
}

TEST_CASE("A star obeys its expansion budget and never crosses solid cells", "[navigation]") {
  const auto terrain = ant::sim::generate_terrain(7);
  const auto constrained =
      ant::sim::find_path(terrain.grid, terrain.home, terrain.source_positions[0], 1);
  CHECK(constrained.status == ant::sim::PathStatus::BudgetExhausted);
  CHECK(constrained.expanded == 1);

  const auto complete =
      ant::sim::find_path(terrain.grid, terrain.home, terrain.source_positions[0]);
  REQUIRE(complete.status == ant::sim::PathStatus::Complete);
  for (const ant::sim::GridPos cell : complete.cells) {
    CHECK(terrain.grid.passable(cell));
  }
}

TEST_CASE("home field follows topology revisions", "[navigation]") {
  auto terrain = ant::sim::generate_terrain(11);
  ant::sim::HomeField field;
  field.rebuild(terrain.grid, terrain.home);
  REQUIRE_FALSE(field.path_home(terrain.grid, terrain.source_positions[0]).empty());
  const auto old_revision = field.revision();
  terrain.grid.set({terrain.entrance.x, 40}, ant::sim::Material::Soil);
  CHECK(terrain.grid.navigation_revision() > old_revision);
  CHECK(field.path_home(terrain.grid, terrain.source_positions[0]).empty());
}
