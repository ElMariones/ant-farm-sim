#include "game/view.hpp"

namespace ant::game {

GameView make_view(const Session& session) {
  const sim::World& world = session.world();
  return {world.seed(),          world.tick(),          sim::Grid::kWidth,
          sim::Grid::kHeight,    world.grid().cells(),  world.home(),
          world.sources(),       world.stores(),        world.stats(),
          world.actors(),        world.brood(),         world.corpses(),
          world.dropped_food(),  world.trails().cells(), world.task_diagnostics(), world.nursery_capacity(),
          world.connected_nest_air(), world.spoil_mound(), world.queen_alive(),
          world.decline(),       world.extinct(), world.focus(), session.focus_available()};
}

} // namespace ant::game
