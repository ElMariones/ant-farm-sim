#pragma once

#include "sim/components.hpp"
#include "sim/nest_plan.hpp"
#include "sim/grid.hpp"
#include "sim/navigation.hpp"
#include "sim/pheromones.hpp"
#include "sim/rng.hpp"

#include <array>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <optional>
#include <vector>

namespace ant::sim {

struct WorldSnapshot;

// Forage sites are finite and are not restocked. New ones appear on the surface over time, and
// the colony only knows about one after a scout has walked past it.
inline constexpr std::size_t kMaxFoodSources = 8;
// How close a scout must pass to notice a site.
inline constexpr int kDiscoveryReach = 6;
// The queen is heavy and unhurried.
inline constexpr int kQueenSpeed = 2;
// How long the colony keeps steering foragers onto a freshly discovered site.
inline constexpr Tick kRecruitmentTicks = 90 * kTicksPerSecond;
// A site nobody has found rots away rather than holding a slot for ever.
inline constexpr Tick kSourceLifetime = 300 * kTicksPerSecond;

struct FoodSource {
  EntityId id{};
  GridPos position{};
  Nutrient nutrient{Nutrient::Carbohydrate};
  std::int64_t amount{};
  std::int64_t capacity{};
  std::int64_t reserved{};
  bool known{};
  Tick appeared{};
};

// Grains a single store cell holds. Everything the colony owns sits in one of these, so a granary
// is a place in the nest rather than a number on the interface.
inline constexpr std::int64_t kGrainsPerStoreCell = 1'800;
struct FoodPile {
  GridPos position{};
  Nutrient nutrient{Nutrient::Carbohydrate};
  // Zero means the cell is claimed for this nutrient but currently empty; it may be re-typed.
  std::int64_t amount{};
};

struct FoodStore {
  std::int64_t carbohydrate{};
  std::int64_t protein{};
  std::int64_t carbohydrate_capacity{};
  std::int64_t protein_capacity{};
};

struct WorldStats {
  std::uint64_t path_requests{};
  std::uint64_t path_expansions{};
  std::uint64_t navigation_replans{};
  std::int64_t picked_up{};
  std::int64_t delivered{};
  std::int64_t external_refill{};
  std::uint64_t completed_round_trips{};
  std::uint64_t cells_excavated{};
  std::uint64_t spoil_delivered{};
  // Grains hauled out after the apron was already full. Counted rather than dropped so excavated
  // cells, delivered grains and the visible mound still add up.
  std::uint64_t spoil_overflow{};
  std::uint64_t eggs_laid{};
  std::uint64_t workers_born{};
  std::uint64_t deaths{};
  std::uint64_t corpses_cleaned{};
  std::int64_t consumed_carbohydrate{};
  std::int64_t consumed_protein{};
  std::int64_t decayed_food{};
  std::uint64_t productive_worker_ticks{};
  std::uint64_t gynes_born{};
  std::uint64_t sources_found{};
  std::uint64_t sources_exhausted{};
  std::uint64_t rooms_built{};
};

struct ActorSnapshot {
  EntityId id{};
  AntKind kind{AntKind::Worker};
  ForageState forage_state{ForageState::AtHome};
  double x{};
  double y{};
  double previous_x{};
  double previous_y{};
  Nutrient cargo_nutrient{Nutrient::Carbohydrate};
  std::int64_t cargo_amount{};
  CargoKind cargo_kind{CargoKind::None};
  Task task{Task::Idle};
  Tick age{};
};

struct BroodSnapshot {
  EntityId id{};
  BroodStage stage{BroodStage::Egg};
  GridPos position{};
  Tick progress{};
  Tick target{};
  Tick care_remaining{};
  Tick starvation{};
  BroodRole role{BroodRole::Worker};
  // Nurse currently carrying this item, or zero when it is lying in the nest.
  EntityId carried_by{};
};

// Permanent Legacy traits, fixed when the colony is founded. Effective parameters are computed
// from base config, then these, then run adaptations; never by compounding an already-modified
// value.
struct TraitModifiers {
  std::uint8_t vigor_tier{};
  std::uint8_t industry_tier{};

  friend constexpr bool operator==(TraitModifiers, TraitModifiers) = default;
};

struct CorpseSnapshot {
  EntityId id{};
  GridPos position{};
  Tick age{};
  bool cleanable{};
};

struct DroppedCargoSnapshot {
  EntityId id{};
  GridPos position{};
  Nutrient nutrient{Nutrient::Carbohydrate};
  std::int64_t amount{};
  Tick age{};
};

struct TaskDiagnostics {
  std::array<std::uint16_t, 4> stimuli{};
  std::array<std::uint32_t, 5> workers_by_task{};
};

class World {
public:
  explicit World(std::uint64_t seed, TraitModifiers traits = {});
  explicit World(const WorldSnapshot& snapshot);

  void step();
  void run_ticks(Tick count);

  [[nodiscard]] std::uint64_t seed() const { return seed_; }
  [[nodiscard]] Tick tick() const { return tick_; }
  [[nodiscard]] const Grid& grid() const { return grid_; }
  [[nodiscard]] Grid& debug_grid() { return grid_; }
  [[nodiscard]] GridPos home() const { return home_; }
  [[nodiscard]] const std::vector<FoodSource>& sources() const { return sources_; }
  [[nodiscard]] const std::vector<Room>& rooms() const { return nest_plan_.rooms(); }
  // The site the colony is currently recruiting to, while the alert lasts.
  [[nodiscard]] EntityId recruiting_source() const {
    return tick_ < recruit_until_ ? recruiting_source_ : 0;
  }
  [[nodiscard]] bool knows_any_food() const;
  // True while the colony is short of a food it has nowhere known to collect.
  [[nodiscard]] bool wants_scouts() const;
  [[nodiscard]] const FoodStore& stores() const { return stores_; }
  [[nodiscard]] const WorldStats& stats() const { return stats_; }
  [[nodiscard]] std::vector<ActorSnapshot> actors() const;
  [[nodiscard]] std::vector<BroodSnapshot> brood() const;
  [[nodiscard]] const std::vector<CorpseSnapshot>& corpses() const { return corpses_; }
  [[nodiscard]] const std::vector<DroppedCargoSnapshot>& dropped_food() const { return dropped_food_; }
  [[nodiscard]] const std::vector<FoodPile>& granary() const { return granary_; }
  [[nodiscard]] const TrailField& trails() const { return trails_; }
  [[nodiscard]] const TaskDiagnostics& task_diagnostics() const { return task_diagnostics_; }
  [[nodiscard]] int nursery_capacity() const { return nursery_capacity_; }
  [[nodiscard]] int connected_nest_air() const { return connected_nest_air_; }
  [[nodiscard]] std::uint64_t spoil_mound() const { return spoil_mound_; }
  [[nodiscard]] bool queen_alive() const { return queen_alive_; }
  [[nodiscard]] bool decline() const { return decline_; }
  [[nodiscard]] bool extinct() const { return extinct_; }
  // Latched once living workers, run births and run age all pass their thresholds. Never revoked.
  [[nodiscard]] bool mature() const { return mature_; }
  [[nodiscard]] int living_workers() const;
  [[nodiscard]] int live_winged_queens() const;
  [[nodiscard]] int winged_brood() const;
  [[nodiscard]] TraitModifiers traits() const { return traits_; }
  [[nodiscard]] std::uint64_t egg_assignment_counter() const { return egg_assignment_counter_; }
  [[nodiscard]] Focus focus() const { return focus_; }
  void set_focus(Focus focus) { focus_ = focus; }
  void set_adaptation_levels(const std::array<std::uint8_t, 4>& levels) { adaptation_levels_ = levels; }
  [[nodiscard]] const std::array<std::uint8_t, 4>& adaptation_levels() const { return adaptation_levels_; }
  [[nodiscard]] std::uint64_t canonical_hash() const;
  [[nodiscard]] bool invariant_holds() const;
  [[nodiscard]] WorldSnapshot snapshot() const;

  void debug_set_source_amount(std::size_t index, std::int64_t amount);
  // Puts a fresh undiscovered site on the surface, so scouting and recruitment can be tested
  // without waiting for the natural schedule.
  EntityId debug_add_source(GridPos position, Nutrient nutrient, std::int64_t amount);
  void debug_set_store(Nutrient nutrient, std::int64_t amount);
  void debug_set_worker_lifespan(EntityId id, Tick lifespan);
  void debug_spawn_brood(BroodStage stage, Tick progress = 0, Tick starvation = 0,
                         BroodRole role = BroodRole::Worker);
  void debug_spawn_workers(int count);
  void debug_kill_workers(int count);
  void debug_set_workers_born(std::uint64_t births);
  // Latches maturity without simulating the twelve minutes and 150 births that normally reach it,
  // so caste-assignment behaviour can be tested independently of how maturity was reached.
  // Empties the nursery so a boundary test can hold population and births still while the colony
  // ticks. Without it a hatch during the settle silently moves the very threshold under test.
  void debug_clear_brood();
  void debug_set_mature();
  void debug_kill_queen();

private:
  void spawn_queen();
  void process_queen(int& path_budget);
  void spawn_workers();
  void spawn_food_sources();
  void place_food_source();
  [[nodiscard]] int surface_row(int x) const;
  [[nodiscard]] FoodSource* find_source(EntityId id);
  [[nodiscard]] const FoodSource* find_source(EntityId id) const;
  void discover_source(FoodSource& source);
  void retire_spent_sources();
  void process_scout(entt::entity entity, int& path_budget);
  [[nodiscard]] int scout_cap() const;
  void refresh_home_field();
  // Splits the connected nest into the brood ring around the queen and the store chambers beyond
  // it, and derives store capacity from the cells that actually exist. Purely a function of the
  // grid, so a restored colony classifies exactly as the uninterrupted one does.
  void refresh_zones();
  [[nodiscard]] FoodPile* pile_at(GridPos cell);
  [[nodiscard]] const FoodPile* pile_at(GridPos cell) const;
  // Where one load of `nutrient` should go: the nearest heap with room, else the nearest free
  // store cell. `bias` spreads simultaneous deliveries over neighbouring heaps.
  [[nodiscard]] std::optional<GridPos> store_target(Nutrient nutrient, EntityId bias) const;
  // Whether one more grain of `nutrient` could be set down in this cell.
  [[nodiscard]] bool accepts_food(GridPos cell, Nutrient nutrient) const;
  // Whether the nutrient may still claim another store cell.
  [[nodiscard]] bool store_cells_left(Nutrient nutrient) const;
  [[nodiscard]] std::optional<GridPos> nursery_target(EntityId ignore_brood) const;
  [[nodiscard]] bool in_nursery(GridPos cell) const;
  std::int64_t deposit_food(GridPos cell, Nutrient nutrient, std::int64_t amount);
  void stock_granary(Nutrient nutrient, std::int64_t amount);
  void store_cargo(entt::entity entity, int& path_budget);
  void process_forager(entt::entity entity, int& path_budget);
  // False when the colony knows of nowhere left to forage, which is what sends a worker scouting.
  [[nodiscard]] bool choose_source(entt::entity entity, int& path_budget);
  void refresh_path(entt::entity entity, int& path_budget);
  // Cells per second is the caller's choice: a worker trots, the queen does not.
  bool move_one_tick(entt::entity entity, int cells_per_second = 6);
  void arrive(entt::entity entity, int& path_budget);
  void release_reservation(Forager& forager);
  [[nodiscard]] std::int64_t& store_for(Nutrient nutrient);
  [[nodiscard]] std::int64_t store_for(Nutrient nutrient) const;
  [[nodiscard]] std::int64_t capacity_for(Nutrient nutrient) const;
  void recompute_needs_and_dig_plan();
  void choose_task(entt::entity entity);
  void process_worker(entt::entity entity, int& path_budget);
  void process_excavator(entt::entity entity, int& path_budget);
  void process_nurse(entt::entity entity, int& path_budget);
  void process_cleaner(entt::entity entity, int& path_budget);
  void route_to(entt::entity entity, GridPos target, int& path_budget);
  void deliver_non_food(entt::entity entity);
  void update_biology();
  void update_trails();
  void remove_dead_workers();
  void spawn_worker(GridPos position);
  [[nodiscard]] bool consume(Nutrient nutrient, std::int64_t amount);
  [[nodiscard]] Tick brood_target(BroodStage stage, BroodRole role) const;
  void spawn_winged_queen(GridPos position);
  // Places one carried grain of spoil on the surface mound. Returns false when there is nowhere
  // left to put it.
  bool deposit_spoil();
  [[nodiscard]] bool cell_is_occupied(GridPos cell) const;
  void update_maturity();
  void refresh_dig_claims();
  void release_dig_claim(const WorkerMind& mind);
  // One stable route preference per ant, so ants sharing endpoints need not share a route.
  [[nodiscard]] RouteBias bias_for(entt::entity e) const;

  std::uint64_t seed_{};
  Tick tick_{};
  EntityId next_id_{1};
  Grid grid_;
  GridPos home_{};
  HomeField home_field_;
  std::vector<FoodSource> sources_;
  FoodStore stores_{60'000, 30'000, 200'000, 100'000};
  WorldStats stats_{};
  Pcg32 behavior_rng_;
  Pcg32 lifecycle_rng_;
  // Where and when new forage sites appear. Kept apart from behaviour and life cycle so a colony
  // that acts differently still meets the same food.
  Pcg32 world_rng_;
  TrailField trails_;
  TaskDiagnostics task_diagnostics_{};
  // Excavation exists to build rooms. One project at a time, so the colony finishes a chamber
  // before starting the next and the nest reads as rooms joined by the corridors that reached them.
  NestPlan nest_plan_;
  std::uint64_t rooms_version_{1};
  std::vector<std::uint16_t> dig_work_;
  std::vector<BroodSnapshot> brood_;
  std::vector<CorpseSnapshot> corpses_;
  std::vector<DroppedCargoSnapshot> dropped_food_;
  // Append-only: a pile's slot is stable, so `pile_index_` can address it by cell.
  std::vector<FoodPile> granary_;
  std::vector<std::int32_t> pile_index_;
  std::vector<GridPos> nursery_cells_;
  std::vector<std::uint8_t> nursery_mask_;
  std::vector<GridPos> store_cells_;
  std::uint64_t zone_revision_{};
  std::uint64_t zone_rooms_version_{};
  Tick next_source_spawn_{};
  EntityId recruiting_source_{};
  Tick recruit_until_{};
  // Scouts alive at the start of this tick, so the colony sends a few searchers and the rest wait.
  int scouting_workers_{};
  int connected_nest_air_{};
  int starting_nest_air_{};
  int nursery_capacity_{12};
  std::uint64_t spoil_mound_{};
  Tick next_laying_{240};
  // When the queen may next choose somewhere to walk.
  Tick queen_settle_{};
  Tick queen_starvation_{};
  bool queen_alive_{true};
  bool decline_{};
  bool extinct_{};
  Focus focus_{Focus::Balanced};
  std::array<std::uint8_t, 4> adaptation_levels_{};
  TraitModifiers traits_{};
  // Food picked up but not yet delivered, per nutrient. Derived state: recomputed on load, never
  // trusted from a snapshot.
  std::array<std::int64_t, 2> carried_food_{};
  bool mature_{};
  std::uint64_t egg_assignment_counter_{};
  entt::registry registry_;
  std::vector<entt::entity> ordered_entities_;
};

} // namespace ant::sim
