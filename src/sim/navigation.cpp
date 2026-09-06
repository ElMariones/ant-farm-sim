#include "sim/navigation.hpp"

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

} // namespace

void HomeField::rebuild(const Grid& grid, const GridPos home) {
  if (!grid.passable(home)) {
    throw std::invalid_argument("home field origin must be passable");
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
      if (!grid.passable(neighbor) || distances_[index_of(neighbor)] >= 0) {
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

std::vector<GridPos> HomeField::path_home(const Grid& grid, const GridPos start) const {
  if (revision_ != grid.navigation_revision() || distance(start) < 0) {
    return {};
  }
  std::vector<GridPos> path;
  GridPos current = start;
  while (current != home_) {
    const int current_distance = distance(current);
    GridPos best = current;
    for (const GridPos offset : kNeighbors) {
      const GridPos candidate{current.x + offset.x, current.y + offset.y};
      if (distance(candidate) == current_distance - 1) {
        best = candidate;
        break;
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
                     const std::size_t expansion_budget) {
  if (!grid.passable(start) || !grid.passable(goal)) {
    return {};
  }
  if (start == goal) {
    return {PathStatus::Complete, {}, 0};
  }

  struct OpenNode {
    int f{};
    int g{};
    std::size_t index{};
  };
  struct Greater {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const {
      if (lhs.f != rhs.f) {
        return lhs.f > rhs.f;
      }
      if (lhs.g != rhs.g) {
        return lhs.g > rhs.g;
      }
      return lhs.index > rhs.index;
    }
  };

  constexpr int kInfinity = std::numeric_limits<int>::max();
  const std::size_t cell_count = static_cast<std::size_t>(Grid::kWidth * Grid::kHeight);
  std::vector<int> costs(cell_count, kInfinity);
  std::vector<std::size_t> parents(cell_count, cell_count);
  std::priority_queue<OpenNode, std::vector<OpenNode>, Greater> open;
  const std::size_t start_index = index_of(start);
  const std::size_t goal_index = index_of(goal);
  costs[start_index] = 0;
  open.push({heuristic(start, goal), 0, start_index});

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
      if (!grid.passable(neighbor)) {
        continue;
      }
      const std::size_t neighbor_index = index_of(neighbor);
      const int new_cost = node.g + 1;
      if (new_cost >= costs[neighbor_index]) {
        continue;
      }
      costs[neighbor_index] = new_cost;
      parents[neighbor_index] = node.index;
      open.push({new_cost + heuristic(neighbor, goal), new_cost, neighbor_index});
    }
  }
  return {PathStatus::Unreachable, {}, expanded};
}

bool is_connected(const Grid& grid, const GridPos start, const GridPos goal) {
  return find_path(grid, start, goal, static_cast<std::size_t>(Grid::kWidth * Grid::kHeight))
             .status == PathStatus::Complete;
}

} // namespace ant::sim
