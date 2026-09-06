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
  std::vector<sim::BroodSnapshot> brood;
  std::vector<sim::CorpseSnapshot> corpses;
  std::vector<sim::DroppedCargoSnapshot> dropped_food;
  std::vector<std::uint16_t> trails;
  sim::TaskDiagnostics tasks{};
  int nursery_capacity{};
  int connected_nest_air{};
  std::uint64_t spoil_mound{};
  bool queen_alive{};
  bool decline{};
  bool extinct{};
  sim::Focus focus{sim::Focus::Balanced};
  bool focus_available{};
};

[[nodiscard]] GameView make_view(const Session& session);

} // namespace ant::game
