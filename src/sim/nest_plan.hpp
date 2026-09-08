#pragma once

#include "sim/grid.hpp"
#include "sim/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ant::sim {

// The colony digs for a reason. Every excavation belongs to one room the colony has decided it
// needs, its access passage, or a cross-passage that shortens an existing journey.
//
// This owns intent only. It never edits the grid: workers still walk to a planned cell, cut it, and
// carry the grain out. A cell only becomes Air because somebody excavated it.

// Excavation envelope: never break the surface, and stay within reach of home.
inline constexpr int kSurfaceFloor = 34;
inline constexpr int kDigRadius = 112;
// Nurses have to walk to the brood, so brood rooms stay within reach of the queen.
inline constexpr int kNurseryReach = 56;
// A room opens at this radius and may be widened to this one before the colony sites another.
// Chambers are small: a colony ends up with many pockets joined by passages rather than a few
// caverns. This is a game geometry rule, not a species-specific biological claim.
inline constexpr int kRoomStartRadius = 3;
inline constexpr int kRoomMaxRadius = 5;
// Solid ground every chamber keeps between its own rim and the rim of every other chamber. Two
// pockets closer than this are one cavity with a waist, not two rooms, so the colony refuses to
// site or widen a room that would come nearer. Nothing else does more to decide the shape of the
// nest: the wider this is, the further apart the chambers sit and the more corridor the colony has
// to cut to join them, which is what turns a compact warren into a long branching farm.
inline constexpr int kRoomSeparation = 9;
// Corridor cross-section, in cells, measured across its run.
inline constexpr int kCorridorWidth = 3;
inline constexpr std::size_t kMaxRooms = 36;
inline constexpr std::size_t kMaxPassages = kMaxRooms * 2;
inline constexpr std::size_t kMaxPassageLength = 256;
// Workers that may cut the same project at once. A chamber now sits at the end of a real corridor
// rather than next door, and every grain cut has to be carried up to the surface, so a bigger crew
// is what keeps the nest growing at the rate it used to.
inline constexpr int kDiggersPerProject = 6;

enum class RoomKind : std::uint8_t { Nursery, Granary };

struct Room {
  GridPos centre{};
  std::uint8_t radius{kRoomStartRadius};
  RoomKind kind{RoomKind::Nursery};
  // Set once nothing inside the room is diggable any more. A rock the colony cannot cut simply
  // stays, and the room is finished around it.
  bool complete{};

  // Rooms are not discs. Each one bulges and pinches by a cell per direction, decided once from
  // where it sits, so a chamber reads as a pocket somebody dug rather than a stamped circle.
  [[nodiscard]] bool contains(GridPos cell) const;
  // The furthest a cell of this room can be from its centre, for loop bounds.
  [[nodiscard]] int reach() const { return radius + 1; }

  friend bool operator==(const Room&, const Room&) = default;
};

[[nodiscard]] bool within_envelope(GridPos cell, GridPos home);

// An immutable route, including both anchors. -1 denotes a cross-passage; otherwise room is the
// destination room's stable vector index. Width is intent too: opening the centre never erases
// the unfinished shoulders. Rock in a shoulder stays as a natural pinch point.
struct Passage {
  std::vector<GridPos> route;
  int room{-1};
  bool complete{};
  friend bool operator==(const Passage&, const Passage&) = default;
};

enum class ConstructionKind : std::uint8_t { None, Nursery, Granary, CrossPassage };
struct Construction {
  ConstructionKind kind{ConstructionKind::None};
  std::size_t remaining_cells{};
  std::size_t complete_passages{};
};

class NestPlan {
public:
  [[nodiscard]] const std::vector<Room>& rooms() const { return rooms_; }
  [[nodiscard]] const std::vector<Passage>& passages() const { return passages_; }
  void restore(std::vector<Room> rooms, std::vector<Passage> passages = {});
  void found(Room room);

  // Marks finished rooms complete, then, when the colony is short of space, either widens the
  // nearest room of that kind or sites a new one one ring further out. One project at a time, so
  // the colony finishes a room before starting the next.
  void update(const Grid& grid, GridPos home, std::uint64_t seed, bool wants_nursery,
              bool wants_granary, bool improve_routes = false);

  // The cell an excavator should cut next: the first exposed face of the project that no other
  // worker has taken this tick. Nothing when the project has no reachable face left.
  [[nodiscard]] std::optional<GridPos> claim(const Grid& grid, GridPos home);
  void clear_claims() { claimed_cells_.clear(); }
  void note_claim(GridPos cell) { if (claimed_cells_.size() < kDiggersPerProject) claimed_cells_.push_back(cell); }
  [[nodiscard]] bool idle() const;
  [[nodiscard]] Construction construction(const Grid& grid, GridPos home) const;
  // Index of the room being dug, if any.
  [[nodiscard]] std::optional<std::size_t> project() const;
  [[nodiscard]] int rooms_of(RoomKind kind) const;

  // Cells the project still needs opened, in dig order: the corridor from the nest outward, then
  // the room itself. Exposed publicly so the excavation order can be asserted directly.
  [[nodiscard]] const std::vector<GridPos>& work_cells(const Grid& grid, GridPos home) const;

private:
  [[nodiscard]] std::vector<GridPos> build_work_cells(const Grid& grid, GridPos home) const;
  [[nodiscard]] bool site_is_clear(const Grid& grid, GridPos home, GridPos centre, int radius,
                                   RoomKind kind) const;
  [[nodiscard]] bool widen(GridPos home, RoomKind kind);
  [[nodiscard]] bool site_new_room(const Grid& grid, GridPos home, std::uint64_t seed,
                                   RoomKind kind);
  bool plan_cross_passage(const Grid& grid, GridPos home, std::uint64_t seed);
  [[nodiscard]] std::optional<std::size_t> passage_project() const;
  void refresh_connected(const Grid& grid, GridPos home) const;

  std::vector<Room> rooms_;
  std::vector<Passage> passages_;
  // Derived cardinal connectivity, never serialized. Shared by all claims on one topology.
  mutable std::vector<GridPos> work_cache_;
  mutable std::uint64_t work_revision_{};
  mutable GridPos work_home_{};
  mutable std::vector<int> connected_;
  mutable std::uint64_t connected_revision_{};
  mutable GridPos connected_home_{};
  std::vector<GridPos> claimed_cells_;
};

} // namespace ant::sim
