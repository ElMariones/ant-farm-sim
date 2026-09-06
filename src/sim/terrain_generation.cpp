#include "sim/terrain_generation.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <cmath>

namespace ant::sim {

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

  const int source_a_x = 54 + static_cast<int>(mix_seed(seed ^ 0xA11CEULL) % 72ULL);
  const int source_b_x = 258 + static_cast<int>(mix_seed(seed ^ 0xBEEFULL) % 72ULL);
  generated.source_positions = {{{source_a_x, 30}, {source_b_x, 30}}};

  for (int y = 31; y <= 58; ++y) {
    const int half_width = y < 46 ? 1 : 2;
    for (int x = generated.entrance.x - half_width; x <= generated.entrance.x + half_width; ++x) {
      grid.set({x, y}, Material::Air);
    }
  }

  for (int y = 50; y <= 66; ++y) {
    for (int x = 177; x <= 207; ++x) {
      const double nx = static_cast<double>(x - 192) / 15.5;
      const double ny = static_cast<double>(y - 58) / 8.5;
      if (nx * nx + ny * ny <= 1.0) {
        grid.set({x, y}, Material::Air);
      }
    }
  }

  for (int root_index = 0; root_index < 18; ++root_index) {
    const std::uint64_t root_seed = mix_seed(seed + static_cast<std::uint64_t>(root_index) * 31ULL);
    int x = 12 + static_cast<int>(root_seed % 360ULL);
    const int length = 10 + static_cast<int>((root_seed >> 12U) % 34ULL);
    for (int depth = 0; depth < length; ++depth) {
      const int y = 33 + depth;
      if (x >= 184 && x <= 200 && y <= 72) {
        continue;
      }
      grid.set({x, y}, Material::Root);
      if ((mix_seed(root_seed + static_cast<std::uint64_t>(depth)) & 3ULL) == 0ULL) {
        x = std::clamp(x + (((root_seed >> (depth % 17)) & 1ULL) == 0ULL ? -1 : 1), 1,
                       Grid::kWidth - 2);
      }
    }
  }

  return generated;
}

} // namespace ant::sim
