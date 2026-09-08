#include "sim/nest_plan.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>

namespace ant::sim {
namespace {

constexpr std::array<GridPos, 4> kCardinals{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

// Bearings the nest may grow along, all sideways or downward so a room never surfaces. Each has
// Manhattan length four, so a whole number of steps places a centre at a predictable distance.
constexpr std::array<GridPos, 9> kBearings{
    {{-4, 0}, {4, 0}, {-3, 1}, {3, 1}, {-2, 2}, {2, 2}, {-1, 3}, {1, 3}, {0, 4}}};

int manhattan(const GridPos a, const GridPos b) {
  return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

std::size_t cell_index(GridPos p) {
  return static_cast<std::size_t>(p.y * Grid::kWidth + p.x);
}
GridPos cell_position(std::size_t index) {
  return {static_cast<int>(index % Grid::kWidth), static_cast<int>(index / Grid::kWidth)};
}

// Only physical, cardinally connected nest air can anchor construction. An isolated pocket of
// air behind rock is not a second entrance and can never attract a stranded excavation crew.
std::vector<int> air_distances(const Grid& grid, GridPos origin) {
  std::vector<int> distances(Grid::kWidth * Grid::kHeight, -1);
  if (grid.at(origin) != Material::Air) return distances;
  std::queue<GridPos> pending;
  pending.push(origin);
  distances[cell_index(origin)] = 0;
  while (!pending.empty()) {
    const GridPos current = pending.front();
    pending.pop();
    for (GridPos direction : kCardinals) {
      const GridPos next{current.x + direction.x, current.y + direction.y};
      if (!grid.in_bounds(next) || grid.at(next) != Material::Air ||
          distances[cell_index(next)] >= 0) continue;
      distances[cell_index(next)] = distances[cell_index(current)] + 1;
      pending.push(next);
    }
  }
  return distances;
}

// Search backwards from a room to the nearest connected part of the nest, or to one particular
// anchor for a cross-passage. Soil costs labor; roots/clay cost more; stone is never traversable.
// The low-frequency seed variation produces persistent bends without consuming an actor's RNG.
std::vector<GridPos> plan_route(const Grid& grid, GridPos home, GridPos goal,
                                const std::vector<int>& connected, std::uint64_t seed,
                                std::optional<GridPos> anchor = std::nullopt) {
  if (!within_envelope(goal, home) ||
      (grid.at(goal) != Material::Air && !is_diggable(grid.at(goal)))) return {};
  using Node = std::pair<int, std::size_t>;
  std::priority_queue<Node, std::vector<Node>, std::greater<>> open;
  std::vector<int> costs(Grid::kWidth * Grid::kHeight, std::numeric_limits<int>::max());
  std::vector<int> parents(costs.size(), -1);
  costs[cell_index(goal)] = 0;
  open.push({0, cell_index(goal)});
  std::size_t expanded = 0;
  while (!open.empty() && expanded < 8192) {
    const auto [cost, index] = open.top();
    open.pop();
    if (cost != costs[index]) continue;
    ++expanded;
    const GridPos current = cell_position(index);
    if (anchor ? current == *anchor : connected[index] >= 0) {
      std::vector<GridPos> route;
      for (int cursor = static_cast<int>(index); cursor >= 0; cursor = parents[static_cast<std::size_t>(cursor)]) {
        route.push_back(cell_position(static_cast<std::size_t>(cursor)));
        if (route.size() > kMaxPassageLength) return {};
      }
      return route;
    }
    for (GridPos direction : kCardinals) {
      const GridPos next{current.x + direction.x, current.y + direction.y};
      if (!within_envelope(next, home)) continue;
      const Material material = grid.at(next);
      if (material != Material::Air && !is_diggable(material)) continue;
      const int effort = material == Material::Root ? 18 : material == Material::Clay ? 14 : 10;
      const int variation = static_cast<int>(mix_seed(seed ^
          static_cast<std::uint64_t>((next.x / 4) + (next.y / 4) * Grid::kWidth)) % 3);
      const int candidate = cost + effort + variation;
      const std::size_t next_index = cell_index(next);
      if (candidate >= costs[next_index]) continue;
      costs[next_index] = candidate;
      parents[next_index] = static_cast<int>(index);
      open.push({candidate, next_index});
    }
  }
  return {};
}

std::vector<GridPos> passage_cells(const Passage& passage, GridPos home) {
  std::vector<GridPos> cells;
  const auto add = [&](GridPos cell) {
    if (within_envelope(cell, home) && std::find(cells.begin(), cells.end(), cell) == cells.end())
      cells.push_back(cell);
  };
  for (std::size_t i = 0; i < passage.route.size(); ++i) {
    add(passage.route[i]);
    // Both incoming and outgoing shoulders keep a bend cardinally connected at full width.
    for (int neighbor : {-1, 1}) {
      const auto n = static_cast<int>(i) + neighbor;
      if (n < 0 || n >= static_cast<int>(passage.route.size())) continue;
      const GridPos heading{passage.route[static_cast<std::size_t>(n)].x - passage.route[i].x,
                            passage.route[static_cast<std::size_t>(n)].y - passage.route[i].y};
      for (int side : {-1, 1}) add({passage.route[i].x - heading.y * side,
                                   passage.route[i].y + heading.x * side});
    }
  }
  return cells;
}

// A rock-enclosed pocket is left intact. Workers can open only the component of the chamber
// reachable from its entrance, including diggable ground, never an island through stone.
std::vector<GridPos> chamber_cells(const Grid& grid, const Room& room,
                                  const std::vector<int>& connected) {
  std::vector<GridPos> cells;
  std::queue<GridPos> pending;
  for (int y = -room.reach(); y <= room.reach(); ++y) {
    for (int x = -room.reach(); x <= room.reach(); ++x) {
      const GridPos cell{room.centre.x + x, room.centre.y + y};
      if (grid.in_bounds(cell) && room.contains(cell) && connected[cell_index(cell)] >= 0) {
        cells.push_back(cell);
        pending.push(cell);
      }
    }
  }
  while (!pending.empty()) {
    const GridPos cell = pending.front();
    pending.pop();
    for (GridPos step : kCardinals) {
      const GridPos next{cell.x + step.x, cell.y + step.y};
      if (!grid.in_bounds(next) || !room.contains(next) ||
          (grid.at(next) != Material::Air && !is_diggable(grid.at(next))) ||
          std::find(cells.begin(), cells.end(), next) != cells.end()) continue;
      cells.push_back(next);
      pending.push(next);
    }
  }
  return cells;
}

} // namespace

bool Room::contains(const GridPos cell) const {
  const int dx = cell.x - centre.x;
  const int dy = cell.y - centre.y;
  const int squared = dx * dx + dy * dy;
  const int base = static_cast<int>(radius);
  if (squared <= (base - 1) * (base - 1)) return true;      // the core is always inside
  if (squared > (base + 1) * (base + 1)) return false;      // and nothing reaches past the rim
  // Eight directions, each given its own bulge or pinch by where the room sits. The offset depends
  // only on the room, so the same cell always answers the same way and widening grows the same
  // shape outward.
  const unsigned sector = (dy > 0 ? 4U : 0U) | (dx > 0 ? 2U : 0U) |
                          (std::abs(dx) > std::abs(dy) ? 1U : 0U);
  const std::uint64_t noise = mix_seed(static_cast<std::uint64_t>(centre.x) * 0x9E3779B9ULL ^
                                       static_cast<std::uint64_t>(centre.y) * 0x85EBCA6BULL ^
                                       (static_cast<std::uint64_t>(sector) << 40U));
  // Mostly the plain radius, sometimes a cell more or less.
  const int offset = static_cast<int>(noise % 4ULL) == 0 ? 1 : static_cast<int>(noise % 4ULL) == 1 ? -1 : 0;
  const int limit = base + offset;
  return squared <= limit * limit;
}

bool within_envelope(const GridPos cell, const GridPos home) {
  return cell.y > kSurfaceFloor && cell.y < Grid::kHeight - 2 && cell.x > 1 &&
         cell.x < Grid::kWidth - 2 && manhattan(cell, home) < kDigRadius;
}

void NestPlan::restore(std::vector<Room> rooms, std::vector<Passage> passages) {
  rooms_ = std::move(rooms);
  if (rooms_.size() > kMaxRooms || passages.size() > kMaxPassages)
    throw std::invalid_argument("nest plan exceeds its limits");
  passages_ = std::move(passages);
  connected_revision_ = 0;
  work_revision_ = 0;
  claimed_cells_.clear();
}

void NestPlan::found(Room room) {
  work_revision_ = 0;
  if (rooms_.size() < kMaxRooms) rooms_.push_back(room);
}

std::optional<std::size_t> NestPlan::project() const {
  for (std::size_t index = 0; index < rooms_.size(); ++index) {
    if (!rooms_[index].complete) return index;
  }
  return std::nullopt;
}

int NestPlan::rooms_of(const RoomKind kind) const {
  return static_cast<int>(
      std::count_if(rooms_.begin(), rooms_.end(), [kind](const Room& r) { return r.kind == kind; }));
}

void NestPlan::refresh_connected(const Grid& grid, GridPos home) const {
  if (connected_revision_ == grid.navigation_revision() && connected_home_ == home) return;
  connected_ = air_distances(grid, home);
  connected_revision_ = grid.navigation_revision();
  connected_home_ = home;
}

std::optional<std::size_t> NestPlan::passage_project() const {
  const auto room = project();
  for (std::size_t i = 0; i < passages_.size(); ++i) {
    if (!passages_[i].complete &&
        (room ? passages_[i].room == static_cast<int>(*room) : passages_[i].room == -1)) return i;
  }
  return std::nullopt;
}

bool NestPlan::idle() const { return !project() && !passage_project(); }

Construction NestPlan::construction(const Grid& grid, GridPos home) const {
  Construction out;
  if (const auto room = project()) out.kind = rooms_[*room].kind == RoomKind::Nursery ?
      ConstructionKind::Nursery : ConstructionKind::Granary;
  else if (passage_project()) out.kind = ConstructionKind::CrossPassage;
  out.remaining_cells = work_cells(grid, home).size();
  out.complete_passages = static_cast<std::size_t>(std::count_if(passages_.begin(), passages_.end(),
      [](const Passage& passage) { return passage.complete && passage.route.size() > 1; }));
  return out;
}

const std::vector<GridPos>& NestPlan::work_cells(const Grid& grid, GridPos home) const {
  if (work_revision_ != grid.terrain_revision() || work_home_ != home) {
    work_cache_ = build_work_cells(grid, home);
    work_revision_ = grid.terrain_revision();
    work_home_ = home;
  }
  return work_cache_;
}

std::vector<GridPos> NestPlan::build_work_cells(const Grid& grid, const GridPos home) const {
  std::vector<GridPos> cells;
  const auto room_index = project();
  const auto passage_index = passage_project();
  if (!room_index && !passage_index) return cells;
  refresh_connected(grid, home);
  const auto add = [&](GridPos cell) {
    if (within_envelope(cell, home) && is_diggable(grid.at(cell)) &&
        std::find(cells.begin(), cells.end(), cell) == cells.end()) cells.push_back(cell);
  };
  if (passage_index) {
    for (GridPos cell : passage_cells(passages_[*passage_index], home)) add(cell);
  }
  if (room_index) {
    const Room& room = rooms_[*room_index];
    // Wait for the entrance rather than excavating a room via unrelated isolated air.
    for (GridPos cell : chamber_cells(grid, room, connected_)) add(cell);
    // Keep all approach cells before the room's cells, even at an irregular chamber mouth.
    std::stable_partition(cells.begin(), cells.end(), [&room](GridPos cell) { return !room.contains(cell); });
  }
  return cells;
}

std::optional<GridPos> NestPlan::claim(const Grid& grid, const GridPos home) {
  if (claimed_cells_.size() >= kDiggersPerProject) return std::nullopt;
  for (const GridPos cell : work_cells(grid, home)) {
    const bool exposed = std::any_of(kCardinals.begin(), kCardinals.end(), [&](GridPos step) {
      const GridPos next{cell.x + step.x, cell.y + step.y};
      return grid.in_bounds(next) && connected_[cell_index(next)] >= 0;
    });
    if (!exposed || std::find(claimed_cells_.begin(), claimed_cells_.end(), cell) != claimed_cells_.end()) continue;
    claimed_cells_.push_back(cell);
    return cell;
  }
  return std::nullopt;
}

bool NestPlan::site_is_clear(const Grid& grid, const GridPos home, const GridPos centre,
                             const int radius, const RoomKind kind) const {
  if (kind == RoomKind::Nursery && manhattan(centre, home) > kNurseryReach) return false;
  int blocked = 0;
  int total = 0;
  for (int dy = -radius - 2; dy <= radius + 2; ++dy) {
    for (int dx = -radius - 2; dx <= radius + 2; ++dx) {
      const GridPos cell{centre.x + dx, centre.y + dy};
      if (dx * dx + dy * dy > (radius + 2) * (radius + 2)) continue;
      if (!within_envelope(cell, home)) return false;
      ++total;
      if (!is_diggable(grid.at(cell)) && grid.at(cell) != Material::Air) ++blocked;
    }
  }
  // A little rock in the wall is character; a site that is mostly rock is not a room.
  if (total == 0 || blocked * 5 > total) return false;
  return std::none_of(rooms_.begin(), rooms_.end(), [&](const Room& other) {
    const int dx = centre.x - other.centre.x, dy = centre.y - other.centre.y;
    const int clearance = radius + other.radius + 3;
    return dx * dx + dy * dy < clearance * clearance;
  });
}

bool NestPlan::widen(const GridPos home, const RoomKind kind) {
  Room* best = nullptr;
  for (Room& room : rooms_) {
    if (room.kind != kind || room.radius >= kRoomMaxRadius) continue;
    const int widened = room.radius + 1;
    const bool crowded = std::any_of(rooms_.begin(), rooms_.end(), [&](const Room& other) {
      const int dx = room.centre.x - other.centre.x, dy = room.centre.y - other.centre.y;
      const int clearance = widened + other.radius + 3;
      return &room != &other && dx * dx + dy * dy < clearance * clearance;
    });
    if (crowded) continue; // A blocked near room must not prevent widening a usable farther room.
    bool fits = true;
    for (int dy = -widened - 1; dy <= widened + 1 && fits; ++dy) {
      for (int dx = -widened - 1; dx <= widened + 1; ++dx) {
        if (dx * dx + dy * dy <= (widened + 1) * (widened + 1) &&
            !within_envelope({room.centre.x + dx, room.centre.y + dy}, home)) { fits = false; break; }
      }
    }
    if (fits && (best == nullptr || manhattan(room.centre, home) < manhattan(best->centre, home))) best = &room;
  }
  if (best == nullptr) return false;
  ++best->radius;
  best->complete = false;
  return true;
}

bool NestPlan::site_new_room(const Grid& grid, const GridPos home, const std::uint64_t seed,
                             const RoomKind kind) {
  if (rooms_.size() >= kMaxRooms || passages_.size() >= kMaxPassages) return false;
  // Nearest workable ring first, so the nest grows outward gradually and corridors stay short.
  const int first_step = kind == RoomKind::Nursery ? 3 : 4;
  for (int step = first_step; step <= 20; ++step) {
    std::optional<GridPos> best;
    std::int64_t best_score = -1;
    std::vector<GridPos> best_route;
    for (std::size_t bearing = 0; bearing < kBearings.size(); ++bearing) {
      const GridPos centre{home.x + kBearings[bearing].x * step,
                           home.y + kBearings[bearing].y * step};
      if (!site_is_clear(grid, home, centre, kRoomStartRadius, kind)) continue;
      std::int64_t separation = 1'000;
      for (const Room& other : rooms_) {
        separation = std::min<std::int64_t>(separation, manhattan(centre, other.centre));
      }
      const std::int64_t score =
          separation * 4 +
          static_cast<std::int64_t>(mix_seed(seed ^ (bearing * 0x9E3779B9ULL) ^
                                             static_cast<std::uint64_t>(rooms_.size())) % 7ULL);
      if (score > best_score) {
        auto route = plan_route(grid, home, centre, connected_, seed);
        if (route.empty()) continue;
        best_score = score;
        best = centre;
        best_route = std::move(route);
      }
    }
    if (best) {
      passages_.push_back({std::move(best_route), static_cast<int>(rooms_.size()), false});
      rooms_.push_back({*best, static_cast<std::uint8_t>(kRoomStartRadius), kind, false});
      return true;
    }
  }
  return false;
}

bool NestPlan::plan_cross_passage(const Grid& grid, GridPos home, std::uint64_t seed) {
  const auto links = std::count_if(passages_.begin(), passages_.end(),
      [](const Passage& passage) { return passage.room == -1; });
  if (passages_.size() >= kMaxPassages || links >= static_cast<int>(rooms_.size() / 2)) return false;
  struct Candidate { GridPos from; GridPos to; int distance; int saving; };
  std::vector<Candidate> candidates;
  for (std::size_t a = 0; a < rooms_.size(); ++a) {
    if (!rooms_[a].complete || connected_[cell_index(rooms_[a].centre)] < 0) continue;
    const auto distances = air_distances(grid, rooms_[a].centre);
    for (std::size_t b = a + 1; b < rooms_.size(); ++b) {
      if (!rooms_[b].complete) continue;
      const int direct = manhattan(rooms_[a].centre, rooms_[b].centre);
      const int actual = distances[cell_index(rooms_[b].centre)];
      if (direct < 12 || direct > 40 || actual < direct + 12 || actual * 2 < direct * 3) continue;
      candidates.push_back({rooms_[a].centre, rooms_[b].centre, actual, actual - direct});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.saving > b.saving;
  });
  for (std::size_t i = 0; i < std::min<std::size_t>(8, candidates.size()); ++i) {
    const auto& candidate = candidates[i];
    auto route = plan_route(grid, home, candidate.to, connected_, seed, candidate.from);
    if (route.empty() || (route.size() - 1) * 4 > static_cast<std::size_t>(candidate.distance * 3)) continue;
    const auto new_cells = std::count_if(route.begin(), route.end(), [&](GridPos cell) {
      return is_diggable(grid.at(cell));
    });
    if (new_cells < 4) continue; // A genuine new connection, never free Work from existing air.
    passages_.push_back({std::move(route), -1, false});
    return true;
  }
  return false;
}

void NestPlan::update(const Grid& grid, const GridPos home, const std::uint64_t seed,
                      const bool wants_nursery, const bool wants_granary, const bool improve_routes) {
  work_revision_ = 0;
  refresh_connected(grid, home);
  for (Passage& passage : passages_) {
    if (passage.complete) continue;
    const auto cells = passage_cells(passage, home);
    passage.complete = std::none_of(cells.begin(), cells.end(), [&](GridPos cell) {
      return is_diggable(grid.at(cell));
    });
  }
  for (std::size_t index = 0; index < rooms_.size(); ++index) {
    Room& room = rooms_[index];
    if (room.complete) continue;
    const auto entrance = std::find_if(passages_.begin(), passages_.end(), [&](const Passage& p) {
      return p.room == static_cast<int>(index);
    });
    if (entrance == passages_.end()) {
      // Old saves retain every dug cell; only an unfinished project needs a new route.
      auto route = plan_route(grid, home, room.centre, connected_, seed);
      if (!route.empty() && passages_.size() < kMaxPassages) {
        Passage approach{std::move(route), static_cast<int>(index), false};
        const auto cells = passage_cells(approach, home);
        approach.complete = std::none_of(cells.begin(), cells.end(), [&](GridPos cell) {
          return is_diggable(grid.at(cell));
        });
        passages_.push_back(std::move(approach));
      }
    }
    const auto chamber = chamber_cells(grid, room, connected_);
    const bool approach_open = std::none_of(passages_.begin(), passages_.end(), [&](const Passage& p) {
      return p.room == static_cast<int>(index) && !p.complete;
    });
    room.complete = !chamber.empty() && approach_open &&
        std::none_of(chamber.begin(), chamber.end(), [&](GridPos cell) { return is_diggable(grid.at(cell)); });
  }
  if (project()) return;

  // Required capacity preempts optional connections. The old cross-passage stays saved and resumes
  // after the chamber; partially cut ground is never filled back in or counted as storage.
  if (wants_nursery) {
    if (!widen(home, RoomKind::Nursery))
      static_cast<void>(site_new_room(grid, home, seed, RoomKind::Nursery));
    if (project()) return;
  }
  if (wants_granary) {
    if (!widen(home, RoomKind::Granary))
      static_cast<void>(site_new_room(grid, home, seed ^ 0x6A11EDULL, RoomKind::Granary));
    if (project()) return;
  }
  if (improve_routes && !passage_project()) static_cast<void>(plan_cross_passage(grid, home, seed));
}

} // namespace ant::sim
