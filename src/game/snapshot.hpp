#pragma once

#include "game/progression.hpp"
#include "sim/snapshot.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace ant::game {

enum class ProfilePhase : std::uint8_t { ActiveRun, BetweenRuns };

struct SettingsSnapshot {
  int ui_scale_percent{100};
  bool reduced_motion{};
  int preferred_speed{1};
};

struct MetaSnapshot {
  std::int64_t legacy_wallet{};
  std::int64_t legacy_earned_total{};
  std::int64_t legacy_spent_total{};
  std::uint8_t vigor_tier{};
  std::uint8_t industry_tier{};
  std::uint64_t generation{1};
  std::uint64_t successful_flights{};
};

// Immutable proof that one flight was paid out. Written in the same committed revision as the
// wallet update, so a repeated or stale flight command cannot credit Legacy twice.
struct FlightReceipt {
  std::string run_id;
  std::int64_t earned_legacy{};
  std::uint64_t births{};
  int winged_queens{};
};

struct RunSnapshot {
  std::string run_id;
  ProgressionConfig embedded_progression;
  std::int64_t work{};
  std::uint64_t productive_tick_remainder{};
  std::uint64_t accounted_productive_ticks{};
  std::array<std::uint8_t, 4> upgrade_levels{};
  sim::Tick next_focus_change_tick{};
  bool assisted{};
  sim::WorldSnapshot world;
};

inline constexpr std::uint32_t kCurrentSchemaVersion = 2;

struct ProfileSnapshot {
  std::uint32_t schema_version{kCurrentSchemaVersion};
  std::uint64_t revision{};
  std::string content_version{"m3-v1"};
  std::string profile_id;
  ProfilePhase phase{ProfilePhase::ActiveRun};
  MetaSnapshot meta;
  SettingsSnapshot settings;
  std::optional<FlightReceipt> last_flight_receipt;
  std::optional<RunSnapshot> run;
};

} // namespace ant::game
