#include "sim/terrain_generation.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <cmath>

namespace ant::sim {
namespace {

// Carves a filled ellipse of open air. Chambers are described by their centre and semi-axes so the
// starting nest reads as rooms joined by passages rather than one scooped-out cavity.
// Carves a filled ellipse of open air, for the royal chamber the founding queen sits in.
void carve_ellipse(Grid& grid, const GridPos centre, const double half_width,
                   const double half_height) {
  const int reach_x = static_cast<int>(half_width) + 1;
  const int reach_y = static_cast<int>(half_height) + 1;
  for (int y = centre.y - reach_y; y <= centre.y + reach_y; ++y) {
    for (int x = centre.x - reach_x; x <= centre.x + reach_x; ++x) {
      if (!grid.in_bounds({x, y})) continue;
      const double nx = static_cast<double>(x - centre.x) / half_width;
      const double ny = static_cast<double>(y - centre.y) / half_height;
      if (nx * nx + ny * ny <= 1.0) grid.set({x, y}, Material::Air);
    }
  }
}

void carve_room(Grid& grid, const Room& room) {
  const int reach = room.reach();
  for (int dy = -reach; dy <= reach; ++dy) {
    for (int dx = -reach; dx <= reach; ++dx) {
      const GridPos cell{room.centre.x + dx, room.centre.y + dy};
      if (grid.in_bounds(cell) && room.contains(cell)) grid.set(cell, Material::Air);
    }
  }
}

// The same larger-axis-first staircase the colony digs later, so the founding corridors and the
// ones the ants cut themselves are the same shape.
void carve_passage(Grid& grid, const GridPos from, const GridPos to) {
  GridPos cursor = from;
  for (int guard = 0; cursor != to && guard < 400; ++guard) {
    const int dx = to.x - cursor.x;
    const int dy = to.y - cursor.y;
    GridPos heading{0, 0};
    if (std::abs(dx) >= std::abs(dy)) heading.x = dx > 0 ? 1 : -1;
    else heading.y = dy > 0 ? 1 : -1;
    cursor = {cursor.x + heading.x, cursor.y + heading.y};
    const GridPos side{-heading.y, heading.x};
    for (int offset = -1; offset <= 1; ++offset) {
      const GridPos cell{cursor.x + side.x * offset, cursor.y + side.y * offset};
      if (grid.in_bounds(cell)) grid.set(cell, Material::Air);
    }
  }
}

} // namespace

GeneratedTerrain generate_terrain(const std::uint64_t seed) {
  GeneratedTerrain generated{Grid{Material::Soil}, {192, 58}, {192, 31}, {}};
  Grid& grid = generated.grid;

  for (int y = 0; y < Grid::kHeight; ++y) {
    for (int x = 0; x < Grid::kWidth; ++x) {
      Material material = Material::Soil;
      if (y < 32) {
        material = Material::Sky;
      } else if (y >= 212) {
        material = Material::Bedrock;
      } else if (y >= 138) {
        material = Material::Clay;
      }
      grid.set({x, y}, material);
    }
  }

  // Roots run down from the turf. They are diggable, but slowly, so a corridor that meets one
  // visibly stalls before it breaks through.
  for (int root_index = 0; root_index < 22; ++root_index) {
    const std::uint64_t root_seed = mix_seed(seed + static_cast<std::uint64_t>(root_index) * 31ULL);
    int x = 12 + static_cast<int>(root_seed % 360ULL);
    const int length = 12 + static_cast<int>((root_seed >> 12U) % 40ULL);
    for (int depth = 0; depth < length; ++depth) {
      const int y = 33 + depth;
      if (!grid.in_bounds({x, y})) break;
      grid.set({x, y}, Material::Root);
      if ((mix_seed(root_seed + static_cast<std::uint64_t>(depth)) & 3ULL) == 0ULL) {
        x = std::clamp(x + (((root_seed >> (depth % 17)) & 1ULL) == 0ULL ? -1 : 1), 1,
                       Grid::kWidth - 2);
      }
    }
  }

  // Stone lenses through the deep ground. Nothing removes them, so the colony has to route around
  // them the way a real nest bends around a buried rock.
  for (int patch = 0; patch < 44; ++patch) {
    const std::uint64_t noise =
        mix_seed(seed * 0x9E3779B9ULL + static_cast<std::uint64_t>(patch) * 0x2545F491ULL);
    const int centre_x = 6 + static_cast<int>(noise % static_cast<std::uint64_t>(Grid::kWidth - 12));
    const int centre_y = 128 + static_cast<int>((noise >> 20U) % 78ULL);
    const int reach_x = 3 + static_cast<int>((noise >> 40U) % 6ULL);
    const int reach_y = 2 + static_cast<int>((noise >> 48U) % 4ULL);
    for (int y = centre_y - reach_y; y <= centre_y + reach_y; ++y) {
      for (int x = centre_x - reach_x; x <= centre_x + reach_x; ++x) {
        if (!grid.in_bounds({x, y}) || y >= 212) continue;
        const double nx = static_cast<double>(x - centre_x) / (static_cast<double>(reach_x) + 0.5);
        const double ny = static_cast<double>(y - centre_y) / (static_cast<double>(reach_y) + 0.5);
        // A ragged rim, so a lens does not read as a drawn ellipse.
        const double rim =
            0.88 + static_cast<double>(mix_seed(noise ^ (static_cast<std::uint64_t>(x) << 20U) ^
                                                static_cast<std::uint64_t>(y)) % 26ULL) / 100.0;
        if (nx * nx + ny * ny <= rim && is_diggable(grid.at({x, y}))) {
          grid.set({x, y}, Material::Stone);
        }
      }
    }
  }

  // The founding nest: an entrance shaft down to the royal chamber, a brood room below the queen
  // and a granary to either side, joined by corridors. Carved last, so nothing generated above can
  // block it.
  for (int y = 31; y <= 54; ++y) {
    const int half_width = y < 46 ? 1 : 2;
    for (int x = generated.entrance.x - half_width; x <= generated.entrance.x + half_width; ++x) {
      grid.set({x, y}, Material::Air);
    }
  }
  carve_ellipse(grid, generated.home, 5.0, 3.5);
  // A founding nest is already a handful of small chambers: one for the brood and three for stores.
  generated.rooms = {
      {{generated.home.x, generated.home.y + 12}, kRoomStartRadius, RoomKind::Nursery, true},
      {{generated.home.x - 13, generated.home.y + 8}, kRoomStartRadius, RoomKind::Granary, true},
      {{generated.home.x + 13, generated.home.y + 8}, kRoomStartRadius, RoomKind::Granary, true},
      {{generated.home.x - 4, generated.home.y + 19}, kRoomStartRadius, RoomKind::Granary, true}};
  for (const Room& room : generated.rooms) {
    carve_passage(grid, generated.home, room.centre);
    carve_room(grid, room);
  }

  // Two forage sites on the surface: one within a short trip of the entrance and one a long trek
  // away, on opposite sides, so the colony has a near staple and a distant prize.
  const std::uint64_t placement = mix_seed(seed ^ 0xF00DULL);
  const int near_side = (placement & 1ULL) == 0ULL ? -1 : 1;
  const int near_offset = 48 + static_cast<int>((placement >> 8U) % 29ULL);
  const int far_offset = 88 + static_cast<int>((placement >> 24U) % 35ULL);
  const int carbohydrate_x =
      std::clamp(generated.entrance.x + near_side * near_offset, 8, Grid::kWidth - 9);
  const int protein_x =
      std::clamp(generated.entrance.x - near_side * far_offset, 8, Grid::kWidth - 9);
  generated.source_positions = {{{carbohydrate_x, 31}, {protein_x, 31}}};

  return generated;
}

} // namespace ant::sim
