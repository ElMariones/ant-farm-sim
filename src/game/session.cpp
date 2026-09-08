#include "game/session.hpp"

#include "game/snapshot.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ant::game {

Session::Session(const std::uint64_t seed, ProgressionConfig progression,
                 const sim::TraitModifiers traits)
    : world_(seed, traits), progression_(std::move(progression)) {
  std::string error;
  if (!validate_progression(progression_, error)) throw std::invalid_argument(error);
}

Session::Session(sim::World world, ProgressionConfig progression)
    : world_(std::move(world)), progression_(std::move(progression)) {}

void Session::step() {
  world_.step();
  const std::uint64_t productive = world_.stats().productive_worker_ticks;
  if (productive < accounted_productive_ticks_) throw std::logic_error("productive tick counter regressed");
  productive_tick_remainder_ += productive - accounted_productive_ticks_;
  accounted_productive_ticks_ = productive;
  const std::uint64_t block = effective_ticks_per_work();
  const std::uint64_t earned = productive_tick_remainder_ / block;
  productive_tick_remainder_ %= block;
  if (earned > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - work_)) {
    throw std::overflow_error("Work total overflow");
  }
  work_ += static_cast<std::int64_t>(earned);
}

void Session::step_ticks(const sim::Tick count) { for (sim::Tick tick = 0; tick < count; ++tick) step(); }

bool Session::focus_available() const {
  return world_.living_workers() >= 12;
}

bool Session::set_focus(const sim::Focus focus) {
  return set_focus_command(focus).accepted;
}

CommandResult Session::set_focus_command(const sim::Focus focus) {
  if (!focus_available()) return {false, CommandRejection::FocusLocked, 0};
  if (world_.tick() < next_focus_change_tick_) return {false, CommandRejection::FocusCooldown, 0};
  world_.set_focus(focus);
  next_focus_change_tick_ = world_.tick() + progression_.focus_cooldown_ticks;
  last_command_message_ = "Colony focus changed";
  return {true, CommandRejection::None, 0};
}

CommandResult Session::buy_upgrade(const UpgradeId id) {
  const std::size_t index = upgrade_index(id);
  if (index >= upgrade_levels_.size()) return {false, CommandRejection::InvalidUpgrade, 0};
  const std::uint8_t level = upgrade_levels_[index];
  if (level >= 10) return {false, CommandRejection::MaxLevel, 0};
  const std::int64_t cost = progression_.upgrades[index].costs[level];
  if (work_ < cost) return {false, CommandRejection::InsufficientWork, 0};
  work_ -= cost;
  ++upgrade_levels_[index];
  world_.set_adaptation_levels(upgrade_levels_);
  last_command_message_ = progression_.upgrades[index].name + " improved";
  return {true, CommandRejection::None, cost};
}

std::int64_t Session::next_upgrade_cost(const UpgradeId id) const {
  const std::size_t index = upgrade_index(id);
  if (index >= upgrade_levels_.size() || upgrade_levels_[index] >= 10) return -1;
  return progression_.upgrades[index].costs[upgrade_levels_[index]];
}

sim::Tick Session::focus_cooldown_remaining() const {
  return world_.tick() >= next_focus_change_tick_ ? 0 : next_focus_change_tick_ - world_.tick();
}

RunSnapshot Session::snapshot(std::string run_id) const {
  if (run_id.empty()) run_id = "run-" + std::to_string(world_.seed()) + "-1";
  return {std::move(run_id), progression_, work_, productive_tick_remainder_,
          accounted_productive_ticks_, upgrade_levels_, next_focus_change_tick_, assisted_,
          world_.snapshot()};
}

Session Session::restore(const RunSnapshot& snapshot) {
  std::string error;
  if (!validate_progression(snapshot.embedded_progression, error)) throw std::invalid_argument(error);
  const std::uint64_t restored_block =
      snapshot.embedded_progression.productive_ticks_per_work *
      (snapshot.world.traits.industry_tier >= 4 ? 80U : 100U) / 100U;
  if (snapshot.productive_tick_remainder >= restored_block ||
      snapshot.accounted_productive_ticks != snapshot.world.stats.productive_worker_ticks ||
      snapshot.upgrade_levels != snapshot.world.adaptation_levels) {
    throw std::invalid_argument("run progression counters are inconsistent");
  }
  for (const auto level : snapshot.upgrade_levels) if (level > 10) throw std::invalid_argument("upgrade level exceeds 10");
  Session session(sim::World(snapshot.world), snapshot.embedded_progression);
  session.work_ = snapshot.work;
  session.productive_tick_remainder_ = snapshot.productive_tick_remainder;
  session.accounted_productive_ticks_ = snapshot.accounted_productive_ticks;
  session.upgrade_levels_ = snapshot.upgrade_levels;
  session.next_focus_change_tick_ = snapshot.next_focus_change_tick;
  session.assisted_ = snapshot.assisted;
  return session;
}

std::uint64_t Session::effective_ticks_per_work() const {
  const std::uint64_t base = progression_.productive_ticks_per_work;
  return world_.traits().industry_tier >= 4 ? base * 80 / 100 : base;
}

void Session::debug_grant_work(const std::int64_t amount) {
  if (amount < 0 || amount > std::numeric_limits<std::int64_t>::max() - work_) {
    throw std::invalid_argument("debug Work grant is out of range");
  }
  work_ += amount;
  assisted_ = true;
}

} // namespace ant::game
