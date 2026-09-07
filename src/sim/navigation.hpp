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

// Ants share one distance field and one A*, so routing stays cheap, but two ants standing on the
// same cell need not walk the same way home. A bias keys a deterministic preference among choices
// that are already equally good, which spreads traffic without inventing detours. A zero key means
// the canonical fixed order, which is what connectivity checks and fixtures want.
struct RouteBias {
  std::uint64_t key{};
};
[[nodiscard]] RouteBias route_bias(std::uint64_t seed, std::uint64_t actor_id);

class HomeField {
public:
  void rebuild(const Grid& grid, GridPos home);
  [[nodiscard]] int distance(GridPos position) const;
  [[nodiscard]] std::vector<GridPos> path_home(const Grid& grid, GridPos start,
                                               RouteBias bias = {}) const;
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
  std::vector<int> distances_;
  GridPos home_{};
  std::uint64_t revision_{};
};

[[nodiscard]] PathResult find_path(const Grid& grid, GridPos start, GridPos goal,
                                   std::size_t expansion_budget = 4096, RouteBias bias = {});
[[nodiscard]] bool is_connected(const Grid& grid, GridPos start, GridPos goal);

} // namespace ant::sim
