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

bool Grid::walkable(const GridPos position) const {
  if (!in_bounds(position)) return false;
  const Material material = cells_[index(position)];
  if (!is_passable(material)) return false;
  if (material != Material::Sky) return true;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      const GridPos neighbour{position.x + dx, position.y + dy};
      if (!in_bounds(neighbour)) continue;
      if (!is_passable(cells_[index(neighbour)])) return true;
    }
  }
  return false;
}

std::uint64_t Grid::terrain_revision() const {
  std::uint64_t total = 0;
  for (const std::uint64_t revision : chunk_revisions_) total += revision;
  return total;
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
  return material == Material::Soil || material == Material::Clay || material == Material::Root;
}

std::uint16_t dig_effort(const Material material) {
  switch (material) {
  case Material::Soil: return 1'000;
  case Material::Clay: return 2'500;
  case Material::Root: return 4'000;
  default: return 0;
  }
}

} // namespace ant::sim
