#pragma once

#include "sim/types.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace ant::game {

enum class UpgradeId : std::uint8_t { Excavation, Nursing, Foraging, Queen };
enum class CommandRejection : std::uint8_t {
  None,
  InsufficientWork,
  MaxLevel,
  FocusLocked,
  FocusCooldown,
  InvalidUpgrade
};

struct CommandResult {
  bool accepted{};
  CommandRejection rejection{CommandRejection::None};
  std::int64_t spent{};
};

struct UpgradeDefinition {
  UpgradeId id{UpgradeId::Excavation};
  std::string name;
  std::array<std::int64_t, 10> costs{};
  int effect_percent_per_level{};
};

struct ProgressionConfig {
  std::string content_version{"m3-v1"};
  std::array<UpgradeDefinition, 4> upgrades;
  std::uint64_t productive_ticks_per_work{1200};
  sim::Tick focus_cooldown_ticks{200};
};

[[nodiscard]] ProgressionConfig canonical_progression();
[[nodiscard]] bool validate_progression(const ProgressionConfig& config, std::string& error);
[[nodiscard]] std::size_t upgrade_index(UpgradeId id);
[[nodiscard]] const char* upgrade_id_name(UpgradeId id);
// Player-facing explanation of why a command was refused.
[[nodiscard]] const char* rejection_reason(CommandRejection rejection);

} // namespace ant::game
