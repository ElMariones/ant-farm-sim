#pragma once

#include "sim/components.hpp"
#include "sim/nest_plan.hpp"
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
  // Whether the saved path was still current for the live navigation topology. Revision numbers
  // themselves are rebuilt on load, so validity is stored as a fact rather than a raw counter.
  bool path_valid{true};
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
  std::vector<FoodSource> sources;
  FoodStore stores{};
  WorldStats stats{};
  std::uint64_t behavior_rng_state{};
  std::uint64_t behavior_rng_increment{};
  std::uint64_t lifecycle_rng_state{};
  std::uint64_t lifecycle_rng_increment{};
  std::uint64_t world_rng_state{};
  std::uint64_t world_rng_increment{};
  std::vector<std::uint16_t> trails;
  std::vector<std::uint16_t> dig_work;
  TaskDiagnostics task_diagnostics{};
  // The rooms the colony has built or is building. They are the whole reason it digs, so they are
  // saved rather than guessed back from the shape of the ground.
  std::vector<Room> rooms;
  Tick next_source_spawn{};
  EntityId recruiting_source{};
  Tick recruit_until{};
  std::vector<BroodSnapshot> brood;
  std::vector<CorpseSnapshot> corpses;
  std::vector<DroppedCargoSnapshot> dropped_food;
  std::vector<FoodPile> granary;
  std::vector<ActorState> actors;
  std::uint64_t spoil_mound{};
  int starting_nest_air{};
  int connected_nest_air{};
  int nursery_capacity{};
  Tick next_laying{};
  Tick queen_settle{};
  Tick queen_starvation{};
  bool queen_alive{};
  bool decline{};
  bool extinct{};
  Focus focus{Focus::Balanced};
  std::array<std::uint8_t, 4> adaptation_levels{};
  TraitModifiers traits{};
  bool mature{};
  std::uint64_t egg_assignment_counter{};
};

} // namespace ant::sim
