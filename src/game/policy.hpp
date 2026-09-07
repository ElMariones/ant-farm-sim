#pragma once

#include "game/prestige.hpp"
#include "game/session.hpp"

#include <array>
#include <cstdint>

namespace ant::game {

struct PolicyOptions {
  // Buy the cheapest affordable run adaptation whenever one is affordable.
  bool buy_upgrades{};
  sim::Focus focus{sim::Focus::Balanced};
  // Record the tick flight first becomes possible, and stop the run there.
  bool stop_at_flight{true};
};

// Ticks are 0 when the milestone was never reached.
struct RunMilestones {
  sim::Tick first_delivery{};
  sim::Tick first_purchase{};
  sim::Tick first_birth{};
  sim::Tick maturity{};
  // ECONOMY asks for time to a stated excavation target, for the Industry comparison.
  sim::Tick hundred_cells_excavated{};
  sim::Tick flight_ready{};
  int peak_workers{};
  int peak_winged_queens{};
  std::uint64_t purchases{};
  std::array<std::uint8_t, 4> final_levels{};
  std::int64_t flight_payout{};
  bool extinct{};
  bool decline{};
  // Whether any larva ever starved, and whether stores ever ran dry.
  bool larval_starvation{};
  bool stores_empty{};
};

// A deterministic scripted player. Commands are issued at most once per simulated second, never
// per rendered frame, so a run measured here does not depend on frame rate.
class ScenarioPolicy {
public:
  explicit ScenarioPolicy(PolicyOptions options);

  // Advances the session by whole simulated seconds, acting between them. Returns early once
  // flight readiness is recorded if the policy stops there.
  void run(Session& session, sim::Tick tick_budget);

  [[nodiscard]] const RunMilestones& milestones() const { return milestones_; }
  [[nodiscard]] const char* limiting_bottleneck() const;

private:
  void act(Session& session);
  void observe(const Session& session);

  PolicyOptions options_;
  RunMilestones milestones_{};
  std::array<std::uint64_t, 6> bottleneck_seconds_{};
  bool focus_set_{};
};

// The cheapest affordable adaptation, ties broken excavation, nursing, foraging, queen. Returns
// false when nothing is affordable.
[[nodiscard]] bool cheapest_affordable_upgrade(const Session& session, UpgradeId& choice);

} // namespace ant::game
