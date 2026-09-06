#pragma once

#include "sim/components.hpp"
#include "sim/grid.hpp"
#include "sim/navigation.hpp"
#include "sim/rng.hpp"

#include <array>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <vector>

namespace ant::sim {

struct FoodSource {
  EntityId id{};
  GridPos position{};
  Nutrient nutrient{Nutrient::Carbohydrate};
  std::int64_t amount{};
  std::int64_t capacity{};
  std::int64_t reserved{};
  std::int64_t refill_amount{};
  Tick refill_interval{};
  Tick next_refill{};
};

struct FoodStore {
  std::int64_t carbohydrate{};
  std::int64_t protein{};
  std::int64_t capacity_per_nutrient{};
};

struct WorldStats {
  std::uint64_t path_requests{};
  std::uint64_t path_expansions{};
  std::uint64_t navigation_replans{};
  std::int64_t picked_up{};
  std::int64_t delivered{};
  std::int64_t external_refill{};
  std::uint64_t completed_round_trips{};
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
};

class World {
public:
  explicit World(std::uint64_t seed);

  void step();
  void run_ticks(Tick count);

  [[nodiscard]] std::uint64_t seed() const { return seed_; }
  [[nodiscard]] Tick tick() const { return tick_; }
  [[nodiscard]] const Grid& grid() const { return grid_; }
  [[nodiscard]] Grid& debug_grid() { return grid_; }
  [[nodiscard]] GridPos home() const { return home_; }
  [[nodiscard]] const std::array<FoodSource, 2>& sources() const { return sources_; }
  [[nodiscard]] const FoodStore& stores() const { return stores_; }
  [[nodiscard]] const WorldStats& stats() const { return stats_; }
  [[nodiscard]] std::vector<ActorSnapshot> actors() const;
  [[nodiscard]] std::uint64_t canonical_hash() const;
  [[nodiscard]] bool invariant_holds() const;

  void debug_set_source_amount(std::size_t index, std::int64_t amount);
  void debug_set_store(Nutrient nutrient, std::int64_t amount);

private:
  void spawn_queen();
  void spawn_workers();
  void refill_sources();
  void refresh_home_field();
  void process_forager(entt::entity entity, int& path_budget);
  void choose_source(entt::entity entity, int& path_budget);
  void refresh_path(entt::entity entity, int& path_budget);
  bool move_one_tick(entt::entity entity);
  void arrive(entt::entity entity, int& path_budget);
  void release_reservation(Forager& forager);
  [[nodiscard]] std::int64_t& store_for(Nutrient nutrient);
  [[nodiscard]] std::int64_t store_for(Nutrient nutrient) const;

  std::uint64_t seed_{};
  Tick tick_{};
  EntityId next_id_{1};
  Grid grid_;
  GridPos home_{};
  HomeField home_field_;
  std::array<FoodSource, 2> sources_{};
  FoodStore stores_{2'000, 2'000, 20'000};
  WorldStats stats_{};
  Pcg32 behavior_rng_;
  entt::registry registry_;
  std::vector<entt::entity> ordered_entities_;
};

} // namespace ant::sim
