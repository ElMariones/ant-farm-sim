#include "sim/pheromones.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ant::sim {
namespace {
constexpr std::uint32_t kScale = 65'536;
constexpr std::uint32_t kNeighborWeight = 3'277; // 0.05
constexpr std::uint32_t kRetention = 65'385; // effective ~60 second half-life at 5 Hz
constexpr GridPos kNeighbors[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
}

TrailField::TrailField()
    : front_(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight)), back_(front_.size()),
      marked_(front_.size()) {}

std::size_t TrailField::index(const GridPos position) {
  if (position.x < 0 || position.x >= Grid::kWidth || position.y < 0 || position.y >= Grid::kHeight) {
    throw std::out_of_range("trail position outside world");
  }
  return static_cast<std::size_t>(position.y * Grid::kWidth + position.x);
}

void TrailField::deposit(const GridPos position, const std::uint16_t amount) {
  const std::size_t cell = index(position);
  front_[cell] = static_cast<std::uint16_t>(std::min<std::uint32_t>(
      std::numeric_limits<std::uint16_t>::max(), static_cast<std::uint32_t>(front_[cell]) + amount));
  if (marked_[cell] == 0) {
    marked_[cell] = 1;
    active_.push_back(cell);
  }
}

void TrailField::update(const Grid& grid) {
  std::vector<std::size_t> candidates = active_;
  for (const std::size_t cell : active_) {
    const GridPos position{static_cast<int>(cell % Grid::kWidth), static_cast<int>(cell / Grid::kWidth)};
    for (const GridPos delta : kNeighbors) {
      const GridPos neighbor{position.x + delta.x, position.y + delta.y};
      if (grid.in_bounds(neighbor)) candidates.push_back(index(neighbor));
    }
  }
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
  std::vector<std::size_t> next_active;
  for (const std::size_t cell : candidates) {
      const GridPos position{static_cast<int>(cell % Grid::kWidth), static_cast<int>(cell / Grid::kWidth)};
      const std::uint32_t center = front_[index(position)];
      std::uint64_t mixed = static_cast<std::uint64_t>(center) * (kScale - 4 * kNeighborWeight);
      for (const GridPos delta : kNeighbors) {
        const GridPos neighbor{position.x + delta.x, position.y + delta.y};
        const std::uint32_t value = grid.in_bounds(neighbor) && grid.passable(neighbor)
                                        ? front_[index(neighbor)] : center;
        mixed += static_cast<std::uint64_t>(value) * kNeighborWeight;
      }
      back_[cell] = static_cast<std::uint16_t>(
          std::min<std::uint64_t>(65'535, ((mixed / kScale) * kRetention) / kScale));
      // Values below the visible/useful fixed-point floor evaporate completely. This keeps the
      // bounded sparse frontier from retaining a halo of one-unit cells forever.
      if (back_[cell] > 3) {
        next_active.push_back(cell);
      } else {
        back_[cell] = 0;
      }
  }
  for (const std::size_t cell : active_) marked_[cell] = 0;
  front_.swap(back_);
  for (const std::size_t cell : candidates) back_[cell] = 0;
  active_ = std::move(next_active);
  for (const std::size_t cell : active_) marked_[cell] = 1;
}

std::uint16_t TrailField::at(const GridPos position) const { return front_[index(position)]; }

std::uint64_t TrailField::mass() const {
  return std::accumulate(front_.begin(), front_.end(), std::uint64_t{0});
}

void TrailField::restore(const std::vector<std::uint16_t>& cells) {
  if (cells.size() != front_.size()) throw std::invalid_argument("trail field has wrong size");
  front_ = cells;
  std::fill(back_.begin(), back_.end(), 0);
  std::fill(marked_.begin(), marked_.end(), 0);
  active_.clear();
  for (std::size_t index = 0; index < front_.size(); ++index) {
    if (front_[index] > 0) { active_.push_back(index); marked_[index] = 1; }
  }
}

} // namespace ant::sim
