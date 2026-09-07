#pragma once

#include "sim/grid.hpp"
#include "sim/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ant::sim {

// Excavation is planned as a few persistent faces rather than a ranked heap of loose cells. A face
// keeps a heading for several cells at a time, so seeded variation bends a corridor instead of
// scattering pits, and a colony ends up with passages and chambers rather than one widening cavity.
//
// This owns intent only. It never edits the grid: workers still walk to a planned cell, dig it, and
// carry the grain out. A cell only becomes Air because somebody excavated it.

inline constexpr std::size_t kMaxDigFaces = 4;
inline constexpr int kWorkersPerFace = 2;
// Corridor cross-section, in cells, measured across the heading.
inline constexpr int kCorridorWidth = 3;
// Cells a face drives before it may consider turning, so noise produces bends rather than jitter.
inline constexpr int kHeadingRun = 4;
// Completed length before a face may spawn a branch, and the length at which it retires.
inline constexpr int kMinBranchLength = 14;
inline constexpr int kMaxBranchLength = 44;
// Ticks a face may fail to make progress before it is retired and its slot reused.
inline constexpr int kBlockedLimit = 240;
// Excavation envelope: never break the surface, and stay within reach of home.
inline constexpr int kSurfaceFloor = 34;
inline constexpr int kDigRadius = 96;
// A chamber is widened around the end of a branch when the colony is short of space.
inline constexpr int kChamberRadius = 3;
inline constexpr int kChamberAt = 10;

struct DigFace {
  // Centreline cell most recently opened. Always Air once the face has advanced at least once.
  GridPos anchor{};
  // Cardinal unit vector the face is driving along.
  GridPos heading{0, 1};
  std::uint16_t length{};
  std::uint16_t blocked{};
  bool chamber{};
  bool active{};

  [[nodiscard]] bool operator==(const DigFace&) const = default;
};

[[nodiscard]] bool within_envelope(GridPos cell, GridPos home);
// Perpendicular to a cardinal heading, used to lay out a corridor's cross-section.
[[nodiscard]] GridPos perpendicular(GridPos heading);

class DigPlan {
public:
  [[nodiscard]] const std::array<DigFace, kMaxDigFaces>& faces() const { return faces_; }
  void restore(const std::vector<DigFace>& faces);
  [[nodiscard]] std::vector<DigFace> save() const;

  // Cells the face wants opened next: the cross-section one step along its heading. Cells outside
  // the envelope or already open are omitted, so the result is exactly the work still outstanding.
  [[nodiscard]] std::vector<GridPos> slice(const Grid& grid, const DigFace& face, GridPos home) const;

  // Retires finished or stuck faces, advances the ones whose slice is open, branches where there is
  // room, and seeds idle slots from `candidates` (connected Air cells that touch diggable ground).
  void update(const Grid& grid, GridPos home, std::uint64_t seed,
              const std::vector<GridPos>& candidates, bool wants_chamber);

  // Hands a worker a cell to dig, respecting the per-face worker cap. Returns the face index too so
  // the claim can be released again.
  struct Claim { GridPos cell; std::size_t face; };
  [[nodiscard]] std::optional<Claim> claim(const Grid& grid, GridPos home);

  void release(std::size_t face);
  void clear_claims();
  void note_claim(std::size_t face);
  [[nodiscard]] bool idle() const;
  // Index of the face that planned `cell`, so a worker's existing target can be matched back.
  [[nodiscard]] std::optional<std::size_t> face_of(const Grid& grid, GridPos cell, GridPos home) const;

private:
  [[nodiscard]] bool advance(const Grid& grid, GridPos home, std::uint64_t seed, std::size_t index);
  void seed_face(const Grid& grid, GridPos home, std::uint64_t seed, std::size_t index,
                 const std::vector<GridPos>& candidates);

  std::array<DigFace, kMaxDigFaces> faces_{};
  std::array<std::uint8_t, kMaxDigFaces> claims_{};
};

} // namespace ant::sim
