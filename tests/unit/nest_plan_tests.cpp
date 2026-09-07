#include "sim/nest_plan.hpp"
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
