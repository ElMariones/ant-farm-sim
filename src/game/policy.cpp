#include "game/policy.hpp"

#include "game/view.hpp"

#include <algorithm>

namespace ant::game {

bool cheapest_affordable_upgrade(const Session& session, UpgradeId& choice) {
  bool found = false;
  std::int64_t best = 0;
  // Index order is the documented tie-break: excavation, nursing, foraging, queen.
  for (std::size_t index = 0; index < 4; ++index) {
    const UpgradeId id = static_cast<UpgradeId>(index);
    const std::int64_t cost = session.next_upgrade_cost(id);
    if (cost < 0 || session.work() < cost) continue;
    if (!found || cost < best) {
      found = true;
      best = cost;
      choice = id;
    }
  }
  return found;
}

ScenarioPolicy::ScenarioPolicy(PolicyOptions options) : options_(options) {}

void ScenarioPolicy::run(Session& session, const sim::Tick tick_budget) {
  const sim::Tick start = session.world().tick();
  while (session.world().tick() - start < tick_budget) {
    session.step_ticks(sim::kTicksPerSecond);
    observe(session);
    act(session);
    if (options_.stop_at_flight && milestones_.flight_ready != 0) return;
    if (session.world().extinct()) return;
  }
}

void ScenarioPolicy::observe(const Session& session) {
  const sim::World& world = session.world();
  const sim::Tick now = world.tick();

  if (milestones_.first_delivery == 0 && world.stats().delivered > 0) milestones_.first_delivery = now;
  if (milestones_.first_birth == 0 && world.stats().workers_born > 0) milestones_.first_birth = now;
  if (milestones_.maturity == 0 && world.mature()) milestones_.maturity = now;
  if (milestones_.hundred_cells_excavated == 0 && world.stats().cells_excavated >= 100) {
    milestones_.hundred_cells_excavated = now;
  }
  milestones_.peak_workers = std::max(milestones_.peak_workers, world.living_workers());
  milestones_.peak_winged_queens = std::max(milestones_.peak_winged_queens, world.live_winged_queens());
  milestones_.extinct = world.extinct();
  milestones_.decline = world.decline();
  if (world.stores().carbohydrate == 0 || world.stores().protein == 0) milestones_.stores_empty = true;
  for (const auto& item : world.brood()) {
    if (item.starvation > 0) milestones_.larval_starvation = true;
  }
  milestones_.final_levels = session.upgrade_levels();

  const GameView view = make_view(session);
  ++bottleneck_seconds_[static_cast<std::size_t>(view.bottleneck)];

  if (milestones_.flight_ready == 0) {
    const FlightPreview preview = preview_flight(session);
    if (preview.eligible) {
      milestones_.flight_ready = now;
      milestones_.flight_payout = preview.payout.total;
    }
  }
}

void ScenarioPolicy::act(Session& session) {
  if (!focus_set_ && session.focus_available()) {
    focus_set_ = session.set_focus_command(options_.focus).accepted;
  }
  if (!options_.buy_upgrades) return;
  UpgradeId choice{UpgradeId::Excavation};
  while (cheapest_affordable_upgrade(session, choice)) {
    const CommandResult bought = session.buy_upgrade(choice);
    if (!bought.accepted) break;
    ++milestones_.purchases;
    if (milestones_.first_purchase == 0) milestones_.first_purchase = session.world().tick();
  }
}

const char* ScenarioPolicy::limiting_bottleneck() const {
  // The condition the colony spent most of its seconds reporting, ignoring "no limit".
  std::size_t best = 1;
  for (std::size_t index = 2; index < bottleneck_seconds_.size(); ++index) {
    if (bottleneck_seconds_[index] > bottleneck_seconds_[best]) best = index;
  }
  if (bottleneck_seconds_[best] == 0) return bottleneck_name(Bottleneck::None);
  return bottleneck_name(static_cast<Bottleneck>(best));
}

} // namespace ant::game
