#pragma once

#include "sim/grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ant::sim {

enum class PathStatus { Complete, Unreachable, BudgetExhausted };

struct PathResult {
  PathStatus status{PathStatus::Unreachable};
  std::vector<GridPos> cells;
  std::size_t expanded{};
};

class HomeField {
public:
  void rebuild(const Grid& grid, GridPos home);
  [[nodiscard]] int distance(GridPos position) const;
  [[nodiscard]] std::vector<GridPos> path_home(const Grid& grid, GridPos start) const;
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
  std::vector<int> distances_;
  GridPos home_{};
  std::uint64_t revision_{};
};

[[nodiscard]] PathResult find_path(const Grid& grid, GridPos start, GridPos goal,
                                   std::size_t expansion_budget = 4096);
[[nodiscard]] bool is_connected(const Grid& grid, GridPos start, GridPos goal);

} // namespace ant::sim
