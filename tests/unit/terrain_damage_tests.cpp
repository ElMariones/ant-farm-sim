#include "presentation/terrain_damage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <vector>

using ant::sim::Grid;
using ant::sim::Material;
using ant::presentation::terrain_damage;

TEST_CASE("one excavation invalidates its shading neighbors across chunk boundaries", "[terrain-damage][performance]") {
  const std::vector<Material> before(Grid::kWidth * Grid::kHeight, Material::Soil);
  // Cover every map edge and chunk boundary, including the short final row of chunks.
  for (int y : {0, 31, 32, 63, 64, 191, 192, 215}) {
    for (int x : {0, 31, 32, 63, 64, 351, 352, 383}) {
      auto after = before;
      after[static_cast<std::size_t>(y * Grid::kWidth + x)] = Material::Air;
      const auto damage = terrain_damage(before, after, {}, {});
      CHECK(damage.includes(x, y));
      if (x > 0) CHECK(damage.includes(x - 1, y));
      if (x + 1 < Grid::kWidth) CHECK(damage.includes(x + 1, y));
      if (y > 0) CHECK(damage.includes(x, y - 1));
      if (y + 1 < Grid::kHeight) CHECK(damage.includes(x, y + 1));
      CHECK(std::count(damage.chunks.begin(), damage.chunks.end(), true) <= 3);
    }
  }
}

TEST_CASE("room changes repaint the old and new footprint even without digging", "[terrain-damage][performance]") {
  const std::vector<Material> cells(Grid::kWidth * Grid::kHeight, Material::Air);
  const std::vector<ant::sim::Room> before{{{31, 63}, 3, ant::sim::RoomKind::Nursery, true}};
  std::vector<ant::sim::Room> after = before;
  after[0].centre = {160, 120};
  after[0].radius = 5;
  after[0].kind = ant::sim::RoomKind::Granary;
  const auto damage = terrain_damage(cells, cells, before, after);
  for (const auto& rooms : {before, after}) {
    for (const auto& room : rooms) {
      for (int y = -room.reach(); y <= room.reach(); ++y)
        for (int x = -room.reach(); x <= room.reach(); ++x)
          if (room.contains({room.centre.x + x, room.centre.y + y}))
            CHECK(damage.includes(room.centre.x + x, room.centre.y + y));
    }
  }
  const auto removed = terrain_damage(cells, cells, before, {});
  CHECK(removed.includes(31, 63));
}

TEST_CASE("unchanged terrain is free and a new colony gets a full repaint", "[terrain-damage][performance]") {
  const std::vector<Material> cells(Grid::kWidth * Grid::kHeight, Material::Soil);
  const auto unchanged = terrain_damage(cells, cells, {}, {});
  CHECK(std::count(unchanged.chunks.begin(), unchanged.chunks.end(), true) == 0);
  const auto first = terrain_damage({}, cells, {}, {});
  CHECK(std::all_of(first.chunks.begin(), first.chunks.end(), [](bool value) { return value; }));
  const auto other_seed = terrain_damage(cells, cells, {}, {}, true);
  CHECK(other_seed.chunks == first.chunks);
}
