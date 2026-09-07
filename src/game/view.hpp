#pragma once

#include "game/session.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::game {

enum class Bottleneck : std::uint8_t { None, Nutrition, Nursing, NurserySpace, QueenOutput, Labor };

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
  sim::Tick focus_cooldown_remaining{};
  std::int64_t work{};
  std::uint64_t productive_tick_remainder{};
  std::array<std::uint8_t, 4> upgrade_levels{};
  std::array<std::int64_t, 4> upgrade_costs{};
  Bottleneck bottleneck{Bottleneck::None};
};

[[nodiscard]] GameView make_view(const Session& session);
[[nodiscard]] const char* bottleneck_name(Bottleneck bottleneck);

} // namespace ant::game
