#include "sim/nest_plan.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

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

// A cell a worker can actually start on: still solid, and with somewhere to stand beside it.
bool exposed(const Grid& grid, const GridPos cell) {
  if (!grid.in_bounds(cell) || !is_diggable(grid.at(cell))) return false;
  return std::any_of(kCardinals.begin(), kCardinals.end(), [&](const GridPos step) {
    return grid.walkable({cell.x + step.x, cell.y + step.y});
  });
}

// The run of solid ground between the nest and a room centre, from the nest end outward. Walking
// back toward home and stopping at the first open cell finds exactly the ground still in the way.
std::vector<GridPos> corridor_centreline(const Grid& grid, const GridPos home,
                                         const GridPos centre) {
  std::vector<GridPos> line;
  GridPos cursor = centre;
  while (cursor != home && line.size() < 256) {
    const int dx = home.x - cursor.x;
    const int dy = home.y - cursor.y;
    if (std::abs(dx) >= std::abs(dy)) cursor.x += dx > 0 ? 1 : -1;
    else cursor.y += dy > 0 ? 1 : -1;
    if (!grid.in_bounds(cursor)) break;
    if (grid.at(cursor) == Material::Air) break; // reached the nest
    line.push_back(cursor);
  }
  std::reverse(line.begin(), line.end());
  return line;
}

} // namespace

bool Room::contains(const GridPos cell) const {
  const int dx = cell.x - centre.x;
  const int dy = cell.y - centre.y;
  return dx * dx + dy * dy <= static_cast<int>(radius) * static_cast<int>(radius);
}

bool within_envelope(const GridPos cell, const GridPos home) {
  return cell.y > kSurfaceFloor && cell.y < Grid::kHeight - 2 && cell.x > 1 &&
         cell.x < Grid::kWidth - 2 && manhattan(cell, home) < kDigRadius;
}

void NestPlan::restore(std::vector<Room> rooms) {
  rooms_ = std::move(rooms);
  if (rooms_.size() > kMaxRooms) rooms_.resize(kMaxRooms);
  claims_ = 0;
}

void NestPlan::found(Room room) {
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

std::vector<GridPos> NestPlan::work_cells(const Grid& grid, const GridPos home) const {
  std::vector<GridPos> cells;
  const std::optional<std::size_t> index = project();
  if (!index) return cells;
  const Room& room = rooms_[*index];

  const std::vector<GridPos> line = corridor_centreline(grid, home, room.centre);
  const int reach = (kCorridorWidth - 1) / 2;
  for (std::size_t step = 0; step < line.size(); ++step) {
    const GridPos ahead = step + 1 < line.size() ? line[step + 1] : room.centre;
    const GridPos heading{ahead.x - line[step].x, ahead.y - line[step].y};
    const GridPos side{-heading.y, heading.x};
    for (int offset = -reach; offset <= reach; ++offset) {
      const GridPos cell{line[step].x + side.x * offset, line[step].y + side.y * offset};
      if (!within_envelope(cell, home) || !is_diggable(grid.at(cell))) continue;
      if (std::find(cells.begin(), cells.end(), cell) == cells.end()) cells.push_back(cell);
    }
  }

  // Then the room itself, opening outward from where the corridor arrives.
  std::vector<GridPos> chamber;
  for (int dy = -room.radius; dy <= room.radius; ++dy) {
    for (int dx = -room.radius; dx <= room.radius; ++dx) {
      const GridPos cell{room.centre.x + dx, room.centre.y + dy};
      if (!room.contains(cell) || !within_envelope(cell, home)) continue;
      if (!is_diggable(grid.at(cell))) continue;
      // The corridor's last stretch can already lie inside the room; plan each cell once.
      if (std::find(cells.begin(), cells.end(), cell) != cells.end()) continue;
      chamber.push_back(cell);
    }
  }
  std::sort(chamber.begin(), chamber.end(), [&room](const GridPos a, const GridPos b) {
    const int reach_a = manhattan(a, room.centre);
    const int reach_b = manhattan(b, room.centre);
    return reach_a != reach_b ? reach_a < reach_b : a < b;
  });
  cells.insert(cells.end(), chamber.begin(), chamber.end());
  return cells;
}

std::optional<GridPos> NestPlan::claim(const Grid& grid, const GridPos home) {
  if (claims_ >= kDiggersPerProject) return std::nullopt;
  int skipped = 0;
  for (const GridPos cell : work_cells(grid, home)) {
    if (!exposed(grid, cell)) continue;
    if (skipped++ < claims_) continue;
    ++claims_;
    return cell;
  }
  return std::nullopt;
}

bool NestPlan::site_is_clear(const Grid& grid, const GridPos home, const GridPos centre,
                             const int radius, const RoomKind kind) const {
  if (kind == RoomKind::Nursery && manhattan(centre, home) > kNurseryReach) return false;
  int blocked = 0;
  int total = 0;
  for (int dy = -radius - 1; dy <= radius + 1; ++dy) {
    for (int dx = -radius - 1; dx <= radius + 1; ++dx) {
      const GridPos cell{centre.x + dx, centre.y + dy};
      if (dx * dx + dy * dy > (radius + 1) * (radius + 1)) continue;
      if (!within_envelope(cell, home)) return false;
      ++total;
      if (!is_diggable(grid.at(cell)) && grid.at(cell) != Material::Air) ++blocked;
    }
  }
  // A little rock in the wall is character; a site that is mostly rock is not a room.
  if (total == 0 || blocked * 5 > total) return false;
  return std::none_of(rooms_.begin(), rooms_.end(), [&](const Room& other) {
    return manhattan(centre, other.centre) < radius + other.radius + 3;
  });
}

bool NestPlan::widen(const GridPos home, const RoomKind kind) {
  Room* best = nullptr;
  for (Room& room : rooms_) {
    if (room.kind != kind || room.radius >= kRoomMaxRadius) continue;
    if (best == nullptr || manhattan(room.centre, home) < manhattan(best->centre, home)) best = &room;
  }
  if (best == nullptr) return false;
  const int widened = best->radius + 1;
  // The overlap check would compare the room with itself, so ask about the ring around it directly.
  const bool crowded = std::any_of(rooms_.begin(), rooms_.end(), [&](const Room& other) {
    return &other != best && manhattan(best->centre, other.centre) < widened + other.radius + 3;
  });
  if (crowded) return false;
  for (int dy = -widened; dy <= widened; ++dy) {
    for (int dx = -widened; dx <= widened; ++dx) {
      if (dx * dx + dy * dy > widened * widened) continue;
      if (!within_envelope({best->centre.x + dx, best->centre.y + dy}, home)) return false;
    }
  }
  best->radius = static_cast<std::uint8_t>(widened);
  best->complete = false;
  return true;
}

bool NestPlan::site_new_room(const Grid& grid, const GridPos home, const std::uint64_t seed,
                             const RoomKind kind) {
  if (rooms_.size() >= kMaxRooms) return false;
  // Nearest workable ring first, so the nest grows outward gradually and corridors stay short.
  const int first_step = kind == RoomKind::Nursery ? 3 : 4;
  for (int step = first_step; step <= 20; ++step) {
    std::optional<GridPos> best;
    std::int64_t best_score = -1;
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
        best_score = score;
        best = centre;
      }
    }
    if (best) {
      rooms_.push_back({*best, static_cast<std::uint8_t>(kRoomStartRadius), kind, false});
      return true;
    }
  }
  return false;
}

void NestPlan::update(const Grid& grid, const GridPos home, const std::uint64_t seed,
                      const bool wants_nursery, const bool wants_granary) {
  for (Room& room : rooms_) {
    if (room.complete) continue;
    const bool corridor_open = corridor_centreline(grid, home, room.centre).empty();
    bool carved = true;
    for (int dy = -room.radius; dy <= room.radius && carved; ++dy) {
      for (int dx = -room.radius; dx <= room.radius; ++dx) {
        const GridPos cell{room.centre.x + dx, room.centre.y + dy};
        if (!room.contains(cell) || !within_envelope(cell, home)) continue;
        if (is_diggable(grid.at(cell))) { carved = false; break; }
      }
    }
    room.complete = carved && corridor_open;
  }
  if (project()) return; // finish one room before starting another

  // Widen the room the colony already has before cutting a new one: a slightly bigger chamber is
  // cheaper than a fresh corridor, which is also how a real nest grows.
  if (wants_nursery) {
    if (!widen(home, RoomKind::Nursery)) {
      static_cast<void>(site_new_room(grid, home, seed, RoomKind::Nursery));
    }
    if (project()) return;
  }
  if (wants_granary && !widen(home, RoomKind::Granary)) {
    static_cast<void>(site_new_room(grid, home, seed ^ 0x6A11EDULL, RoomKind::Granary));
  }
}

} // namespace ant::sim
