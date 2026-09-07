#include "sim/navigation.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>

namespace ant::sim {
namespace {

constexpr std::array<GridPos, 4> kNeighbors{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

std::size_t index_of(const GridPos position) {
  return static_cast<std::size_t>(position.y * Grid::kWidth + position.x);
}

GridPos position_of(const std::size_t index) {
  return {static_cast<int>(index % static_cast<std::size_t>(Grid::kWidth)),
          static_cast<int>(index / static_cast<std::size_t>(Grid::kWidth))};
}

int heuristic(const GridPos a, const GridPos b) {
  return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

// A stable per-ant ordering value for one candidate cell. Nothing here consumes simulation RNG, so
// route variation cannot shift brood or job outcomes.
std::uint64_t preference(const std::uint64_t key, const GridPos cell) {
  return mix_seed(key ^ (static_cast<std::uint64_t>(cell.x) * 0x9E3779B97F4A7C15ULL) ^
                  (static_cast<std::uint64_t>(cell.y) * 0xBF58476D1CE4E5B9ULL));
}

} // namespace

RouteBias route_bias(const std::uint64_t seed, const std::uint64_t actor_id) {
  // Force a non-zero key so an actor never accidentally lands on "no variation".
  return {mix_seed(seed ^ 0x0D16B1A5ULL ^ (actor_id * 0x9E3779B97F4A7C15ULL)) | 1ULL};
}

void HomeField::rebuild(const Grid& grid, const GridPos home) {
  if (!grid.walkable(home)) {
    throw std::invalid_argument("home field origin must be walkable");
  }
  distances_.assign(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight), -1);
  home_ = home;
  revision_ = grid.navigation_revision();

  std::queue<GridPos> frontier;
  distances_[index_of(home)] = 0;
  frontier.push(home);
  while (!frontier.empty()) {
    const GridPos current = frontier.front();
    frontier.pop();
    const int next_distance = distances_[index_of(current)] + 1;
    for (const GridPos offset : kNeighbors) {
      const GridPos neighbor{current.x + offset.x, current.y + offset.y};
      if (!grid.walkable(neighbor) || distances_[index_of(neighbor)] >= 0) {
        continue;
      }
      distances_[index_of(neighbor)] = next_distance;
      frontier.push(neighbor);
    }
  }
}

int HomeField::distance(const GridPos position) const {
  if (position.x < 0 || position.x >= Grid::kWidth || position.y < 0 ||
      position.y >= Grid::kHeight || distances_.empty()) {
    return -1;
  }
  return distances_[index_of(position)];
}

std::vector<GridPos> HomeField::path_home(const Grid& grid, const GridPos start,
                                          const RouteBias bias) const {
  if (revision_ != grid.navigation_revision() || distance(start) < 0) {
    return {};
  }
  std::vector<GridPos> path;
  GridPos current = start;
  while (current != home_) {
    const int current_distance = distance(current);
    GridPos best = current;
    std::uint64_t best_preference = 0;
    // Every descending neighbour is an equally short way home. Which one this ant takes is settled
    // by its own stable preference, so a wide gallery carries several routes and a one-cell
    // bottleneck still carries exactly one.
    for (const GridPos offset : kNeighbors) {
      const GridPos candidate{current.x + offset.x, current.y + offset.y};
      if (distance(candidate) != current_distance - 1) {
        continue;
      }
      if (bias.key == 0) {
        best = candidate;
        break;
      }
      const std::uint64_t candidate_preference = preference(bias.key, candidate);
      if (best == current || candidate_preference < best_preference) {
        best = candidate;
        best_preference = candidate_preference;
      }
    }
    if (best == current) {
      return {};
    }
    path.push_back(best);
    current = best;
  }
  return path;
}

PathResult find_path(const Grid& grid, const GridPos start, const GridPos goal,
                     const std::size_t expansion_budget, const RouteBias bias) {
  if (!grid.walkable(start) || !grid.walkable(goal)) {
    return {};
  }
  if (start == goal) {
    return {PathStatus::Complete, {}, 0};
  }

  struct OpenNode {
    int f{};
    int g{};
    std::size_t index{};
    // Settles which of two equally good nodes is expanded first. Whichever predecessor gets there
    // first claims the parent pointer, so this is the knob that decides which equally short route
    // an ant ends up committed to. Cell index for the canonical order, this ant's stable
    // preference otherwise.
    std::uint64_t order{};
  };
  struct Greater {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const {
      if (lhs.f != rhs.f) {
        return lhs.f > rhs.f;
      }
      if (lhs.g != rhs.g) {
        return lhs.g > rhs.g;
      }
      return lhs.order > rhs.order;
    }
  };
  const auto ordering = [bias](const GridPos cell, const std::size_t index) {
    return bias.key == 0 ? static_cast<std::uint64_t>(index) : preference(bias.key, cell);
  };

  constexpr int kInfinity = std::numeric_limits<int>::max();
  const std::size_t cell_count = static_cast<std::size_t>(Grid::kWidth * Grid::kHeight);
  std::vector<int> costs(cell_count, kInfinity);
  std::vector<std::size_t> parents(cell_count, cell_count);
  std::priority_queue<OpenNode, std::vector<OpenNode>, Greater> open;
  const std::size_t start_index = index_of(start);
  const std::size_t goal_index = index_of(goal);
  costs[start_index] = 0;
  open.push({heuristic(start, goal), 0, start_index, ordering(start, start_index)});

  std::size_t expanded = 0;
  while (!open.empty()) {
    const OpenNode node = open.top();
    open.pop();
    if (node.g != costs[node.index]) {
      continue;
    }
    if (node.index == goal_index) {
      std::vector<GridPos> reversed;
      for (std::size_t cursor = goal_index; cursor != start_index; cursor = parents[cursor]) {
        if (cursor == cell_count) {
          return {PathStatus::Unreachable, {}, expanded};
        }
        reversed.push_back(position_of(cursor));
      }
      std::reverse(reversed.begin(), reversed.end());
      return {PathStatus::Complete, std::move(reversed), expanded};
    }
    if (expanded >= expansion_budget) {
      return {PathStatus::BudgetExhausted, {}, expanded};
    }
    ++expanded;
    const GridPos current = position_of(node.index);
    for (const GridPos offset : kNeighbors) {
      const GridPos neighbor{current.x + offset.x, current.y + offset.y};
      if (!grid.walkable(neighbor)) {
        continue;
      }
      const std::size_t neighbor_index = index_of(neighbor);
      const int new_cost = node.g + 1;
      if (new_cost >= costs[neighbor_index]) {
        continue;
      }
      costs[neighbor_index] = new_cost;
      parents[neighbor_index] = node.index;
      open.push({new_cost + heuristic(neighbor, goal), new_cost, neighbor_index,
                 ordering(neighbor, neighbor_index)});
    }
  }
  return {PathStatus::Unreachable, {}, expanded};
}

bool is_connected(const Grid& grid, const GridPos start, const GridPos goal) {
  return find_path(grid, start, goal, static_cast<std::size_t>(Grid::kWidth * Grid::kHeight))
             .status == PathStatus::Complete;
}

} // namespace ant::sim
