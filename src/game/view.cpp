#include "game/view.hpp"

namespace ant::game {

GameView make_view(const Session& session) {
  const sim::World& world = session.world();
  return {world.seed(),         world.tick(),  sim::Grid::kWidth, sim::Grid::kHeight,
          world.grid().cells(), world.home(),  world.sources(),   world.stores(),
          world.stats(),        world.actors()};
}

} // namespace ant::game
