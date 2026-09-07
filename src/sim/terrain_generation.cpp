#include "sim/terrain_generation.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <cmath>

namespace ant::sim {
namespace {

// Carves a filled ellipse of open air. Chambers are described by their centre and semi-axes so the
// starting nest reads as rooms joined by passages rather than one scooped-out cavity.
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

void carve_corridor(Grid& grid, const int from_x, const int to_x, const int top_y,
                    const int bottom_y) {
  for (int y = top_y; y <= bottom_y; ++y) {
    for (int x = std::min(from_x, to_x); x <= std::max(from_x, to_x); ++x) {
      if (grid.in_bounds({x, y})) grid.set({x, y}, Material::Air);
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

  // The founding nest: an entrance shaft, a royal chamber around the queen, and two store chambers
  // out to either side. Carved last, so nothing generated above can block it.
  for (int y = 31; y <= 54; ++y) {
    const int half_width = y < 46 ? 1 : 2;
    for (int x = generated.entrance.x - half_width; x <= generated.entrance.x + half_width; ++x) {
      grid.set({x, y}, Material::Air);
    }
  }
  carve_ellipse(grid, generated.home, 7.0, 4.5);
  const GridPos left_store{generated.home.x - 20, 60};
  const GridPos right_store{generated.home.x + 20, 60};
  carve_ellipse(grid, left_store, 7.0, 4.5);
  carve_ellipse(grid, right_store, 7.0, 4.5);
  carve_corridor(grid, left_store.x + 6, generated.home.x - 6, 59, 61);
  carve_corridor(grid, generated.home.x + 6, right_store.x - 6, 59, 61);

  // Two forage sites on the surface: one within a short trip of the entrance and one a long trek
  // away, on opposite sides, so the colony has a near staple and a distant prize.
  const std::uint64_t placement = mix_seed(seed ^ 0xF00DULL);
  const int near_side = (placement & 1ULL) == 0ULL ? -1 : 1;
  const int near_offset = 60 + static_cast<int>((placement >> 8U) % 33ULL);
  const int far_offset = 108 + static_cast<int>((placement >> 24U) % 39ULL);
  const int carbohydrate_x =
      std::clamp(generated.entrance.x + near_side * near_offset, 8, Grid::kWidth - 9);
  const int protein_x =
      std::clamp(generated.entrance.x - near_side * far_offset, 8, Grid::kWidth - 9);
  generated.source_positions = {{{carbohydrate_x, 31}, {protein_x, 31}}};

  return generated;
}

} // namespace ant::sim
