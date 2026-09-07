#pragma once

#include "game/progression.hpp"
#include "game/snapshot.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ant::game {

class Session;

enum class TraitBranch : std::uint8_t { Vigor, Industry };

// ECONOMY: an explicit table, never an exponential formula.
inline constexpr std::array<std::int64_t, 4> kTraitCosts{3, 7, 15, 32};
inline constexpr std::uint8_t kMaxTraitTier = 4;

// Exact floor of the square root over the whole validated domain, without floating point.
[[nodiscard]] std::uint64_t integer_sqrt(std::uint64_t value);

struct Payout {
  std::int64_t total{};
  std::int64_t birth_bonus{};
  std::int64_t queen_bonus{};
};

[[nodiscard]] Payout flight_payout(std::uint64_t births, int live_winged_queens);

enum class FlightBlock : std::uint8_t {
  None,
  NoActiveRun,
  NotMature,
  TooFewWingedQueens,
  QueenDead,
  AssistedRun
};

[[nodiscard]] const char* flight_block_reason(FlightBlock block);

struct FlightPreview {
  bool eligible{};
  FlightBlock block{FlightBlock::None};
  Payout payout;
  std::uint64_t births{};
  int live_winged_queens{};
  int living_workers{};
  bool mature{};
  sim::Tick run_ticks{};
  // The next threshold that would raise the payout, derived from the rules rather than hard-coded
  // hint text. Zero means no further step exists.
  std::uint64_t next_birth_threshold{};
  int next_queen_threshold{};
};

[[nodiscard]] FlightPreview preview_flight(const Session& session);

// A durable action produces one candidate profile for the caller to commit. Nothing is applied to
// the live session until that commit is known to have succeeded.
struct TransactionResult {
  bool accepted{};
  std::string error;
  std::optional<ProfileSnapshot> candidate;
};

// Revalidates the run and the caller's view of the profile, so a repeated or stale command cannot
// credit a second payout or apply to a different run.
[[nodiscard]] TransactionResult prepare_flight(const ProfileSnapshot& current,
                                               const Session& session,
                                               std::string_view expected_run_id,
                                               std::uint64_t expected_revision);

[[nodiscard]] TransactionResult prepare_trait_purchase(const ProfileSnapshot& current,
                                                       TraitBranch branch,
                                                       std::uint64_t expected_revision);

// Founds the next colony from the between-runs profile: a new generation, a new run id, and the
// owned traits applied once at founding.
[[nodiscard]] TransactionResult prepare_new_run(const ProfileSnapshot& current, std::uint64_t seed,
                                                const ProgressionConfig& content,
                                                std::uint64_t expected_revision);

// Ends a run with no reward. Requires explicit player confirmation at the call site.
[[nodiscard]] TransactionResult prepare_abandon_run(const ProfileSnapshot& current,
                                                    std::uint64_t expected_revision);

[[nodiscard]] std::uint8_t trait_tier(const MetaSnapshot& meta, TraitBranch branch);
[[nodiscard]] std::int64_t next_trait_cost(const MetaSnapshot& meta, TraitBranch branch);
[[nodiscard]] const char* trait_branch_name(TraitBranch branch);
[[nodiscard]] sim::TraitModifiers trait_modifiers(const MetaSnapshot& meta);

} // namespace ant::game
