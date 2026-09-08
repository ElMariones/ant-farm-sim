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

// Reusable scratch owned by one World. Search stamps initialize only visited cells; neither
// topology nor route decisions are cached. Interleaved worlds therefore cannot share stale work.
class Pathfinder {
public:
  [[nodiscard]] PathResult find(const Grid& grid, GridPos start, GridPos goal,
                                 std::size_t expansion_budget = 4096, RouteBias bias = {});
private:
  struct OpenNode {
    int f{}, g{};
    std::size_t index{};
    std::uint64_t order{};
  };
  struct Greater {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const;
  };
  std::vector<int> costs_;
  std::vector<std::size_t> parents_;
  std::vector<std::uint32_t> stamps_;
  std::vector<OpenNode> open_;
  std::uint32_t search_{};
};

[[nodiscard]] PathResult find_path(const Grid& grid, GridPos start, GridPos goal,
                                   std::size_t expansion_budget = 4096, RouteBias bias = {});
[[nodiscard]] bool is_connected(const Grid& grid, GridPos start, GridPos goal);

} // namespace ant::sim
