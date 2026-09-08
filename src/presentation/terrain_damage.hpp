#pragma once

#include "sim/grid.hpp"
#include "sim/nest_plan.hpp"

#include <array>
#include <span>

namespace ant::presentation {

// Pixel shading reads the material of the cell and its cardinal neighbors. Update every touched
// chunk including that one-cell halo; room geometry changes invalidate both old and new bounds.
// Pure bookkeeping: no graphics API or simulation mutation, so boundary coverage is headless-testable.
struct TerrainDamage {
  std::array<bool, sim::Grid::kChunkColumns * sim::Grid::kChunkRows> chunks{};
  [[nodiscard]] bool includes(int x, int y) const {
    return chunks[static_cast<std::size_t>((y / sim::Grid::kChunkSize) * sim::Grid::kChunkColumns +
                                          x / sim::Grid::kChunkSize)];
  }
  void include(int x, int y) {
    if (x < 0 || y < 0 || x >= sim::Grid::kWidth || y >= sim::Grid::kHeight) return;
    chunks[static_cast<std::size_t>((y / sim::Grid::kChunkSize) * sim::Grid::kChunkColumns +
                                    x / sim::Grid::kChunkSize)] = true;
  }
};

[[nodiscard]] TerrainDamage terrain_damage(std::span<const sim::Material> before,
    std::span<const sim::Material> after, std::span<const sim::Room> old_rooms,
    std::span<const sim::Room> new_rooms, bool force_full = false);

} // namespace ant::presentation
