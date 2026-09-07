#include "sim/dig_plan.hpp"

#include "sim/rng.hpp"

#include <algorithm>
#include <cstdlib>

namespace ant::sim {
namespace {

constexpr std::array<GridPos, 4> kCardinals{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

std::uint64_t face_noise(const std::uint64_t seed, const std::size_t index, const GridPos cell,
                         const std::uint64_t salt) {
  return mix_seed(seed ^ (static_cast<std::uint64_t>(index) * 0x9E3779B9ULL) ^
                  (static_cast<std::uint64_t>(cell.x) * 0x85EBCA6BULL) ^
                  (static_cast<std::uint64_t>(cell.y) * 0xC2B2AE35ULL) ^ (salt * 0x27D4EB2FULL));
}

GridPos rotated(const GridPos heading, const int turn) {
  // turn is -1, 0 or +1 quarter turns; screen y grows downward but the rule is symmetric.
  if (turn == 0) return heading;
  return turn > 0 ? GridPos{-heading.y, heading.x} : GridPos{heading.y, -heading.x};
}

bool open(const Grid& grid, const GridPos cell) {
  return grid.in_bounds(cell) && grid.at(cell) == Material::Air;
}

} // namespace

bool within_envelope(const GridPos cell, const GridPos home) {
  return cell.y > kSurfaceFloor && cell.y < Grid::kHeight - 2 && cell.x > 1 &&
         cell.x < Grid::kWidth - 2 &&
         std::abs(cell.x - home.x) + std::abs(cell.y - home.y) < kDigRadius;
}

GridPos perpendicular(const GridPos heading) { return {-heading.y, heading.x}; }

void DigPlan::restore(const std::vector<DigFace>& faces) {
  faces_.fill(DigFace{});
  for (std::size_t index = 0; index < std::min(faces.size(), kMaxDigFaces); ++index) {
    faces_[index] = faces[index];
  }
  claims_.fill(0);
}

std::vector<DigFace> DigPlan::save() const {
  std::vector<DigFace> out;
  for (const DigFace& face : faces_) {
    if (face.active) out.push_back(face);
  }
  return out;
}

std::vector<GridPos> DigPlan::slice(const Grid& grid, const DigFace& face,
                                    const GridPos home) const {
  std::vector<GridPos> cells;
  if (!face.active) return cells;
  if (face.chamber) {
    // A rounded blob around the branch end, so a chamber does not read as a wider corridor.
    for (int dy = -kChamberRadius; dy <= kChamberRadius; ++dy) {
      for (int dx = -kChamberRadius; dx <= kChamberRadius; ++dx) {
        if (dx * dx + dy * dy > kChamberRadius * kChamberRadius) continue;
        const GridPos cell{face.anchor.x + dx, face.anchor.y + dy};
        if (!within_envelope(cell, home) || !grid.in_bounds(cell)) continue;
        if (is_diggable(grid.at(cell))) cells.push_back(cell);
      }
    }
    return cells;
  }
  const GridPos step{face.anchor.x + face.heading.x, face.anchor.y + face.heading.y};
  const GridPos side = perpendicular(face.heading);
  const int reach = (kCorridorWidth - 1) / 2;
  for (int k = -reach; k <= reach; ++k) {
    const GridPos cell{step.x + side.x * k, step.y + side.y * k};
    if (!within_envelope(cell, home) || !grid.in_bounds(cell)) continue;
    if (is_diggable(grid.at(cell))) cells.push_back(cell);
  }
  // A canonical order, so which cell a given worker takes does not depend on the sign of the
  // heading and two runs of the same colony assign the same grains.
  std::sort(cells.begin(), cells.end());
  return cells;
}

bool DigPlan::advance(const Grid& grid, const GridPos home, const std::uint64_t seed,
                      const std::size_t index) {
  DigFace& face = faces_[index];
  const GridPos step{face.anchor.x + face.heading.x, face.anchor.y + face.heading.y};
  // The face only moves on once its centreline cell is genuinely open. A blocked shoulder — a root
  // or the envelope edge — narrows the corridor rather than stalling it.
  if (!open(grid, step)) return false;
  face.anchor = step;
  ++face.length;
  face.blocked = 0;
  if (face.length >= kMaxBranchLength) {
    face.active = false;
    return true;
  }
  if (face.length % kHeadingRun != 0) return true;

  // Turn deliberately and rarely, and never toward the surface, so corridors bend and stay buried.
  const std::uint64_t noise = face_noise(seed, index, face.anchor, face.length);
  int turn = 0;
  if (noise % 100ULL < 34ULL) turn = (noise / 100ULL) % 2ULL == 0 ? 1 : -1;
  GridPos heading = rotated(face.heading, turn);
  const GridPos ahead{face.anchor.x + heading.x, face.anchor.y + heading.y};
  if (!within_envelope(ahead, home)) {
    for (const int alternative : {1, -1, 2}) {
      const GridPos candidate = rotated(face.heading, alternative == 2 ? 0 : alternative);
      const GridPos probe{face.anchor.x + candidate.x * (alternative == 2 ? -1 : 1),
                          face.anchor.y + candidate.y * (alternative == 2 ? -1 : 1)};
      if (within_envelope(probe, home)) {
        heading = alternative == 2 ? GridPos{-candidate.x, -candidate.y} : candidate;
        break;
      }
    }
  }
  face.heading = heading;
  return true;
}

void DigPlan::seed_face(const Grid& grid, const GridPos home, const std::uint64_t seed,
                        const std::size_t index, const std::vector<GridPos>& candidates) {
  if (candidates.empty()) return;
  // Prefer a start that is far from home and from the other faces, so branches separate instead of
  // all gnawing at the same wall.
  const GridPos* best = nullptr;
  std::int64_t best_score = -1;
  for (const GridPos& candidate : candidates) {
    if (!within_envelope(candidate, home)) continue;
    std::int64_t separation = 1'000;
    for (std::size_t other = 0; other < kMaxDigFaces; ++other) {
      if (!faces_[other].active) continue;
      separation = std::min<std::int64_t>(separation,
                                          std::abs(candidate.x - faces_[other].anchor.x) +
                                              std::abs(candidate.y - faces_[other].anchor.y));
    }
    const std::int64_t reach =
        std::abs(candidate.x - home.x) + std::abs(candidate.y - home.y);
    const std::int64_t score = separation * 4 + reach +
                               static_cast<std::int64_t>(face_noise(seed, index, candidate, 7) % 5ULL);
    if (score > best_score || (score == best_score && best != nullptr && candidate < *best)) {
      best_score = score;
      best = &candidate;
    }
  }
  if (best == nullptr) return;

  DigFace face{};
  face.anchor = *best;
  face.active = true;
  face.length = 0;
  // Drive toward whichever cardinal still has ground to give, breaking ties deterministically.
  GridPos chosen{0, 1};
  std::int64_t chosen_score = -1;
  for (std::size_t direction = 0; direction < kCardinals.size(); ++direction) {
    const GridPos heading = kCardinals[direction];
    const GridPos probe{face.anchor.x + heading.x, face.anchor.y + heading.y};
    if (!within_envelope(probe, home) || !grid.in_bounds(probe)) continue;
    if (!is_diggable(grid.at(probe))) continue;
    const std::int64_t score =
        (heading.y > 0 ? 6 : heading.y < 0 ? 0 : 3) +
        static_cast<std::int64_t>(face_noise(seed, index, probe, direction) % 4ULL);
    if (score > chosen_score) {
      chosen_score = score;
      chosen = heading;
    }
  }
  if (chosen_score < 0) return;
  face.heading = chosen;
  faces_[index] = face;
}

void DigPlan::update(const Grid& grid, const GridPos home, const std::uint64_t seed,
                     const std::vector<GridPos>& candidates, const bool wants_chamber) {
  for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
    DigFace& face = faces_[index];
    if (!face.active) continue;
    if (!within_envelope(face.anchor, home)) {
      face.active = false;
      continue;
    }
    if (face.chamber) {
      // A chamber is done when there is nothing diggable left inside its radius.
      if (slice(grid, face, home).empty()) face.active = false;
      continue;
    }
    if (slice(grid, face, home).empty()) {
      if (!advance(grid, home, seed, index)) {
        // Nothing to dig and the centreline is still closed: the face is walled in by material it
        // may not remove. Give it a bounded chance to turn before the slot is reused.
        if (++face.blocked >= kBlockedLimit) {
          face.active = false;
        } else if (face.blocked % 60 == 0) {
          face.heading = rotated(face.heading, face.blocked % 120 == 0 ? 1 : -1);
        }
      }
    }
  }

  // A branch end becomes a chamber only when the colony actually needs the room.
  if (wants_chamber) {
    for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
      DigFace& face = faces_[index];
      if (face.active && !face.chamber && face.length >= kChamberAt) {
        face.chamber = true;
        break;
      }
    }
  }

  // Branch from a face that has driven far enough, into a free slot, heading off to one side.
  for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
    const DigFace parent = faces_[index];
    if (!parent.active || parent.chamber || parent.length < kMinBranchLength) continue;
    if (parent.length % kMinBranchLength != 0) continue;
    for (std::size_t slot = 0; slot < kMaxDigFaces; ++slot) {
      if (faces_[slot].active) continue;
      const GridPos side = perpendicular(parent.heading);
      const bool flip = face_noise(seed, index, parent.anchor, 11) % 2ULL == 0;
      DigFace branch{};
      branch.anchor = parent.anchor;
      branch.heading = flip ? side : GridPos{-side.x, -side.y};
      branch.active = true;
      const GridPos probe{branch.anchor.x + branch.heading.x, branch.anchor.y + branch.heading.y};
      if (!within_envelope(probe, home)) break;
      faces_[slot] = branch;
      break;
    }
  }

  for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
    if (!faces_[index].active) seed_face(grid, home, seed, index, candidates);
  }
}

std::optional<DigPlan::Claim> DigPlan::claim(const Grid& grid, const GridPos home) {
  for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
    if (!faces_[index].active || claims_[index] >= kWorkersPerFace) continue;
    const std::vector<GridPos> cells = slice(grid, faces_[index], home);
    if (cells.empty()) continue;
    // Workers on the same face take different cells of the cross-section where they can, so two
    // excavators widen the corridor together instead of queueing on one grain.
    const GridPos cell = cells[std::min<std::size_t>(claims_[index], cells.size() - 1)];
    ++claims_[index];
    return Claim{cell, index};
  }
  return std::nullopt;
}

void DigPlan::release(const std::size_t face) {
  if (face < kMaxDigFaces && claims_[face] > 0) --claims_[face];
}
void DigPlan::clear_claims() { claims_.fill(0); }
void DigPlan::note_claim(const std::size_t face) {
  if (face < kMaxDigFaces && claims_[face] < 255) ++claims_[face];
}

bool DigPlan::idle() const {
  return std::none_of(faces_.begin(), faces_.end(), [](const DigFace& f) { return f.active; });
}

std::optional<std::size_t> DigPlan::face_of(const Grid& grid, const GridPos cell,
                                            const GridPos home) const {
  for (std::size_t index = 0; index < kMaxDigFaces; ++index) {
    if (!faces_[index].active) continue;
    const std::vector<GridPos> cells = slice(grid, faces_[index], home);
    if (std::find(cells.begin(), cells.end(), cell) != cells.end()) return index;
  }
  return std::nullopt;
}

} // namespace ant::sim
