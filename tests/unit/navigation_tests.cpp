#include "sim/navigation.hpp"
#include "sim/terrain_generation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

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

TEST_CASE("a wide gallery carries several legal routes, one per ant", "[navigation][routes]") {
  // An open hall with the nest at one corner: every monotone staircase across it is equally short.
  ant::sim::Grid grid(ant::sim::Material::Soil);
  const ant::sim::GridPos home{20, 60};
  for (int y = 55; y <= 65; ++y) {
    for (int x = 20; x <= 40; ++x) grid.set({x, y}, ant::sim::Material::Air);
  }
  ant::sim::HomeField field;
  field.rebuild(grid, home);
  const ant::sim::GridPos far{40, 55};

  std::set<std::vector<ant::sim::GridPos>> home_routes;
  std::set<std::vector<ant::sim::GridPos>> outward_routes;
  for (std::uint64_t id = 1; id <= 32; ++id) {
    const auto bias = ant::sim::route_bias(2026, id);
    const auto home_route = field.path_home(grid, far, bias);
    REQUIRE_FALSE(home_route.empty());
    CHECK(home_route.back() == home);
    // Variation only ever picks between equally short choices: no ant takes a detour.
    CHECK(home_route.size() == static_cast<std::size_t>(field.distance(far)));
    home_routes.insert(home_route);

    const auto outward = ant::sim::find_path(grid, home, far, 4096, bias);
    REQUIRE(outward.status == ant::sim::PathStatus::Complete);
    CHECK(outward.cells.size() == static_cast<std::size_t>(field.distance(far)));
    outward_routes.insert(outward.cells);
  }
  CHECK(home_routes.size() > 1);
  CHECK(outward_routes.size() > 1);

  // The same ant on the same colony always walks the same way.
  const auto repeat = ant::sim::route_bias(2026, 5);
  CHECK(field.path_home(grid, far, repeat) == field.path_home(grid, far, repeat));
  CHECK(ant::sim::find_path(grid, home, far, 4096, repeat).cells ==
        ant::sim::find_path(grid, home, far, 4096, repeat).cells);
  // A different colony seed gives the same ant id a different preference.
  CHECK(ant::sim::route_bias(2026, 5).key != ant::sim::route_bias(7, 5).key);
}

TEST_CASE("a one-cell passage still carries every ant", "[navigation][routes]") {
  // Two rooms joined by a single corridor: there is exactly one legal route and no ant may miss it.
  ant::sim::Grid grid(ant::sim::Material::Soil);
  const ant::sim::GridPos home{20, 60};
  for (int x = 20; x <= 24; ++x) grid.set({x, 60}, ant::sim::Material::Air);
  for (int x = 25; x <= 30; ++x) grid.set({x, 60}, ant::sim::Material::Air);
  for (int x = 31; x <= 35; ++x) grid.set({x, 60}, ant::sim::Material::Air);
  ant::sim::HomeField field;
  field.rebuild(grid, home);
  const ant::sim::GridPos far{35, 60};

  for (std::uint64_t id = 1; id <= 32; ++id) {
    const auto bias = ant::sim::route_bias(11, id);
    const auto route = field.path_home(grid, far, bias);
    REQUIRE_FALSE(route.empty());
    CHECK(route.size() == static_cast<std::size_t>(field.distance(far)));
    CHECK(route.back() == home);
    CHECK(ant::sim::find_path(grid, home, far, 4096, bias).status ==
          ant::sim::PathStatus::Complete);
  }
}

TEST_CASE("an unbiased route keeps the canonical order fixtures rely on", "[navigation][routes]") {
  ant::sim::Grid grid(ant::sim::Material::Soil);
  const ant::sim::GridPos home{20, 60};
  for (int y = 58; y <= 62; ++y) {
    for (int x = 20; x <= 30; ++x) grid.set({x, y}, ant::sim::Material::Air);
  }
  ant::sim::HomeField field;
  field.rebuild(grid, home);
  const auto plain = field.path_home(grid, {30, 58});
  CHECK(plain == field.path_home(grid, {30, 58}, ant::sim::RouteBias{}));
  CHECK(ant::sim::find_path(grid, home, {30, 58}).cells ==
        ant::sim::find_path(grid, home, {30, 58}, 4096, ant::sim::RouteBias{}).cells);
}

TEST_CASE("reused pathfinding scratch preserves routes across unrelated searches", "[navigation][performance]") {
  auto first = ant::sim::generate_terrain(42);
  auto second = ant::sim::generate_terrain(7);
  ant::sim::Pathfinder reused;
  for (std::uint64_t actor = 1; actor <= 32; ++actor) {
    const auto bias = ant::sim::route_bias(42, actor);
    for (const std::size_t budget : {std::size_t{0}, std::size_t{1}, std::size_t{4096}}) {
      const auto expected = ant::sim::find_path(first.grid, first.home, first.source_positions[0], budget, bias);
      const auto actual = reused.find(first.grid, first.home, first.source_positions[0], budget, bias);
      CHECK(actual.status == expected.status);
      CHECK(actual.expanded == expected.expanded);
      CHECK(actual.cells == expected.cells);
      static_cast<void>(reused.find(second.grid, second.source_positions[1], second.home, 10, bias));
    }
  }
  // Early-return queries and a newly blocked cell cannot leave a usable old predecessor chain.
  CHECK(reused.find(first.grid, first.home, first.home).cells.empty());
  first.grid.set(first.source_positions[0], ant::sim::Material::Stone);
  CHECK(reused.find(first.grid, first.home, first.source_positions[0]).status == ant::sim::PathStatus::Unreachable);
  const auto actual = reused.find(second.grid, second.home, second.source_positions[0]);
  CHECK(actual.cells == ant::sim::find_path(second.grid, second.home, second.source_positions[0]).cells);
}
