#pragma once

#include "game/progression.hpp"
#include "sim/types.hpp"
#include "sim/world.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace ant::game {

struct RunSnapshot;

class Session {
public:
  explicit Session(std::uint64_t seed, ProgressionConfig progression = canonical_progression(),
                   sim::TraitModifiers traits = {});

  void step();
  void step_ticks(sim::Tick count);
  [[nodiscard]] bool set_focus(sim::Focus focus);
  [[nodiscard]] CommandResult set_focus_command(sim::Focus focus);
  [[nodiscard]] CommandResult buy_upgrade(UpgradeId id);
  [[nodiscard]] bool focus_available() const;
  [[nodiscard]] std::int64_t work() const { return work_; }
  [[nodiscard]] std::uint64_t productive_tick_remainder() const { return productive_tick_remainder_; }
  [[nodiscard]] const std::array<std::uint8_t, 4>& upgrade_levels() const { return upgrade_levels_; }
  [[nodiscard]] std::int64_t next_upgrade_cost(UpgradeId id) const;
  [[nodiscard]] sim::Tick focus_cooldown_remaining() const;
  [[nodiscard]] const ProgressionConfig& progression() const { return progression_; }
  // Base content, then the Industry IV trait. Run adaptations do not affect this rate.
  [[nodiscard]] std::uint64_t effective_ticks_per_work() const;
  [[nodiscard]] const std::string& last_command_message() const { return last_command_message_; }
  [[nodiscard]] bool assisted() const { return assisted_; }
  [[nodiscard]] RunSnapshot snapshot(std::string run_id = {}) const;
  [[nodiscard]] static Session restore(const RunSnapshot& snapshot);

  // Grants Work without simulated labour. ECONOMY requires such a run be marked assisted so it
  // can never qualify for a prestige payout.
  void debug_grant_work(std::int64_t amount);

  [[nodiscard]] const sim::World& world() const { return world_; }
  [[nodiscard]] sim::World& debug_world() { return world_; }

private:
  Session(sim::World world, ProgressionConfig progression);
  sim::World world_;
  ProgressionConfig progression_;
  std::int64_t work_{};
  std::uint64_t productive_tick_remainder_{};
  std::uint64_t accounted_productive_ticks_{};
  std::array<std::uint8_t, 4> upgrade_levels_{};
  sim::Tick next_focus_change_tick_{};
  bool assisted_{};
  std::string last_command_message_;
};

} // namespace ant::game
