#pragma once

#include "sim/types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::sim {

enum class Material : std::uint8_t { Sky, Air, Soil, Clay, Root, Bedrock };

class Grid {
public:
  static constexpr int kWidth = 384;
  static constexpr int kHeight = 216;
  static constexpr int kChunkSize = 32;
  static constexpr int kChunkColumns = (kWidth + kChunkSize - 1) / kChunkSize;
  static constexpr int kChunkRows = (kHeight + kChunkSize - 1) / kChunkSize;

  explicit Grid(Material fill = Material::Soil);

  [[nodiscard]] bool in_bounds(GridPos position) const;
  [[nodiscard]] Material at(GridPos position) const;
  [[nodiscard]] bool passable(GridPos position) const;
  void set(GridPos position, Material material);

  [[nodiscard]] std::uint64_t navigation_revision() const { return navigation_revision_; }
  [[nodiscard]] std::uint64_t chunk_revision(int chunk_x, int chunk_y) const;
  [[nodiscard]] const std::vector<Material>& cells() const { return cells_; }
  [[nodiscard]] std::uint64_t material_hash() const;

private:
  [[nodiscard]] std::size_t index(GridPos position) const;

  std::vector<Material> cells_;
  std::array<std::uint64_t, kChunkColumns * kChunkRows> chunk_revisions_{};
  std::uint64_t navigation_revision_{1};
};

[[nodiscard]] bool is_passable(Material material);
[[nodiscard]] bool is_diggable(Material material);

} // namespace ant::sim
