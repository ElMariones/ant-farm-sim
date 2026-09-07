#include "game/prestige.hpp"

#include "game/session.hpp"

#include <algorithm>
#include <limits>

namespace ant::game {
namespace {

constexpr std::uint64_t kBirthStep = 150;
constexpr int kQueenStep = 3;
constexpr int kMaxCountedQueens = 10;

TransactionResult refuse(std::string error) { return {false, std::move(error), std::nullopt}; }

// Every durable action revalidates the caller's view of the profile before building a candidate.
std::optional<std::string> stale(const ProfileSnapshot& current, const std::uint64_t expected) {
  if (current.revision != expected) {
    return "the profile changed since this action was offered (revision " +
           std::to_string(current.revision) + ", expected " + std::to_string(expected) + ")";
  }
  return std::nullopt;
}

} // namespace

std::uint64_t integer_sqrt(const std::uint64_t value) {
  if (value < 2) return value;
  // Bit-by-bit restoring square root: exact for the whole 64-bit domain, no floating point.
  std::uint64_t result = 0;
  std::uint64_t bit = std::uint64_t{1} << 62;
  std::uint64_t remainder = value;
  while (bit > remainder) bit >>= 2;
  while (bit != 0) {
    if (remainder >= result + bit) {
      remainder -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

Payout flight_payout(const std::uint64_t births, const int live_winged_queens) {
  Payout payout;
  payout.birth_bonus = static_cast<std::int64_t>(integer_sqrt(births / kBirthStep));
  payout.queen_bonus = std::max(0, std::min(live_winged_queens, kMaxCountedQueens)) / kQueenStep;
  payout.total = 2 + payout.birth_bonus + payout.queen_bonus;
  return payout;
}

const char* flight_block_reason(const FlightBlock block) {
  switch (block) {
  case FlightBlock::None: return "Ready to fly";
  case FlightBlock::NoActiveRun: return "No colony is running";
  case FlightBlock::NotMature: return "The colony is not mature yet";
  case FlightBlock::TooFewWingedQueens: return "At least three winged queens must be alive";
  case FlightBlock::QueenDead: return "The founding queen has died";
  case FlightBlock::AssistedRun: return "This run was assisted and cannot fly";
  }
  return "Flight unavailable";
}

FlightPreview preview_flight(const Session& session) {
  const sim::World& world = session.world();
  FlightPreview preview;
  preview.births = world.stats().workers_born;
  preview.live_winged_queens = world.live_winged_queens();
  preview.living_workers = world.living_workers();
  preview.mature = world.mature();
  preview.run_ticks = world.tick();
  preview.payout = flight_payout(preview.births, preview.live_winged_queens);

  // The next step that would actually raise the payout, computed from the rules.
  const std::uint64_t owned_steps = integer_sqrt(preview.births / kBirthStep);
  const std::uint64_t next_step = owned_steps + 1;
  preview.next_birth_threshold = kBirthStep * next_step * next_step;
  const int counted = std::min(preview.live_winged_queens, kMaxCountedQueens);
  preview.next_queen_threshold =
      counted >= kMaxCountedQueens ? 0 : (counted / kQueenStep + 1) * kQueenStep;

  if (!world.mature()) preview.block = FlightBlock::NotMature;
  else if (!world.queen_alive()) preview.block = FlightBlock::QueenDead;
  else if (preview.live_winged_queens < 3) preview.block = FlightBlock::TooFewWingedQueens;
  else if (session.assisted()) preview.block = FlightBlock::AssistedRun;
  preview.eligible = preview.block == FlightBlock::None;
  return preview;
}

TransactionResult prepare_flight(const ProfileSnapshot& current, const Session& session,
                                 const std::string_view expected_run_id,
                                 const std::uint64_t expected_revision) {
  if (const auto error = stale(current, expected_revision)) return refuse(*error);
  if (current.phase != ProfilePhase::ActiveRun || !current.run) {
    return refuse("this profile has no active run to end");
  }
  if (current.run->run_id != expected_run_id) {
    return refuse("this flight command belongs to a different run");
  }
  // A receipt for this run already exists, so the payout was credited by an earlier commit.
  if (current.last_flight_receipt && current.last_flight_receipt->run_id == current.run->run_id) {
    return refuse("this run has already been credited");
  }

  const FlightPreview preview = preview_flight(session);
  if (!preview.eligible) return refuse(flight_block_reason(preview.block));

  ProfileSnapshot candidate = current;
  const std::int64_t payout = preview.payout.total;
  if (payout > std::numeric_limits<std::int64_t>::max() - candidate.meta.legacy_earned_total ||
      payout > std::numeric_limits<std::int64_t>::max() - candidate.meta.legacy_wallet) {
    return refuse("Legacy total would overflow");
  }
  candidate.meta.legacy_wallet += payout;
  candidate.meta.legacy_earned_total += payout;
  ++candidate.meta.successful_flights;
  candidate.last_flight_receipt =
      FlightReceipt{current.run->run_id, payout, preview.births, preview.live_winged_queens};
  candidate.phase = ProfilePhase::BetweenRuns;
  candidate.run.reset();
  return {true, {}, std::move(candidate)};
}

std::uint8_t trait_tier(const MetaSnapshot& meta, const TraitBranch branch) {
  return branch == TraitBranch::Vigor ? meta.vigor_tier : meta.industry_tier;
}

std::int64_t next_trait_cost(const MetaSnapshot& meta, const TraitBranch branch) {
  const std::uint8_t tier = trait_tier(meta, branch);
  if (tier >= kMaxTraitTier) return -1;
  return kTraitCosts[tier];
}

const char* trait_branch_name(const TraitBranch branch) {
  return branch == TraitBranch::Vigor ? "Vigor" : "Industry";
}

sim::TraitModifiers trait_modifiers(const MetaSnapshot& meta) {
  return {meta.vigor_tier, meta.industry_tier};
}

TransactionResult prepare_trait_purchase(const ProfileSnapshot& current, const TraitBranch branch,
                                         const std::uint64_t expected_revision) {
  if (const auto error = stale(current, expected_revision)) return refuse(*error);
  if (current.phase != ProfilePhase::BetweenRuns) {
    return refuse("permanent traits can only be bought between runs");
  }
  const std::uint8_t tier = trait_tier(current.meta, branch);
  if (tier >= kMaxTraitTier) {
    return refuse(std::string(trait_branch_name(branch)) + " is already complete");
  }
  const std::int64_t cost = kTraitCosts[tier];
  if (current.meta.legacy_wallet < cost) {
    return refuse("not enough Genetic Legacy (" + std::to_string(cost) + " needed)");
  }

  ProfileSnapshot candidate = current;
  candidate.meta.legacy_wallet -= cost;
  candidate.meta.legacy_spent_total += cost;
  // Tiers are linear inside a branch, so buying tier N implies owning 1..N-1 already.
  if (branch == TraitBranch::Vigor) ++candidate.meta.vigor_tier;
  else ++candidate.meta.industry_tier;
  return {true, {}, std::move(candidate)};
}

TransactionResult prepare_new_run(const ProfileSnapshot& current, const std::uint64_t seed,
                                  const ProgressionConfig& content,
                                  const std::uint64_t expected_revision) {
  if (const auto error = stale(current, expected_revision)) return refuse(*error);
  if (current.phase != ProfilePhase::BetweenRuns || current.run) {
    return refuse("a run is already active");
  }
  std::string validation_error;
  if (!validate_progression(content, validation_error)) return refuse(validation_error);
  if (current.meta.generation == std::numeric_limits<std::uint64_t>::max()) {
    return refuse("generation counter would overflow");
  }

  ProfileSnapshot candidate = current;
  ++candidate.meta.generation;
  candidate.content_version = content.content_version;
  // Effective parameters: base content, then owned Legacy traits, then run upgrades at zero.
  const Session founded(seed, content, trait_modifiers(current.meta));
  candidate.phase = ProfilePhase::ActiveRun;
  candidate.run = founded.snapshot("run-" + std::to_string(seed) + "-" +
                                   std::to_string(candidate.meta.generation));
  return {true, {}, std::move(candidate)};
}

TransactionResult prepare_abandon_run(const ProfileSnapshot& current,
                                      const std::uint64_t expected_revision) {
  if (const auto error = stale(current, expected_revision)) return refuse(*error);
  if (current.phase != ProfilePhase::ActiveRun || !current.run) {
    return refuse("there is no run to abandon");
  }
  ProfileSnapshot candidate = current;
  // A reset advances the generation but never the successful-flight count or the wallet.
  candidate.phase = ProfilePhase::BetweenRuns;
  candidate.run.reset();
  return {true, {}, std::move(candidate)};
}

} // namespace ant::game
