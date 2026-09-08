#include "game/view.hpp"

#include <algorithm>

namespace ant::game {

GameView make_view(const Session& session) {
  const sim::World& world = session.world();
  Bottleneck bottleneck = Bottleneck::None;
  if (world.stores().carbohydrate < 10'000 || world.stores().protein < 10'000) bottleneck = Bottleneck::Nutrition;
  else if (world.brood().size() >= static_cast<std::size_t>(world.nursery_capacity())) bottleneck = Bottleneck::NurserySpace;
  else if (!world.brood().empty() && world.task_diagnostics().stimuli[2] >= 500) bottleneck = Bottleneck::Nursing;
  else if (world.brood().empty() && world.queen_alive()) bottleneck = Bottleneck::QueenOutput;
  else if (world.task_diagnostics().workers_by_task[4] > world.actors().size() / 2) bottleneck = Bottleneck::Labor;
  GameView view;
  view.seed = world.seed();
  view.tick = world.tick();
  view.grid_width = sim::Grid::kWidth;
  view.grid_height = sim::Grid::kHeight;
  view.terrain = world.grid().cells();
  view.home = world.home();
  view.sources = world.sources();
  view.rooms = world.rooms();
  view.construction = world.construction();
  view.recruiting_source = world.recruiting_source();
  view.knows_any_food = world.knows_any_food();
  view.stores = world.stores();
  view.stats = world.stats();
  view.actors = world.actors();
  view.brood = world.brood();
  view.corpses = world.corpses();
  view.dropped_food = world.dropped_food();
  view.granary = world.granary();
  view.trails = world.trails().cells();
  view.tasks = world.task_diagnostics();
  view.nursery_capacity = world.nursery_capacity();
  view.connected_nest_air = world.connected_nest_air();
  view.spoil_mound = world.spoil_mound();
  view.queen_alive = world.queen_alive();
  view.decline = world.decline();
  view.extinct = world.extinct();
  view.focus = world.focus();
  view.focus_available = session.focus_available();
  view.focus_cooldown_remaining = session.focus_cooldown_remaining();
  view.work = session.work();
  view.productive_tick_remainder = session.productive_tick_remainder();
  view.upgrade_levels = session.upgrade_levels();
  for (std::size_t index = 0; index < view.upgrade_costs.size(); ++index) {
    view.upgrade_costs[index] = session.next_upgrade_cost(static_cast<UpgradeId>(index));
  }
  view.bottleneck = bottleneck;
  view.terrain_revision = world.grid().terrain_revision();
  view.legacy.flight = preview_flight(session);
  return view;
}

GameView::Watched GameView::watched() const {
  Watched out;
  out.workers_born = stats.workers_born;
  out.gynes_born = stats.gynes_born;
  out.sources_found = stats.sources_found;
  out.sources_exhausted = stats.sources_exhausted;
  out.rooms_built = stats.rooms_built;
  out.complete_rooms = static_cast<std::size_t>(
      std::count_if(rooms.begin(), rooms.end(), [](const sim::Room& room) { return room.complete; }));
  out.complete_passages = construction.complete_passages;
  out.upgrade_levels = upgrade_levels;
  return out;
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
