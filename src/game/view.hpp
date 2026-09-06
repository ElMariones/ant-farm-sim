#pragma once

#include "game/session.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::game {

struct GameView {
  std::uint64_t seed{};
  sim::Tick tick{};
  int grid_width{};
  int grid_height{};
  std::vector<sim::Material> terrain;
  sim::GridPos home{};
  std::array<sim::FoodSource, 2> sources{};
  sim::FoodStore stores{};
  sim::WorldStats stats{};
  std::vector<sim::ActorSnapshot> actors;
};

[[nodiscard]] GameView make_view(const Session& session);

} // namespace ant::game
