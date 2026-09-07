#pragma once

#include "sim/grid.hpp"
#include "sim/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ant::sim {

// The colony digs for a reason. Every excavation belongs to one room the colony has decided it
// needs — a brood room or a granary — and to the corridor that reaches it, so a finished nest is
// chambers joined by passages rather than wandering lines.
//
// This owns intent only. It never edits the grid: workers still walk to a planned cell, cut it, and
// carry the grain out. A cell only becomes Air because somebody excavated it.

// Excavation envelope: never break the surface, and stay within reach of home.
inline constexpr int kSurfaceFloor = 34;
inline constexpr int kDigRadius = 96;
// Nurses have to walk to the brood, so brood rooms stay within reach of the queen.
inline constexpr int kNurseryReach = 46;
// A room opens at this radius and may be widened to this one before the colony sites another.
inline constexpr int kRoomStartRadius = 5;
inline constexpr int kRoomMaxRadius = 8;
// Corridor cross-section, in cells, measured across its run.
inline constexpr int kCorridorWidth = 3;
inline constexpr std::size_t kMaxRooms = 14;
// Workers that may cut the same project at once.
inline constexpr int kDiggersPerProject = 4;

enum class RoomKind : std::uint8_t { Nursery, Granary };

struct Room {
  GridPos centre{};
  std::uint8_t radius{kRoomStartRadius};
  RoomKind kind{RoomKind::Nursery};
  // Set once nothing inside the room is diggable any more. A rock the colony cannot cut simply
  // stays, and the room is finished around it.
  bool complete{};

  [[nodiscard]] bool contains(GridPos cell) const;

  friend bool operator==(const Room&, const Room&) = default;
};

[[nodiscard]] bool within_envelope(GridPos cell, GridPos home);

class NestPlan {
public:
  [[nodiscard]] const std::vector<Room>& rooms() const { return rooms_; }
  void restore(std::vector<Room> rooms);
  void found(Room room);

  // Marks finished rooms complete, then, when the colony is short of space, either widens the
  // nearest room of that kind or sites a new one one ring further out. One project at a time, so
  // the colony finishes a room before starting the next.
  void update(const Grid& grid, GridPos home, std::uint64_t seed, bool wants_nursery,
              bool wants_granary);

  // The cell an excavator should cut next: the first exposed face of the project that no other
  // worker has taken this tick. Nothing when the project has no reachable face left.
  [[nodiscard]] std::optional<GridPos> claim(const Grid& grid, GridPos home);
  void clear_claims() { claims_ = 0; }
  void note_claim() { ++claims_; }
  [[nodiscard]] bool idle() const { return !project().has_value(); }
  // Index of the room being dug, if any.
  [[nodiscard]] std::optional<std::size_t> project() const;
  [[nodiscard]] int rooms_of(RoomKind kind) const;

  // Cells the project still needs opened, in dig order: the corridor from the nest outward, then
  // the room itself. Exposed publicly so the excavation order can be asserted directly.
  [[nodiscard]] std::vector<GridPos> work_cells(const Grid& grid, GridPos home) const;

private:
  [[nodiscard]] bool site_is_clear(const Grid& grid, GridPos home, GridPos centre, int radius,
                                   RoomKind kind) const;
  [[nodiscard]] bool widen(GridPos home, RoomKind kind);
  [[nodiscard]] bool site_new_room(const Grid& grid, GridPos home, std::uint64_t seed,
                                   RoomKind kind);

  std::vector<Room> rooms_;
  int claims_{};
};

} // namespace ant::sim
