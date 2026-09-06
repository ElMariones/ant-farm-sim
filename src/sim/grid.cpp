#include "sim/grid.hpp"

#include <stdexcept>

namespace ant::sim {

Grid::Grid(const Material fill) : cells_(static_cast<std::size_t>(kWidth * kHeight), fill) {}

bool Grid::in_bounds(const GridPos position) const {
  return position.x >= 0 && position.x < kWidth && position.y >= 0 && position.y < kHeight;
}

std::size_t Grid::index(const GridPos position) const {
  if (!in_bounds(position)) {
    throw std::out_of_range("grid position out of bounds");
  }
  return static_cast<std::size_t>(position.y * kWidth + position.x);
}

Material Grid::at(const GridPos position) const { return cells_.at(index(position)); }

bool Grid::passable(const GridPos position) const {
  return in_bounds(position) &&
         is_passable(cells_[static_cast<std::size_t>(position.y * kWidth + position.x)]);
}

void Grid::set(const GridPos position, const Material material) {
  const std::size_t cell_index = index(position);
  const Material old = cells_[cell_index];
  if (old == material) {
    return;
  }
  cells_[cell_index] = material;
  const int chunk_x = position.x / kChunkSize;
  const int chunk_y = position.y / kChunkSize;
  ++chunk_revisions_[static_cast<std::size_t>(chunk_y * kChunkColumns + chunk_x)];
  if (is_passable(old) != is_passable(material)) {
    ++navigation_revision_;
  }
}

std::uint64_t Grid::chunk_revision(const int chunk_x, const int chunk_y) const {
  if (chunk_x < 0 || chunk_x >= kChunkColumns || chunk_y < 0 || chunk_y >= kChunkRows) {
    throw std::out_of_range("chunk position out of bounds");
  }
  return chunk_revisions_[static_cast<std::size_t>(chunk_y * kChunkColumns + chunk_x)];
}

std::uint64_t Grid::material_hash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const Material material : cells_) {
    hash ^= static_cast<std::uint8_t>(material);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool is_passable(const Material material) {
  return material == Material::Sky || material == Material::Air;
}

bool is_diggable(const Material material) {
  return material == Material::Soil || material == Material::Clay;
}

} // namespace ant::sim
