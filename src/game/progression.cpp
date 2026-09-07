#include "game/progression.hpp"

#include <limits>

namespace ant::game {
namespace {

std::array<std::int64_t, 10> canonical_costs() {
  std::array<std::int64_t, 10> costs{};
  std::uint64_t numerator = 15;
  std::uint64_t denominator = 1;
  for (std::size_t level = 0; level < costs.size(); ++level) {
    costs[level] = static_cast<std::int64_t>((numerator + denominator - 1) / denominator);
    if (level + 1 < costs.size()) {
      numerator *= 8;
      denominator *= 5;
    }
  }
  return costs;
}

} // namespace

ProgressionConfig canonical_progression() {
  const auto costs = canonical_costs();
  return {"m3-v1",
          {{{UpgradeId::Excavation, "Strong Mandibles", costs, 25},
            {UpgradeId::Nursing, "Nursery Rhythm", costs, 10},
            {UpgradeId::Foraging, "Efficient Trails", costs, 20},
            {UpgradeId::Queen, "Queen Vitality", costs, 15}}},
          1'200,
          10 * sim::kTicksPerSecond};
}

bool validate_progression(const ProgressionConfig& config, std::string& error) {
  if (config.content_version.empty() || config.content_version.size() > 64) {
    error = "content_version must contain 1..64 characters";
    return false;
  }
  if (config.productive_ticks_per_work == 0 || config.productive_ticks_per_work > 20'000) {
    error = "productive_ticks_per_work outside 1..20000";
    return false;
  }
  if (config.focus_cooldown_ticks > 60 * sim::kTicksPerSecond) {
    error = "focus cooldown exceeds 60 seconds";
    return false;
  }
  std::array<bool, 4> seen{};
  for (const UpgradeDefinition& upgrade : config.upgrades) {
    const std::size_t index = upgrade_index(upgrade.id);
    if (seen[index] || upgrade.name.empty() || upgrade.name.size() > 64 ||
        upgrade.effect_percent_per_level <= 0 || upgrade.effect_percent_per_level > 100) {
      error = "invalid or duplicate upgrade definition";
      return false;
    }
    seen[index] = true;
    std::int64_t previous = 0;
    for (const std::int64_t cost : upgrade.costs) {
      if (cost <= 0 || cost < previous || cost > std::numeric_limits<std::int32_t>::max()) {
        error = "upgrade costs must be positive, monotonic, and bounded";
        return false;
      }
      previous = cost;
    }
  }
  return true;
}

std::size_t upgrade_index(const UpgradeId id) {
  switch (id) {
  case UpgradeId::Excavation: return 0;
  case UpgradeId::Nursing: return 1;
  case UpgradeId::Foraging: return 2;
  case UpgradeId::Queen: return 3;
  }
  return 4;
}

const char* upgrade_id_name(const UpgradeId id) {
  switch (id) {
  case UpgradeId::Excavation: return "excavation";
  case UpgradeId::Nursing: return "nursing";
  case UpgradeId::Foraging: return "foraging";
  case UpgradeId::Queen: return "queen";
  }
  return "invalid";
}

const char* rejection_reason(const CommandRejection rejection) {
  switch (rejection) {
  case CommandRejection::None: return "Accepted";
  case CommandRejection::InsufficientWork: return "Not enough Work yet";
  case CommandRejection::MaxLevel: return "Already at the highest level";
  case CommandRejection::FocusLocked: return "Focus unlocks at 12 workers";
  case CommandRejection::FocusCooldown: return "Focus was changed too recently";
  case CommandRejection::InvalidUpgrade: return "That adaptation does not exist";
  }
  return "Command refused";
}

} // namespace ant::game
