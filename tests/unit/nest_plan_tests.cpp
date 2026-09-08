#include "sim/nest_plan.hpp"
#include "sim/navigation.hpp"
#include "sim/world.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using ant::sim::Grid;
using ant::sim::GridPos;
using ant::sim::Material;
using ant::sim::NestPlan;
using ant::sim::Room;
using ant::sim::RoomKind;
using ant::sim::World;
using ant::sim::kRoomMaxRadius;
using ant::sim::kRoomStartRadius;
using ant::sim::within_envelope;

namespace {

constexpr GridPos kHome{192, 60};

Grid soil_with_air(const std::vector<GridPos>& air) {
  Grid grid(Material::Soil);
  for (const GridPos cell : air) grid.set(cell, Material::Air);
  return grid;
}

// The founding chamber the colony digs out from, so a plan has somewhere to start.
Grid nest_around_home() {
  Grid grid(Material::Soil);
  for (int y = kHome.y - 3; y <= kHome.y + 3; ++y) {
    for (int x = kHome.x - 3; x <= kHome.x + 3; ++x) grid.set({x, y}, Material::Air);
  }
  return grid;
}

} // namespace

TEST_CASE("a room is only planned where there is room for one", "[nest]") {
  NestPlan plan;
  Grid grid = nest_around_home();
  plan.update(grid, kHome, 42, true, false);
  REQUIRE(plan.rooms().size() == 1);
  const Room& room = plan.rooms().front();
  CHECK(room.kind == RoomKind::Nursery);
  CHECK(room.radius == kRoomStartRadius);
  CHECK_FALSE(room.complete);
  CHECK(within_envelope(room.centre, kHome));
  // Brood rooms stay close enough to the queen for a nurse to walk there.
  CHECK(std::abs(room.centre.x - kHome.x) + std::abs(room.centre.y - kHome.y) <=
        ant::sim::kNurseryReach);
}

TEST_CASE("one project runs at a time and the corridor is cut before the room", "[nest]") {
  NestPlan plan;
  Grid grid = nest_around_home();
  plan.update(grid, kHome, 42, true, true);
  REQUIRE(plan.rooms().size() == 1);

  const std::vector<GridPos> work = plan.work_cells(grid, kHome);
  REQUIRE_FALSE(work.empty());
  const Room& room = plan.rooms().front();
  // Everything the corridor needs comes before anything the room needs, so the passage arrives
  // first and the chamber opens at the end of it.
  bool seen_room_cell = false;
  for (const GridPos cell : work) {
    if (room.contains(cell)) seen_room_cell = true;
    else CHECK_FALSE(seen_room_cell);
  }
  CHECK(seen_room_cell);

  // A second call while a room is unfinished never starts another one.
  plan.update(grid, kHome, 42, true, true);
  CHECK(plan.rooms().size() == 1);
}

TEST_CASE("only an exposed face can be claimed, and never more than the crew", "[nest]") {
  NestPlan plan;
  Grid grid = nest_around_home();
  plan.update(grid, kHome, 42, true, false);

  int claims = 0;
  std::vector<GridPos> taken;
  while (const auto cell = plan.claim(grid, kHome)) {
    // A worker has to be able to stand beside the cell it is cutting.
    bool reachable = false;
    for (const GridPos step : {GridPos{0, -1}, GridPos{1, 0}, GridPos{0, 1}, GridPos{-1, 0}}) {
      reachable = reachable || grid.walkable({cell->x + step.x, cell->y + step.y});
    }
    CHECK(reachable);
    CHECK(is_diggable(grid.at(*cell)));
    CHECK(std::find(taken.begin(), taken.end(), *cell) == taken.end());
    taken.push_back(*cell);
    ++claims;
  }
  // How many faces are exposed at once depends on the shape of the ground; what must hold is that
  // the crew is never exceeded and no two workers are sent to the same cell.
  CHECK(claims > 0);
  CHECK(claims <= ant::sim::kDiggersPerProject);
  plan.clear_claims();
  CHECK(plan.claim(grid, kHome));
}

TEST_CASE("a full room is widened before another is sited", "[nest]") {
  NestPlan plan;
  Grid grid(Material::Air); // everything already open, so a room completes immediately
  plan.found({{kHome.x, kHome.y + 14}, kRoomStartRadius, RoomKind::Nursery, true});
  for (int radius = kRoomStartRadius; radius < kRoomMaxRadius; ++radius) {
    plan.update(grid, kHome, 42, true, false);
    CHECK(plan.rooms().size() == 1);
    CHECK(plan.rooms().front().radius == radius + 1);
  }

  // Widening stops at the cap, and only then does the colony cut a second brood room.
  plan.update(grid, kHome, 42, true, false);
  CHECK(plan.rooms().front().radius == kRoomMaxRadius);
  REQUIRE(plan.rooms().size() == 2);
  CHECK(plan.rooms().back().kind == RoomKind::Nursery);
}

TEST_CASE("a room is finished around rock it cannot cut", "[nest]") {
  NestPlan plan;
  Grid grid(Material::Air);
  const GridPos centre{kHome.x, kHome.y + 14};
  grid.set(centre, Material::Stone);
  plan.found({centre, kRoomStartRadius, RoomKind::Granary, false});
  plan.update(grid, kHome, 42, false, false);
  CHECK(plan.rooms().front().complete);
  CHECK(plan.idle());
}

TEST_CASE("a founding colony builds only what it needs and its nest stays connected", "[nest]") {
  World world(7);
  const Grid before = world.grid();
  const std::size_t founding_rooms = world.rooms().size();
  world.run_ticks(30'000);

  CHECK(world.rooms().size() >= founding_rooms);
  for (const Room& room : world.rooms()) {
    CHECK(within_envelope(room.centre, world.home()));
    CHECK(room.radius >= kRoomStartRadius);
    CHECK(room.radius <= kRoomMaxRadius);
  }
  // Rooms never overlap, so each one is a distinct chamber rather than one widening cavity.
  for (std::size_t a = 0; a < world.rooms().size(); ++a) {
    for (std::size_t b = a + 1; b < world.rooms().size(); ++b) {
      const Room& first = world.rooms()[a];
      const Room& second = world.rooms()[b];
      CHECK(std::abs(first.centre.x - second.centre.x) +
                std::abs(first.centre.y - second.centre.y) >
            first.radius + second.radius);
    }
  }

  const Grid& grid = world.grid();
  for (int y = ant::sim::kSurfaceFloor; y < Grid::kHeight; ++y) {
    for (int x = 0; x < Grid::kWidth; ++x) {
      const GridPos cell{x, y};
      const Material now = grid.at(cell);
      if (now == before.at(cell)) continue;
      // Excavation only ever turns diggable ground into open air, plus spoil tipped back on top.
      CHECK(((is_diggable(before.at(cell)) && now == Material::Air) ||
             (before.at(cell) == Material::Sky && now == Material::Soil)));
      if (now == Material::Air) CHECK(within_envelope(cell, world.home()));
    }
  }
  CHECK(world.invariant_holds());
}

TEST_CASE("the excavation envelope keeps the nest underground", "[nest]") {
  CHECK_FALSE(within_envelope({kHome.x, ant::sim::kSurfaceFloor}, kHome));
  CHECK(within_envelope({kHome.x, ant::sim::kSurfaceFloor + 1}, kHome));
  CHECK_FALSE(within_envelope({kHome.x + ant::sim::kDigRadius, kHome.y}, kHome));
  const Grid grid = soil_with_air({kHome});
  CHECK(grid.at(kHome) == Material::Air);
}

TEST_CASE("passages route around stone and retain unfinished shoulders", "[nest-network]") {
  Grid grid = nest_around_home();
  // The direct approach is blocked by a stone lens; an air pocket on its far side is isolated.
  for (int y = kHome.y - 7; y <= kHome.y + 7; ++y) grid.set({kHome.x + 9, y}, Material::Stone);
  grid.set({kHome.x + 14, kHome.y}, Material::Air);
  NestPlan plan;
  plan.found({{kHome.x + 22, kHome.y}, kRoomStartRadius, RoomKind::Granary, false});
  plan.update(grid, kHome, 42, false, false);
  REQUIRE(plan.passages().size() == 1);
  const auto route = plan.passages().front().route;
  REQUIRE_FALSE(route.empty());
  CHECK(std::abs(route.front().x - kHome.x) <= 3);
  CHECK(std::abs(route.front().y - kHome.y) <= 3);
  CHECK(std::any_of(route.begin(), route.end(), [](GridPos cell) {
    return std::abs(cell.y - kHome.y) > 7;
  }));
  for (GridPos cell : route) {
    REQUIRE(grid.at(cell) != Material::Stone);
    grid.set(cell, Material::Air);
  }
  plan.update(grid, kHome, 42, false, false);
  CHECK(plan.passages().front().route == route);
  CHECK_FALSE(plan.passages().front().complete);
  CHECK_FALSE(plan.work_cells(grid, kHome).empty());
  // Finish through real exposed faces; the centreline alone must not count as a finished passage.
  for (int cut = 0; cut < 600 && !plan.idle(); ++cut) {
    plan.clear_claims();
    const auto cell = plan.claim(grid, kHome);
    REQUIRE(cell);
    grid.set(*cell, Material::Air);
    plan.update(grid, kHome, 42, false, false);
  }
  CHECK(plan.idle());
  CHECK(plan.passages().front().complete);
  for (int y = kHome.y - 7; y <= kHome.y + 7; ++y) CHECK(grid.at({kHome.x + 9, y}) == Material::Stone);
}

TEST_CASE("the nest branches from nearby connected air instead of always from home", "[nest-network]") {
  Grid grid = nest_around_home();
  for (int x = kHome.x; x <= kHome.x + 26; ++x) grid.set({x, kHome.y + 3}, Material::Air);
  NestPlan plan;
  plan.found({{kHome.x + 26, kHome.y + 14}, kRoomStartRadius, RoomKind::Granary, false});
  plan.update(grid, kHome, 7, false, false);
  REQUIRE(plan.passages().size() == 1);
  const auto& route = plan.passages().front().route;
  CHECK(route.front().x > kHome.x + 20);
  CHECK(route.size() < 18);
}

TEST_CASE("recorded dig targets reserve their actual cells rather than an ordinal face", "[nest-network]") {
  Grid grid = nest_around_home();
  NestPlan plan;
  plan.update(grid, kHome, 42, true, false);
  const auto first = plan.claim(grid, kHome);
  REQUIRE(first);
  const auto second = plan.claim(grid, kHome);
  REQUIRE(second);
  plan.clear_claims();
  plan.note_claim(*second);
  const auto next = plan.claim(grid, kHome);
  REQUIRE(next);
  CHECK(*next == *first);
  CHECK(*next != *second);
}

TEST_CASE("a useful cross-passage closes a loop and capacity work takes priority", "[nest-network]") {
  Grid grid(Material::Soil);
  // Two chambers reached by the long sides of a U. A connection across its mouth saves 40 steps.
  const GridPos left{kHome.x - 12, kHome.y + 20};
  const GridPos right{kHome.x + 12, kHome.y + 20};
  for (int x = left.x; x <= right.x; ++x) grid.set({x, kHome.y}, Material::Air);
  for (int y = kHome.y; y <= left.y; ++y) {
    grid.set({left.x, y}, Material::Air);
    grid.set({right.x, y}, Material::Air);
  }
  NestPlan plan;
  plan.found({left, kRoomStartRadius, RoomKind::Granary, true});
  plan.found({right, kRoomStartRadius, RoomKind::Granary, true});
  plan.update(grid, kHome, 42, false, false, false);
  CHECK(plan.passages().empty());
  plan.update(grid, kHome, 42, false, false, true);
  REQUIRE(plan.passages().size() == 1);
  CHECK(plan.construction(grid, kHome).kind == ant::sim::ConstructionKind::CrossPassage);
  const auto connection = plan.passages().front().route;
  CHECK(connection.front() == left);
  CHECK(connection.back() == right);
  CHECK(connection.size() < 40);
  CHECK_FALSE(plan.idle());

  SECTION("capacity preempts the connection") {
    plan.update(grid, kHome, 42, true, false, true);
    CHECK(plan.construction(grid, kHome).kind == ant::sim::ConstructionKind::Nursery);
    CHECK(plan.passages().front().route == connection);
    CHECK_FALSE(plan.passages().front().complete);
  }
  SECTION("completed connection shortens actual legal routes") {
    const auto before = ant::sim::find_path(grid, left, right);
    REQUIRE(before.status == ant::sim::PathStatus::Complete);
    for (int cut = 0; cut < 300 && !plan.idle(); ++cut) {
      plan.clear_claims();
      const auto cell = plan.claim(grid, kHome);
      REQUIRE(cell);
      grid.set(*cell, Material::Air);
      plan.update(grid, kHome, 42, false, false, false);
    }
    REQUIRE(plan.idle());
    const auto after = ant::sim::find_path(grid, left, right);
    REQUIRE(after.status == ant::sim::PathStatus::Complete);
    CHECK(after.cells.size() * 4 <= before.cells.size() * 3);
    plan.update(grid, kHome, 42, false, false, true);
    CHECK(plan.passages().size() == 1);
    CHECK(plan.rooms().size() == 2);
  }
}

TEST_CASE("a fed nest never digs a duplicate connection with no travel benefit", "[nest-network]") {
  Grid grid(Material::Air);
  NestPlan plan;
  plan.found({{kHome.x - 12, kHome.y + 20}, kRoomMaxRadius, RoomKind::Granary, true});
  plan.found({{kHome.x + 12, kHome.y + 20}, kRoomMaxRadius, RoomKind::Granary, true});
  plan.update(grid, kHome, 42, false, false, true);
  CHECK(plan.passages().empty());
  CHECK(plan.idle());
}
