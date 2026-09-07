#pragma once

#include "sim/components.hpp"
#include "sim/grid.hpp"
#include "sim/world.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::sim {

struct ActorState {
  Identity identity{};
  Position position{};
  Ant ant{};
  Cargo cargo{};
  bool worker{};
  Movement movement{};
  Forager forager{};
  WorkerMind mind{};
  Life life{};
};

struct WorldSnapshot {
  std::uint64_t seed{};
  Tick tick{};
  EntityId next_id{};
  GridPos home{};
  std::vector<Material> terrain;
  std::array<FoodSource, 2> sources{};
  FoodStore stores{};
  WorldStats stats{};
  std::uint64_t behavior_rng_state{};
  std::uint64_t behavior_rng_increment{};
  std::uint64_t lifecycle_rng_state{};
  std::uint64_t lifecycle_rng_increment{};
  std::vector<std::uint16_t> trails;
  std::vector<std::uint16_t> dig_work;
  TaskDiagnostics task_diagnostics{};
  std::vector<GridPos> frontiers;
  std::vector<BroodSnapshot> brood;
  std::vector<CorpseSnapshot> corpses;
  std::vector<DroppedCargoSnapshot> dropped_food;
  std::vector<ActorState> actors;
  std::uint64_t spoil_mound{};
  int starting_nest_air{};
  int connected_nest_air{};
  int nursery_capacity{};
  Tick next_laying{};
  Tick queen_starvation{};
  bool queen_alive{};
  bool decline{};
  bool extinct{};
  Focus focus{Focus::Balanced};
  std::array<std::uint8_t, 4> adaptation_levels{};
};

} // namespace ant::sim
