#include "game/view.hpp"

namespace ant::game {

GameView make_view(const Session& session) {
  const sim::World& world = session.world();
  Bottleneck bottleneck = Bottleneck::None;
  if (world.stores().carbohydrate < 10'000 || world.stores().protein < 10'000) bottleneck = Bottleneck::Nutrition;
  else if (world.brood().size() >= static_cast<std::size_t>(world.nursery_capacity())) bottleneck = Bottleneck::NurserySpace;
  else if (!world.brood().empty() && world.task_diagnostics().stimuli[2] >= 500) bottleneck = Bottleneck::Nursing;
  else if (world.brood().empty() && world.queen_alive()) bottleneck = Bottleneck::QueenOutput;
  else if (world.task_diagnostics().workers_by_task[4] > world.actors().size() / 2) bottleneck = Bottleneck::Labor;
  GameView view{world.seed(),          world.tick(),          sim::Grid::kWidth,
          sim::Grid::kHeight,    world.grid().cells(),  world.home(),
          world.sources(),       world.stores(),        world.stats(),
          world.actors(),        world.brood(),         world.corpses(),
          world.dropped_food(),  world.trails().cells(), world.task_diagnostics(), world.nursery_capacity(),
          world.connected_nest_air(), world.spoil_mound(), world.queen_alive(),
          world.decline(),       world.extinct(), world.focus(), session.focus_available()};
  view.focus_cooldown_remaining = session.focus_cooldown_remaining();
  view.work = session.work();
  view.productive_tick_remainder = session.productive_tick_remainder();
  view.upgrade_levels = session.upgrade_levels();
  for (std::size_t index = 0; index < view.upgrade_costs.size(); ++index) {
    view.upgrade_costs[index] = session.next_upgrade_cost(static_cast<UpgradeId>(index));
  }
  view.bottleneck = bottleneck;
  return view;
}

const char* bottleneck_name(const Bottleneck bottleneck) {
  switch (bottleneck) {
  case Bottleneck::None: return "No limiting condition";
  case Bottleneck::Nutrition: return "Nutrition limits growth";
  case Bottleneck::Nursing: return "Brood needs nursing";
  case Bottleneck::NurserySpace: return "Nursery space limits growth";
  case Bottleneck::QueenOutput: return "Queen output limits growth";
  case Bottleneck::Labor: return "Productive labor is scarce";
  }
  return "Unknown bottleneck";
}

} // namespace ant::game
