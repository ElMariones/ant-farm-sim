#include "sim/dig_plan.hpp"
#include "sim/world.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace ant::sim;

namespace {

constexpr GridPos kHome{192, 58};

// A solid world with a single open cell, which is where a face can stand to start driving.
Grid soil_with_air(const std::vector<GridPos>& air) {
  Grid grid(Material::Soil);
  for (const GridPos cell : air) grid.set(cell, Material::Air);
  return grid;
}

DigPlan plan_with(const DigFace& face) {
  DigPlan plan;
  plan.restore({face});
  return plan;
}

} // namespace

TEST_CASE("the excavation envelope keeps digging buried and within reach", "[dig]") {
  CHECK(within_envelope({192, 60}, kHome));
  CHECK_FALSE(within_envelope({192, kSurfaceFloor}, kHome));
  CHECK(within_envelope({192, kSurfaceFloor + 1}, kHome));
  CHECK_FALSE(within_envelope({192 + kDigRadius, 58}, kHome));
  CHECK(within_envelope({192 + kDigRadius - 1, 58}, kHome));
  // The envelope now reaches the clay layer, which the old radius could not.
  CHECK(within_envelope({192, 140}, kHome));
  CHECK_FALSE(within_envelope({0, 60}, kHome));
}

TEST_CASE("a face plans a corridor cross-section across its heading", "[dig]") {
  const Grid grid = soil_with_air({{192, 60}});
  DigFace face; face.anchor = {192, 60}; face.heading = {0, 1}; face.active = true;
  const DigPlan plan = plan_with(face);

  const std::vector<GridPos> cells = plan.slice(grid, face, kHome);
  REQUIRE(cells.size() == static_cast<std::size_t>(kCorridorWidth));
  // One step along the heading, spread across the perpendicular.
  for (const GridPos cell : cells) CHECK(cell.y == 61);
  CHECK(std::is_sorted(cells.begin(), cells.end(),
                       [](const GridPos a, const GridPos b) { return a.x < b.x; }));
  CHECK(cells.front().x == 191);
  CHECK(cells.back().x == 193);

  DigFace sideways = face; sideways.heading = {1, 0};
  const std::vector<GridPos> across = plan_with(sideways).slice(grid, sideways, kHome);
  REQUIRE(across.size() == static_cast<std::size_t>(kCorridorWidth));
  for (const GridPos cell : across) CHECK(cell.x == 193);
}

TEST_CASE("a face never plans material it may not remove or ground outside the envelope", "[dig]") {
  Grid grid = soil_with_air({{192, 60}});
  grid.set({191, 61}, Material::Bedrock);
  grid.set({193, 61}, Material::Stone);
  DigFace face; face.anchor = {192, 60}; face.heading = {0, 1}; face.active = true;

  const std::vector<GridPos> cells = plan_with(face).slice(grid, face, kHome);
  REQUIRE(cells.size() == 1);
  CHECK(cells.front() == GridPos{192, 61});

  // A root is slow going, not a wall: it is planned like any other ground.
  Grid rooted = soil_with_air({{192, 60}});
  rooted.set({193, 61}, Material::Root);
  CHECK(plan_with(face).slice(rooted, face, kHome).size() ==
        static_cast<std::size_t>(kCorridorWidth));
  CHECK(dig_effort(Material::Root) > dig_effort(Material::Clay));
  CHECK(dig_effort(Material::Clay) > dig_effort(Material::Soil));
  CHECK(dig_effort(Material::Stone) == 0);

  // Driving at the surface plans nothing above the floor rather than breaking through.
  DigFace upward; upward.anchor = {192, kSurfaceFloor + 1}; upward.heading = {0, -1};
  upward.active = true;
  CHECK(plan_with(upward).slice(grid, upward, kHome).empty());
}

TEST_CASE("a chamber is a rounded blob, not a wider corridor", "[dig]") {
  const Grid grid = soil_with_air({{192, 80}});
  DigFace face; face.anchor = {192, 80}; face.heading = {0, 1}; face.active = true;
  face.chamber = true;

  const std::vector<GridPos> cells = plan_with(face).slice(grid, face, kHome);
  CHECK(cells.size() > static_cast<std::size_t>(kCorridorWidth * 2));
  for (const GridPos cell : cells) {
    const int dx = cell.x - 192, dy = cell.y - 80;
    CHECK(dx * dx + dy * dy <= kChamberRadius * kChamberRadius);
  }
  // Rounded, so the corners of the bounding box are not planned.
  CHECK(std::none_of(cells.begin(), cells.end(), [](const GridPos c) {
    return c == GridPos{192 + kChamberRadius, 80 + kChamberRadius};
  }));
}

TEST_CASE("claims are capped per face and spread across the working face", "[dig]") {
  const Grid grid = soil_with_air({{192, 60}});
  DigFace face; face.anchor = {192, 60}; face.heading = {0, 1}; face.active = true;
  DigPlan plan = plan_with(face);

  const auto first = plan.claim(grid, kHome);
  const auto second = plan.claim(grid, kHome);
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->face == second->face);
  // Two excavators widen the corridor together instead of queueing on one grain.
  CHECK(first->cell != second->cell);
  // The face is full: a third worker gets nothing rather than piling on.
  CHECK_FALSE(plan.claim(grid, kHome));

  plan.release(first->face);
  CHECK(plan.claim(grid, kHome));
}

TEST_CASE("a face advances only once its centreline is genuinely open", "[dig]") {
  Grid grid = soil_with_air({{192, 60}});
  DigFace face; face.anchor = {192, 60}; face.heading = {0, 1}; face.active = true;
  DigPlan plan = plan_with(face);

  // The shoulders are open but the centre is not: the face holds its ground.
  grid.set({191, 61}, Material::Air);
  grid.set({193, 61}, Material::Air);
  plan.update(grid, kHome, 42, {}, false);
  CHECK(plan.faces()[0].anchor == GridPos{192, 60});
  CHECK(plan.faces()[0].length == 0);

  grid.set({192, 61}, Material::Air);
  plan.update(grid, kHome, 42, {}, false);
  CHECK(plan.faces()[0].anchor == GridPos{192, 61});
  CHECK(plan.faces()[0].length == 1);
}

TEST_CASE("a walled-in face recovers or retires in bounded time", "[dig]") {
  Grid grid(Material::Bedrock);
  grid.set({192, 60}, Material::Air);
  DigFace face; face.anchor = {192, 60}; face.heading = {0, 1}; face.active = true;
  DigPlan plan = plan_with(face);

  // Nothing diggable in any direction and no candidates to reseed from.
  for (int tick = 0; tick < kBlockedLimit + 1; ++tick) plan.update(grid, kHome, 42, {}, false);
  CHECK(plan.idle());
  CHECK_FALSE(plan.claim(grid, kHome));
}

TEST_CASE("idle faces are seeded from connected ground and kept apart", "[dig]") {
  const Grid grid = soil_with_air({{192, 60}, {150, 70}, {230, 90}});
  DigPlan plan;
  CHECK(plan.idle());

  plan.update(grid, kHome, 42, {{192, 60}, {150, 70}, {230, 90}}, false);
  CHECK_FALSE(plan.idle());
  std::vector<GridPos> anchors;
  for (const DigFace& face : plan.faces()) if (face.active) anchors.push_back(face.anchor);
  REQUIRE(!anchors.empty());
  for (const GridPos anchor : anchors) {
    CHECK(within_envelope(anchor, kHome));
    // A face starts standing in open ground, not inside the wall it is about to remove.
    CHECK(grid.at(anchor) == Material::Air);
  }
  // Seeding is deterministic for a seed.
  DigPlan twin;
  twin.update(grid, kHome, 42, {{192, 60}, {150, 70}, {230, 90}}, false);
  CHECK(twin.faces() == plan.faces());
}

TEST_CASE("active faces survive a save and restore", "[dig]") {
  const Grid grid = soil_with_air({{192, 60}});
  DigFace face; face.anchor = {192, 60}; face.heading = {1, 0}; face.length = 9; face.active = true;
  DigPlan plan = plan_with(face);

  const std::vector<DigFace> saved = plan.save();
  REQUIRE(saved.size() == 1);
  DigPlan restored;
  restored.restore(saved);
  CHECK(restored.faces()[0] == face);
  CHECK(restored.slice(grid, restored.faces()[0], kHome) == plan.slice(grid, face, kHome));
  // Claims are worker commitments, not plan state, and are retallied after a load.
  CHECK(restored.claim(grid, kHome));
}

TEST_CASE("a dug colony forms connected passages and removes only diggable ground", "[dig]") {
  World world(7);
  const Grid before = world.grid();
  world.run_ticks(12'000);
  const Grid& grid = world.grid();

  int dug = 0;
  for (int y = kSurfaceFloor; y < Grid::kHeight; ++y) {
    for (int x = 0; x < Grid::kWidth; ++x) {
      const GridPos cell{x, y};
      const Material now = grid.at(cell);
      const Material was = before.at(cell);
      if (now == was) continue;
      // The only material change excavation may make is diggable ground becoming open,
      // plus spoil tipped back onto the surface as Soil.
      CHECK(((is_diggable(was) && now == Material::Air) ||
             (was == Material::Sky && now == Material::Soil)));
      if (now == Material::Air) {
        ++dug;
        CHECK(within_envelope(cell, world.home()));
      }
    }
  }
  CHECK(dug > 0);

  // Every open cell below the surface touches another one: excavation never leaves a lone pit.
  for (int y = kSurfaceFloor; y < Grid::kHeight; ++y) {
    for (int x = 0; x < Grid::kWidth; ++x) {
      if (grid.at({x, y}) != Material::Air) continue;
      bool joined = false;
      for (const GridPos step : {GridPos{0, -1}, GridPos{1, 0}, GridPos{0, 1}, GridPos{-1, 0}}) {
        const GridPos neighbour{x + step.x, y + step.y};
        if (grid.in_bounds(neighbour) && grid.at(neighbour) == Material::Air) joined = true;
      }
      CHECK(joined);
    }
  }
}

TEST_CASE("every excavated grain is accounted for as mound or overflow", "[dig]") {
  World world(42);
  world.run_ticks(20'000);
  const WorldStats& stats = world.stats();
  // A grain is hauled out exactly once. Deliveries split into what the bounded apron accepted and
  // what was tipped out of view, and nothing is counted twice or lost.
  CHECK(stats.spoil_delivered == world.spoil_mound() + stats.spoil_overflow);
  CHECK(stats.spoil_delivered <= stats.cells_excavated);
}
