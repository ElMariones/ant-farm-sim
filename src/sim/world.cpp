#include "sim/world.hpp"

#include "sim/snapshot.hpp"
#include "sim/terrain_generation.hpp"

#include <algorithm>
#include <limits>
#include <array>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <utility>

namespace ant::sim {

namespace {
// A path and its cursor are one value: clearing them apart leaves next_cell past the end, which
// is an invalid movement state a snapshot must never contain.
void clear_path(Movement& movement) {
  movement.path.clear();
  movement.next_cell = 0;
}
} // namespace
namespace {
constexpr std::array<GridPos, 4> kNeighbors{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
std::int32_t cell_center(const int value) { return value * kSubcellsPerCell + kSubcellsPerCell / 2; }
std::size_t grid_index(const GridPos p) { return static_cast<std::size_t>(p.y * Grid::kWidth + p.x); }
void hash_value(std::uint64_t& hash, const std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) { hash ^= (value >> static_cast<unsigned>(byte * 8)) & 0xffULL; hash *= 1099511628211ULL; }
}
void hash_signed(std::uint64_t& hash, const std::int64_t value) {
  hash_value(hash, static_cast<std::uint64_t>(value));
}
} // namespace

World::World(const std::uint64_t seed, const TraitModifiers traits)
    : seed_(seed), grid_(Material::Soil),
      behavior_rng_(mix_seed(seed ^ 0xB3A4107ULL), mix_seed(seed ^ 0x515EEDULL)),
      lifecycle_rng_(mix_seed(seed ^ 0x11FEC1EULL), mix_seed(seed ^ 0xA63ULL)),
      dig_work_(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight)) {
  GeneratedTerrain generated = generate_terrain(seed);
  grid_ = std::move(generated.grid); home_ = generated.home;
  sources_[0] = {next_id_++, generated.source_positions[0], Nutrient::Carbohydrate, 100'000, 100'000, 0, 800, 20, 20};
  sources_[1] = {next_id_++, generated.source_positions[1], Nutrient::Protein, 100'000, 100'000, 0, 400, 20, 20};
  traits_ = traits;
  refresh_home_field(); refresh_zones();
  // The founding stock is placed in real cells, so the colony starts with visible heaps rather
  // than a number, and never with more grain than its chambers can hold.
  const std::int64_t founding_carbohydrate = std::min<std::int64_t>(60'000, stores_.carbohydrate_capacity);
  const std::int64_t founding_protein = std::min<std::int64_t>(30'000, stores_.protein_capacity);
  stock_granary(Nutrient::Carbohydrate, founding_carbohydrate);
  stock_granary(Nutrient::Protein, founding_protein);
  spawn_queen(); spawn_workers(); recompute_needs_and_dig_plan();
  starting_nest_air_ = connected_nest_air_;
}

World::World(const WorldSnapshot& snapshot)
    : seed_(snapshot.seed), tick_(snapshot.tick), next_id_(snapshot.next_id),
      grid_(Material::Soil), home_(snapshot.home), sources_(snapshot.sources), stores_(snapshot.stores),
      stats_(snapshot.stats),
      behavior_rng_(Pcg32::restore(snapshot.behavior_rng_state, snapshot.behavior_rng_increment)),
      lifecycle_rng_(Pcg32::restore(snapshot.lifecycle_rng_state, snapshot.lifecycle_rng_increment)),
      task_diagnostics_(snapshot.task_diagnostics),
      dig_work_(snapshot.dig_work), brood_(snapshot.brood), corpses_(snapshot.corpses),
      dropped_food_(snapshot.dropped_food), granary_(snapshot.granary),
      connected_nest_air_(snapshot.connected_nest_air),
      starting_nest_air_(snapshot.starting_nest_air), nursery_capacity_(snapshot.nursery_capacity),
      spoil_mound_(snapshot.spoil_mound), next_laying_(snapshot.next_laying),
      queen_starvation_(snapshot.queen_starvation), queen_alive_(snapshot.queen_alive),
      decline_(snapshot.decline), extinct_(snapshot.extinct), focus_(snapshot.focus),
      adaptation_levels_(snapshot.adaptation_levels), traits_(snapshot.traits),
      mature_(snapshot.mature), egg_assignment_counter_(snapshot.egg_assignment_counter) {
  if (snapshot.terrain.size() != static_cast<std::size_t>(Grid::kWidth * Grid::kHeight) ||
      dig_work_.size() != snapshot.terrain.size()) throw std::invalid_argument("snapshot grid size mismatch");
  for (int y = 0; y < Grid::kHeight; ++y) for (int x = 0; x < Grid::kWidth; ++x) {
    grid_.set({x, y}, snapshot.terrain[static_cast<std::size_t>(y * Grid::kWidth + x)]);
  }
  trails_.restore(snapshot.trails);
  refresh_home_field();
  refresh_zones();
  // A profile written before food had a place in the nest restores as totals with no heaps. Lay
  // them out now so the colony continues with the grain it had, sitting where it can be seen.
  if (granary_.empty() && (stores_.carbohydrate > 0 || stores_.protein > 0)) {
    stock_granary(Nutrient::Carbohydrate, stores_.carbohydrate);
    stock_granary(Nutrient::Protein, stores_.protein);
  }
  for (const ActorState& actor : snapshot.actors) {
    const entt::entity entity = registry_.create();
    registry_.emplace<Identity>(entity, actor.identity);
    registry_.emplace<Position>(entity, actor.position);
    registry_.emplace<Ant>(entity, actor.ant);
    registry_.emplace<Cargo>(entity, actor.cargo);
    if (actor.worker) {
      Movement movement = actor.movement;
      // Restore staleness, not the counter: a path that needed replanning before the save must
      // still need it after.
      // Navigation revisions start at 1 and only increase, so 0 marks a path that was already
      // stale and can never be mistaken for a current one later.
      movement.path_revision = actor.path_valid ? grid_.navigation_revision() : 0;
      registry_.emplace<Movement>(entity, std::move(movement));
      registry_.emplace<Forager>(entity, actor.forager);
      registry_.emplace<WorkerMind>(entity, actor.mind);
      registry_.emplace<Life>(entity, actor.life);
    } else if (actor.ant.kind == AntKind::WingedQueen) {
      registry_.emplace<Life>(entity, actor.life);
    }
    ordered_entities_.push_back(entity);
  }
  dig_plan_.restore(snapshot.dig_faces);
  frontier_revision_ = snapshot.frontiers_valid ? grid_.navigation_revision() : 0;
  for (const ActorState& actor : snapshot.actors) {
    if (actor.cargo.kind == CargoKind::Food && actor.cargo.amount > 0) {
      carried_food_[static_cast<std::size_t>(actor.cargo.nutrient)] += actor.cargo.amount;
    }
  }
  if (!invariant_holds()) throw std::invalid_argument("snapshot violates world invariants");
}

void World::spawn_queen() {
  const entt::entity e = registry_.create(); registry_.emplace<Identity>(e, next_id_++); registry_.emplace<Ant>(e, AntKind::Queen);
  registry_.emplace<Position>(e, cell_center(home_.x), cell_center(home_.y), cell_center(home_.x), cell_center(home_.y));
  registry_.emplace<Cargo>(e); ordered_entities_.push_back(e);
}

void World::spawn_worker(const GridPos p) {
  const entt::entity e = registry_.create(); registry_.emplace<Identity>(e, next_id_++); registry_.emplace<Ant>(e, AntKind::Worker);
  registry_.emplace<Position>(e, cell_center(p.x), cell_center(p.y), cell_center(p.x), cell_center(p.y));
  registry_.emplace<Cargo>(e); registry_.emplace<Movement>(e); registry_.emplace<Forager>(e);
  WorkerMind mind; for (std::uint16_t& threshold : mind.thresholds) threshold = static_cast<std::uint16_t>(250U + behavior_rng_.bounded(501U));
  registry_.emplace<WorkerMind>(e, mind);
  registry_.emplace<Life>(e, Life{0, static_cast<Tick>(1'200U + lifecycle_rng_.bounded(1'201U)) * kTicksPerSecond, 0});
  ordered_entities_.push_back(e);
}

void World::spawn_winged_queen(const GridPos p) {
  const entt::entity e = registry_.create(); registry_.emplace<Identity>(e, next_id_++); registry_.emplace<Ant>(e, AntKind::WingedQueen);
  registry_.emplace<Position>(e, cell_center(p.x), cell_center(p.y), cell_center(p.x), cell_center(p.y));
  registry_.emplace<Cargo>(e);
  // Winged queens do not forage, dig or nurse, and do not die of age in v0.1.
  registry_.emplace<Life>(e, Life{0, std::numeric_limits<Tick>::max(), 0});
  ordered_entities_.push_back(e);
}

void World::spawn_workers() {
  constexpr std::array<GridPos, 6> offsets{{{-3, 0}, {-2, -2}, {0, -3}, {2, -2}, {3, 0}, {0, 3}}};
  const std::size_t count = offsets.size() + (traits_.vigor_tier >= 2 ? 2U : 0U);
  for (std::size_t i = 0; i < count; ++i) { const GridPos offset = offsets[i % offsets.size()]; spawn_worker({home_.x + offset.x, home_.y + offset.y}); registry_.get<Life>(ordered_entities_.back()).age = static_cast<Tick>(i) * 24U * kTicksPerSecond; }
}

void World::refill_sources() {
  for (FoodSource& source : sources_) while (tick_ >= source.next_refill) {
    const std::int64_t before = source.amount; source.amount = std::min(source.capacity, source.amount + source.refill_amount);
    stats_.external_refill += source.amount - before; source.next_refill += source.refill_interval;
  }
}
void World::refresh_home_field() { if (home_field_.revision() != grid_.navigation_revision()) home_field_.rebuild(grid_, home_); }

void World::refresh_zones() {
  if (zone_revision_ == grid_.navigation_revision()) return;
  zone_revision_ = grid_.navigation_revision();
  nursery_cells_.clear();
  store_cells_.clear();
  const std::size_t cell_count = static_cast<std::size_t>(Grid::kWidth * Grid::kHeight);
  nursery_mask_.assign(cell_count, 0);
  std::vector<std::uint8_t> visited(cell_count);
  std::queue<GridPos> queue;
  queue.push(home_);
  visited[grid_index(home_)] = 1;
  while (!queue.empty()) {
    const GridPos cell = queue.front();
    queue.pop();
    const int reach = std::abs(cell.x - home_.x) + std::abs(cell.y - home_.y);
    if (reach <= kNurseryRadius) {
      nursery_cells_.push_back(cell);
      nursery_mask_[grid_index(cell)] = 1;
    } else {
      // Only a real room stores food. A three-wide corridor can never reach sixteen open cells in
      // its five-by-five neighbourhood, so passages stay clear for traffic and chambers fill up.
      int open = 0;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          const GridPos probe{cell.x + dx, cell.y + dy};
          if (grid_.in_bounds(probe) && grid_.at(probe) == Material::Air) ++open;
        }
      }
      if (open >= 16) store_cells_.push_back(cell);
    }
    for (const GridPos step : kNeighbors) {
      const GridPos next{cell.x + step.x, cell.y + step.y};
      if (!grid_.in_bounds(next) || visited[grid_index(next)] != 0) continue;
      if (grid_.at(next) != Material::Air) continue;
      visited[grid_index(next)] = 1;
      queue.push(next);
    }
  }
  const auto nearer = [this](const GridPos a, const GridPos b) {
    const int reach_a = std::abs(a.x - home_.x) + std::abs(a.y - home_.y);
    const int reach_b = std::abs(b.x - home_.x) + std::abs(b.y - home_.y);
    return reach_a != reach_b ? reach_a < reach_b : a < b;
  };
  std::sort(nursery_cells_.begin(), nursery_cells_.end(), nearer);
  std::sort(store_cells_.begin(), store_cells_.end(), nearer);

  pile_index_.assign(cell_count, -1);
  for (std::size_t index = 0; index < granary_.size(); ++index) {
    pile_index_[grid_index(granary_[index].position)] = static_cast<std::int32_t>(index);
  }
  // Capacity is exactly the room the colony has dug. Store cells only ever accumulate — air never
  // becomes solid inside the nest — so this is monotonic and can never invalidate stored grain,
  // and it depends on nothing but the grid, so a restored colony computes the same number.
  const std::int64_t cells = static_cast<std::int64_t>(store_cells_.size());
  const std::int64_t carbohydrate_cells = cells * 2 / 3;
  stores_.carbohydrate_capacity = carbohydrate_cells * kGrainsPerStoreCell;
  stores_.protein_capacity = (cells - carbohydrate_cells) * kGrainsPerStoreCell;
}

FoodPile* World::pile_at(const GridPos cell) {
  return const_cast<FoodPile*>(std::as_const(*this).pile_at(cell));
}

const FoodPile* World::pile_at(const GridPos cell) const {
  if (!grid_.in_bounds(cell) || pile_index_.empty()) return nullptr;
  const std::int32_t index = pile_index_[grid_index(cell)];
  return index < 0 ? nullptr : &granary_[static_cast<std::size_t>(index)];
}

bool World::in_nursery(const GridPos cell) const {
  return grid_.in_bounds(cell) && !nursery_mask_.empty() && nursery_mask_[grid_index(cell)] != 0;
}

bool World::accepts_food(const GridPos cell, const Nutrient nutrient) const {
  const FoodPile* pile = pile_at(cell);
  if (pile == nullptr) return grid_.in_bounds(cell) && grid_.at(cell) == Material::Air;
  return pile->amount == 0 ||
         (pile->nutrient == nutrient && pile->amount < kGrainsPerStoreCell);
}

std::optional<GridPos> World::store_target(const Nutrient nutrient, const EntityId bias) const {
  const std::int64_t allowed = capacity_for(nutrient) / kGrainsPerStoreCell;
  std::int64_t used = 0;
  for (const FoodPile& pile : granary_) {
    if (pile.amount > 0 && pile.nutrient == nutrient) ++used;
  }
  // Several near candidates rather than one, so simultaneous deliveries spread over neighbouring
  // heaps instead of every forager converging on the same cell.
  constexpr std::size_t kCandidates = 4;
  std::array<GridPos, kCandidates> topping_up{};
  std::array<GridPos, kCandidates> empty{};
  std::size_t partial = 0;
  std::size_t free = 0;
  for (const GridPos cell : store_cells_) {
    const FoodPile* pile = pile_at(cell);
    if (pile == nullptr || pile->amount == 0) {
      if (free < kCandidates) empty[free++] = cell;
    } else if (pile->nutrient == nutrient && pile->amount < kGrainsPerStoreCell) {
      if (partial < kCandidates) topping_up[partial++] = cell;
    }
    if (partial == kCandidates && free == kCandidates) break;
  }
  if (partial > 0) return topping_up[static_cast<std::size_t>(bias % partial)];
  if (used < allowed && free > 0) return empty[static_cast<std::size_t>(bias % free)];
  return std::nullopt;
}

std::int64_t World::deposit_food(const GridPos cell, const Nutrient nutrient,
                                 const std::int64_t amount) {
  FoodPile* pile = pile_at(cell);
  if (pile == nullptr) {
    pile_index_[grid_index(cell)] = static_cast<std::int32_t>(granary_.size());
    granary_.push_back({cell, nutrient, 0});
    pile = &granary_.back();
  }
  if (pile->amount == 0) pile->nutrient = nutrient;
  if (pile->nutrient != nutrient) return 0;
  const std::int64_t placed = std::min(amount, kGrainsPerStoreCell - pile->amount);
  pile->amount += placed;
  store_for(nutrient) += placed;
  return placed;
}

void World::stock_granary(const Nutrient nutrient, std::int64_t amount) {
  for (FoodPile& pile : granary_) {
    if (pile.nutrient == nutrient) pile.amount = 0;
  }
  store_for(nutrient) = 0;
  while (amount > 0) {
    const std::optional<GridPos> cell = store_target(nutrient, 0);
    if (!cell) break;
    const std::int64_t placed = deposit_food(*cell, nutrient, amount);
    if (placed <= 0) break;
    amount -= placed;
  }
}

std::optional<GridPos> World::nursery_target(const EntityId ignore_brood) const {
  for (const GridPos cell : nursery_cells_) {
    if (cell == home_) continue; // the queen's own cell stays clear
    const bool taken = std::any_of(brood_.begin(), brood_.end(), [&](const BroodSnapshot& item) {
      return item.id != ignore_brood && item.carried_by == 0 && item.position == cell;
    });
    if (!taken) return cell;
  }
  return std::nullopt;
}

void World::store_cargo(const entt::entity entity, int& path_budget) {
  Cargo& cargo = registry_.get<Cargo>(entity);
  Forager& forager = registry_.get<Forager>(entity);
  Movement& movement = registry_.get<Movement>(entity);
  if (cargo.kind != CargoKind::Food || cargo.amount <= 0) {
    cargo = Cargo{};
    forager.state = ForageState::AtHome;
    forager.source_index = -1;
    clear_path(movement);
    return;
  }
  // Keep walking to the heap this load was assigned, and only look for another once that heap can
  // no longer take it.
  if (!accepts_food(forager.store_cell, cargo.nutrient)) {
    const std::optional<GridPos> cell =
        store_target(cargo.nutrient, registry_.get<Identity>(entity).id);
    if (!cell) {
      forager.state = ForageState::WaitingForStorage;
      clear_path(movement);
      return;
    }
    forager.store_cell = *cell;
    clear_path(movement);
  }
  if (registry_.get<Position>(entity).cell() != forager.store_cell) {
    route_to(entity, forager.store_cell, path_budget);
    return;
  }
  const std::int64_t placed = deposit_food(forager.store_cell, cargo.nutrient, cargo.amount);
  cargo.amount -= placed;
  carried_food_[static_cast<std::size_t>(cargo.nutrient)] -= placed;
  stats_.delivered += placed;
  if (placed > 0) ++stats_.productive_worker_ticks;
  if (cargo.amount == 0) {
    cargo.kind = CargoKind::None;
    forager.state = ForageState::AtHome;
    forager.source_index = -1;
    ++stats_.completed_round_trips;
    clear_path(movement);
  }
}


void World::step() {
  refill_sources(); refresh_home_field();
  for (const entt::entity e : ordered_entities_) { Position& p = registry_.get<Position>(e); p.previous_x_subcells = p.x_subcells; p.previous_y_subcells = p.y_subcells; }
  if (tick_ % kTicksPerSecond == 0) recompute_needs_and_dig_plan();
  task_diagnostics_.workers_by_task.fill(0);
  refresh_dig_claims();
  int path_budget = 16; for (const entt::entity e : ordered_entities_) if (registry_.all_of<WorkerMind>(e)) process_worker(e, path_budget);
  if ((tick_ + 1) % kTicksPerSecond == 0) update_biology();
  if ((tick_ + 1) % 4 == 0) update_trails();
  ++tick_;
}
void World::run_ticks(const Tick count) { for (Tick i = 0; i < count; ++i) step(); }

void World::recompute_needs_and_dig_plan() {
  refresh_zones();
  if (frontier_revision_ != grid_.navigation_revision()) {
    frontier_revision_ = grid_.navigation_revision();
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight)); std::queue<GridPos> queue;
    queue.push(home_); visited[grid_index(home_)] = 1; connected_nest_air_ = 0; dig_candidates_.clear();
    while (!queue.empty()) {
      const GridPos p = queue.front(); queue.pop(); if (grid_.at(p) != Material::Air) continue; ++connected_nest_air_;
      bool touches_ground = false;
      for (const GridPos d : kNeighbors) { const GridPos n{p.x + d.x, p.y + d.y}; if (!grid_.in_bounds(n)) continue;
        if (grid_.at(n) == Material::Air && visited[grid_index(n)] == 0) { visited[grid_index(n)] = 1; queue.push(n); }
        else if (is_diggable(grid_.at(n)) && within_envelope(n, home_)) touches_ground = true;
      }
      // A face starts from open ground it can stand in, not from the wall itself, so its heading
      // has somewhere to come from.
      if (touches_ground && within_envelope(p, home_)) dig_candidates_.push_back(p);
    }
  }
  // Brood needs a cradle, so the nursery is no larger than the cells that exist around the queen.
  nursery_capacity_ =
      std::max(12, std::min(connected_nest_air_ / 4, static_cast<int>(nursery_cells_.size())));
  // A chamber is dug when the colony is out of room for brood or for grain, which is what turns a
  // full larder into new excavation rather than idle workers.
  const bool wants_chamber = brood_.size() * 4 >= static_cast<std::size_t>(nursery_capacity_ * 3) ||
                             stores_.carbohydrate * 4 >= stores_.carbohydrate_capacity * 3 ||
                             stores_.protein * 4 >= stores_.protein_capacity * 3;
  dig_plan_.update(grid_, home_, seed_, dig_candidates_, wants_chamber);
  const auto shortage = [](const std::int64_t amount, const std::int64_t target) { return amount >= target ? std::uint16_t{0} : static_cast<std::uint16_t>(((target - amount) * 1'000) / target); };
  task_diagnostics_.stimuli[0] = std::max(shortage(stores_.carbohydrate, stores_.carbohydrate_capacity / 2), shortage(stores_.protein, stores_.protein_capacity / 2));
  task_diagnostics_.stimuli[1] = dig_plan_.idle() ? 0 : static_cast<std::uint16_t>(brood_.size() * 4 >= static_cast<std::size_t>(nursery_capacity_ * 3) ? 850 : 380);
  const std::size_t uncared = static_cast<std::size_t>(std::count_if(brood_.begin(), brood_.end(), [](const BroodSnapshot& b) { return b.care_remaining == 0; }));
  task_diagnostics_.stimuli[2] = brood_.empty() ? 0 : static_cast<std::uint16_t>((uncared * 1'000U) / brood_.size());
  const std::size_t cleanable = static_cast<std::size_t>(std::count_if(corpses_.begin(), corpses_.end(), [](const CorpseSnapshot& c) { return c.cleanable; }));
  task_diagnostics_.stimuli[3] = static_cast<std::uint16_t>(
      std::min<std::size_t>(1'000, cleanable * 350 + dropped_food_.size() * 500));
}

void World::choose_task(const entt::entity e) {
  WorkerMind& mind = registry_.get<WorkerMind>(e); const std::int64_t emergency = static_cast<std::int64_t>(ordered_entities_.size()) * 30 + 200;
  if (stores_.carbohydrate < emergency && sources_[0].amount > sources_[0].reserved) mind.task = Task::Forage;
  else {
    const bool food = sources_[0].amount + sources_[1].amount > sources_[0].reserved + sources_[1].reserved;
    const bool clean = !dropped_food_.empty() ||
                       std::any_of(corpses_.begin(), corpses_.end(),
                                   [](const CorpseSnapshot& c) { return c.cleanable; });
    std::array<TaskWeight, 5> weights{{
      {Task::Forage, food ? response_weight(task_diagnostics_.stimuli[0], mind.thresholds[0]) : 0},
      {Task::Excavate, dig_plan_.idle() ? 0 : response_weight(task_diagnostics_.stimuli[1], mind.thresholds[1])},
      {Task::Nurse, brood_.empty() ? 0 : response_weight(task_diagnostics_.stimuli[2], mind.thresholds[2])},
      {Task::Clean, clean ? response_weight(task_diagnostics_.stimuli[3], mind.thresholds[3]) : 0}, {Task::Idle, 1'000}}};
    for (TaskWeight& weight : weights) {
      const bool focused = (focus_ == Focus::Foraging && weight.task == Task::Forage) ||
                           (focus_ == Focus::Expansion && weight.task == Task::Excavate) ||
                           (focus_ == Focus::Growth && weight.task == Task::Nurse);
      if (focused) weight.weight = weight.weight * 3U / 2U;
      if (weight.task == mind.task) weight.weight = weight.weight * 5U / 4U;
    }
    mind.task = choose_weighted_task(weights, behavior_rng_);
  }
  // Switching task also gives up any dig face this worker was committed to.
  release_dig_claim(mind);
  mind.committed_until = tick_ + 5 * kTicksPerSecond; mind.has_target = false; mind.action_ticks = 0;
}

void World::process_worker(const entt::entity e, int& path_budget) {
  WorkerMind& mind = registry_.get<WorkerMind>(e); Cargo& cargo = registry_.get<Cargo>(e);
  if (cargo.kind == CargoKind::Food) {
    mind.task = Task::Forage;
    process_forager(e, path_budget);
    ++task_diagnostics_.workers_by_task[static_cast<std::size_t>(Task::Forage)];
    return;
  }
  if (((tick_ + registry_.get<Identity>(e).id) % 10 == 0 && tick_ >= mind.committed_until) || (mind.task == Task::Excavate && dig_plan_.idle()) || (mind.task == Task::Nurse && brood_.empty())) choose_task(e);
  ++task_diagnostics_.workers_by_task[static_cast<std::size_t>(mind.task)];
  if (cargo.kind == CargoKind::Spoil || cargo.kind == CargoKind::Corpse) { deliver_non_food(e); return; }
  if (mind.task != Task::Forage) { Forager& f = registry_.get<Forager>(e); if (f.reserved_amount > 0) release_reservation(f); f.state = ForageState::AtHome; }
  switch (mind.task) { case Task::Forage: process_forager(e, path_budget); break; case Task::Excavate: process_excavator(e, path_budget); break; case Task::Nurse: process_nurse(e, path_budget); break; case Task::Clean: process_cleaner(e, path_budget); break; case Task::Idle: break; }
}

void World::process_forager(const entt::entity e, int& path_budget) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); Cargo& cargo = registry_.get<Cargo>(e);
  if (f.state == ForageState::ToSource && f.reserved_amount > 0 && tick_ >= f.reservation_expiry) { release_reservation(f); clear_path(m); f.state = ForageState::AtHome; f.retry_after = tick_ + 200; }
  if (f.state == ForageState::WaitingForStorage) {
    // Holds the load until a chamber has room again, rather than dissolving it into a counter.
    if (!store_target(cargo.nutrient, registry_.get<Identity>(e).id)) return;
    f.state = ForageState::Storing;
  }
  if (f.state == ForageState::Storing) { store_cargo(e, path_budget); return; }
  if (f.state == ForageState::AtHome) {
    if (cargo.amount > 0) { f.state = ForageState::Returning; m.path = home_field_.path_home(grid_, registry_.get<Position>(e).cell(), bias_for(e)); m.next_cell = 0; m.path_revision = grid_.navigation_revision(); }
    else if (tick_ >= f.retry_after) choose_source(e, path_budget);
  }
  if ((f.state == ForageState::ToSource || f.state == ForageState::Returning) && m.path_revision != grid_.navigation_revision()) { ++stats_.navigation_replans; refresh_path(e, path_budget); }
  if ((f.state == ForageState::ToSource || f.state == ForageState::Returning) && move_one_tick(e)) arrive(e, path_budget);
}

void World::choose_source(const entt::entity e, int& path_budget) {
  if (path_budget <= 0) return; const EntityId id = registry_.get<Identity>(e).id; Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); const GridPos start = registry_.get<Position>(e).cell();
  // Follow the nutrient the colony is actually short of, measured against the same 50%-of-capacity
  // target the foraging stimulus uses and counting food already on its way home. Trails only break
  // a tie, so a well-worn route to a full larder cannot starve the other nutrient.
  const auto shortfall = [this](const Nutrient nutrient) {
    const std::int64_t target = capacity_for(nutrient) / 2;
    if (target <= 0) return std::int64_t{0};
    const std::int64_t have =
        store_for(nutrient) + carried_food_[static_cast<std::size_t>(nutrient)];
    return have >= target ? std::int64_t{0} : (target - have) * 1'000 / target;
  };
  const std::int64_t first_need = shortfall(sources_[0].nutrient);
  const std::int64_t second_need = shortfall(sources_[1].nutrient);
  const std::uint16_t first_trail = trails_.at(sources_[0].position);
  const std::uint16_t second_trail = trails_.at(sources_[1].position);
  const int preferred =
      first_need != second_need
          ? (first_need > second_need ? 0 : 1)
          : (first_trail == second_trail ? static_cast<int>(id % 2ULL)
                                         : (first_trail > second_trail ? 0 : 1));
  for (int attempt = 0; attempt < 2 && path_budget > 0; ++attempt) {
    const int source_index = (preferred + attempt) % 2; FoodSource& source = sources_[static_cast<std::size_t>(source_index)];
    const std::int64_t carry_capacity =
        2'000 * (100 + 20 * adaptation_levels_[2]) / 100 * (traits_.industry_tier >= 3 ? 120 : 100) / 100;
    const std::int64_t room = capacity_for(source.nutrient) - store_for(source.nutrient) -
                              carried_food_[static_cast<std::size_t>(source.nutrient)] - source.reserved;
    const std::int64_t reservation = std::min<std::int64_t>({carry_capacity, source.amount - source.reserved, room}); if (reservation <= 0) continue;
    source.reserved += reservation; f.source_index = source_index; f.reserved_amount = reservation; f.reservation_expiry = tick_ + 1'200; ++stats_.path_requests; --path_budget;
    const PathResult path = find_path(grid_, start, source.position, 4096, bias_for(e)); stats_.path_expansions += path.expanded;
    if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); f.state = ForageState::ToSource; return; }
    release_reservation(f); f.retry_after = tick_ + 200; if (path.status == PathStatus::BudgetExhausted) return;
  }
}

void World::refresh_path(const entt::entity e, int& path_budget) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); const GridPos start = registry_.get<Position>(e).cell(); clear_path(m);
  if (f.state == ForageState::Returning) { m.path = home_field_.path_home(grid_, start, bias_for(e)); m.path_revision = grid_.navigation_revision(); return; }
  if (f.state != ForageState::ToSource || f.source_index < 0 || path_budget <= 0) return; FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; ++stats_.path_requests; --path_budget;
  const PathResult path = find_path(grid_, start, source.position, 4096, bias_for(e)); stats_.path_expansions += path.expanded;
  if (path.status == PathStatus::Complete) { m.path = path.cells; m.path_revision = grid_.navigation_revision(); }
  else { release_reservation(f); f.state = ForageState::AtHome; f.retry_after = tick_ + 200; }
}

bool World::move_one_tick(const entt::entity e) {
  Movement& m = registry_.get<Movement>(e); Position& p = registry_.get<Position>(e); if (m.next_cell >= m.path.size()) return true; const GridPos next = m.path[m.next_cell]; if (!grid_.walkable(next)) return false;
  const std::int32_t before_x = p.x_subcells;
  const std::int32_t before_y = p.y_subcells;
  m.speed_residual += 6 * kSubcellsPerCell * (traits_.industry_tier >= 2 ? 110 : 100) / 100; const int travel = m.speed_residual / kTicksPerSecond; m.speed_residual %= kTicksPerSecond;
  const std::int32_t tx = cell_center(next.x), ty = cell_center(next.y);
  const std::int32_t dx = tx - p.x_subcells, dy = ty - p.y_subcells;
  // Spend the tick's travel along x first, then any remainder along y, never stepping past the
  // target on either axis. A fixed-size step used to overshoot the target column and the ant would
  // ping-pong across it forever without ever closing the distance in y.
  std::int32_t remaining = travel;
  if (dx != 0) {
    const std::int32_t step = std::min(remaining, std::abs(dx));
    p.x_subcells += dx > 0 ? step : -step;
    remaining -= step;
  }
  if (remaining > 0 && dy != 0) {
    const std::int32_t step = std::min(remaining, std::abs(dy));
    p.y_subcells += dy > 0 ? step : -step;
    remaining -= step;
  }
  if (p.x_subcells == tx && p.y_subcells == ty) ++m.next_cell;
  if (p.x_subcells != before_x || p.y_subcells != before_y) ++stats_.productive_worker_ticks;
  return m.next_cell >= m.path.size();
}

void World::arrive(const entt::entity e, int&) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); Cargo& cargo = registry_.get<Cargo>(e);
  if (f.state == ForageState::ToSource) {
    FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; const std::int64_t picked = std::min(source.amount, f.reserved_amount); source.amount -= picked; source.reserved -= f.reserved_amount; f.reserved_amount = 0;
    if (picked <= 0) { f.source_index = -1; f.state = ForageState::AtHome; f.retry_after = tick_ + 100; clear_path(m); return; }
    cargo.kind = CargoKind::Food; cargo.nutrient = source.nutrient; cargo.amount = picked; carried_food_[static_cast<std::size_t>(source.nutrient)] += picked; stats_.picked_up += picked; f.state = ForageState::Returning;
    m.path = home_field_.path_home(grid_, registry_.get<Position>(e).cell(), bias_for(e)); m.next_cell = 0; m.path_revision = grid_.navigation_revision(); return;
  }
  if (f.state == ForageState::Returning) {
    // Home is only the hall: the load still has to be carried on to the heap it belongs on.
    clear_path(m);
    f.state = ForageState::Storing;
  }
}

void World::release_reservation(Forager& f) { if (f.source_index >= 0 && f.reserved_amount > 0) { FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; source.reserved = std::max<std::int64_t>(0, source.reserved - f.reserved_amount); } f.source_index = -1; f.reserved_amount = 0; }

void World::route_to(const entt::entity e, const GridPos target, int& path_budget) {
  Movement& m = registry_.get<Movement>(e); if (m.next_cell < m.path.size() && m.path_revision == grid_.navigation_revision()) { static_cast<void>(move_one_tick(e)); return; }
  if (registry_.get<Position>(e).cell() == target || path_budget <= 0) return; ++stats_.path_requests; --path_budget; const PathResult path = find_path(grid_, registry_.get<Position>(e).cell(), target, 4096, bias_for(e)); stats_.path_expansions += path.expanded;
  if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); static_cast<void>(move_one_tick(e)); }
}

void World::process_excavator(const entt::entity e, int& path_budget) {
  WorkerMind& mind = registry_.get<WorkerMind>(e);
  if (!mind.has_target || !grid_.in_bounds(mind.target) || !is_diggable(grid_.at(mind.target))) {
    // Give up the old claim before taking a new one, or this worker is counted on two faces.
    release_dig_claim(mind);
    mind.has_target = false;
    if (const auto claimed = dig_plan_.claim(grid_, home_)) {
      mind.target = claimed->cell;
      mind.has_target = true;
      // Stay committed long enough to actually get there and take a bite. A face at the far end of
      // the nest is a ten-second walk, and a worker that reconsidered every five seconds turned
      // back before it ever arrived; a face next door still costs nothing extra.
      const GridPos here = registry_.get<Position>(e).cell();
      const int reach = std::abs(claimed->cell.x - here.x) + std::abs(claimed->cell.y - here.y);
      mind.committed_until = tick_ + static_cast<Tick>(5 + reach / 6) * kTicksPerSecond;
    }
  }
  if (!mind.has_target) return; const GridPos current = registry_.get<Position>(e).cell(); GridPos work = current; bool adjacent = false;
  for (const GridPos d : kNeighbors) { const GridPos candidate{mind.target.x + d.x, mind.target.y + d.y}; if (grid_.walkable(candidate)) { work = candidate; adjacent = candidate == current; if (adjacent) break; } }
  if (!adjacent) { route_to(e, work, path_budget); return; } if (++mind.action_ticks < 10) return; mind.action_ticks = 0; std::uint16_t& remaining = dig_work_[grid_index(mind.target)]; if (remaining == 0) remaining = dig_effort(grid_.at(mind.target));
  const std::uint16_t dig_amount = static_cast<std::uint16_t>(
      (100 + 25 * adaptation_levels_[0]) * (traits_.industry_tier >= 1 ? 115 : 100) / 100);
  ++stats_.productive_worker_ticks;
  if (remaining <= dig_amount) { remaining = 0; grid_.set(mind.target, Material::Air); Cargo& cargo = registry_.get<Cargo>(e); cargo.kind = CargoKind::Spoil; cargo.amount = 1'000; ++stats_.cells_excavated; release_dig_claim(mind); mind.has_target = false; }
  else remaining = static_cast<std::uint16_t>(remaining - dig_amount);
}

void World::process_nurse(const entt::entity e, int& path_budget) {
  const EntityId id = registry_.get<Identity>(e).id;
  const GridPos here = registry_.get<Position>(e).cell();
  // Carrying an egg: walk it to a free cradle and set it down there.
  const auto carried = std::find_if(brood_.begin(), brood_.end(),
      [id](const BroodSnapshot& item) { return item.carried_by == id; });
  if (carried != brood_.end()) {
    carried->position = here;
    const std::optional<GridPos> cradle = nursery_target(carried->id);
    if (!cradle) { carried->carried_by = 0; return; }
    if (here != *cradle) { route_to(e, *cradle, path_budget); return; }
    carried->carried_by = 0;
    ++stats_.productive_worker_ticks;
    return;
  }
  // Brood that has ended up outside the ring around the queen is fetched back before any tending.
  const auto stray = std::find_if(brood_.begin(), brood_.end(), [this](const BroodSnapshot& item) {
    return item.carried_by == 0 && !in_nursery(item.position);
  });
  if (stray != brood_.end() && nursery_target(stray->id)) {
    if (here != stray->position) { route_to(e, stray->position, path_budget); return; }
    stray->carried_by = id;
    return;
  }
  if (here != home_) { route_to(e, home_, path_budget); return; } WorkerMind& mind = registry_.get<WorkerMind>(e); if (++mind.action_ticks < kTicksPerSecond) return; mind.action_ticks = 0; if (brood_.empty()) return;
  auto item = std::min_element(brood_.begin(), brood_.end(), [](const BroodSnapshot& a, const BroodSnapshot& b) { return a.care_remaining != b.care_remaining ? a.care_remaining < b.care_remaining : a.id < b.id; });
  if (item->care_remaining < 5 * kTicksPerSecond) { item->care_remaining = 5 * kTicksPerSecond; ++stats_.productive_worker_ticks; }
}

void World::process_cleaner(const entt::entity e, int& path_budget) {
  WorkerMind& mind = registry_.get<WorkerMind>(e);
  if (!dropped_food_.empty()) {
    auto dropped = std::min_element(dropped_food_.begin(), dropped_food_.end(),
        [](const DroppedCargoSnapshot& a, const DroppedCargoSnapshot& b) { return a.id < b.id; });
    if (registry_.get<Position>(e).cell() != dropped->position) {
      route_to(e, dropped->position, path_budget);
      return;
    }
    Cargo& cargo = registry_.get<Cargo>(e);
    cargo.kind = CargoKind::Food;
    cargo.nutrient = dropped->nutrient;
    cargo.amount = dropped->amount;
    carried_food_[static_cast<std::size_t>(cargo.nutrient)] += cargo.amount;
    dropped_food_.erase(dropped);
    return;
  }
  auto corpse = std::find_if(corpses_.begin(), corpses_.end(),
                             [](const CorpseSnapshot& c) { return c.cleanable; });
  if (corpse == corpses_.end()) { mind.has_target = false; return; }
  if (registry_.get<Position>(e).cell() != corpse->position) { route_to(e, corpse->position, path_budget); return; }
  Cargo& cargo = registry_.get<Cargo>(e); cargo.kind = CargoKind::Corpse; cargo.entity_id = corpse->id; cargo.amount = 1; corpses_.erase(corpse);
}

void World::deliver_non_food(const entt::entity e) {
  Cargo& cargo = registry_.get<Cargo>(e); Movement& m = registry_.get<Movement>(e); const GridPos outlet{home_.x, 31};
  if (registry_.get<Position>(e).cell() != outlet) { if (m.next_cell >= m.path.size() || m.path_revision != grid_.navigation_revision()) { const PathResult path = find_path(grid_, registry_.get<Position>(e).cell(), outlet, 4096, bias_for(e)); if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); } } static_cast<void>(move_one_tick(e)); return; }
  // The apron is deliberately bounded, so a full one does not trap the excavator holding the grain.
  // The haul still counts as work done; the grain is recorded as overflow tipped out of view rather
  // than silently counted as mound, which is the only way the two stay reconcilable.
  if (cargo.kind == CargoKind::Spoil) { if (deposit_spoil()) ++spoil_mound_; else ++stats_.spoil_overflow; ++stats_.spoil_delivered; } else if (cargo.kind == CargoKind::Corpse) ++stats_.corpses_cleaned; cargo = Cargo{}; clear_path(m);
}

bool World::consume(const Nutrient nutrient, const std::int64_t amount) {
  std::int64_t& store = store_for(nutrient);
  if (store < amount) return false;
  store -= amount;
  // Eaten out of the nearest heaps first, so the chambers by the queen empty before the far ones.
  std::int64_t remaining = amount;
  for (FoodPile& pile : granary_) {
    if (remaining <= 0) break;
    if (pile.nutrient != nutrient || pile.amount <= 0) continue;
    const std::int64_t taken = std::min(remaining, pile.amount);
    pile.amount -= taken;
    remaining -= taken;
  }
  if (nutrient == Nutrient::Carbohydrate) stats_.consumed_carbohydrate += amount;
  else stats_.consumed_protein += amount;
  return true;
}
Tick World::brood_target(const BroodStage stage, const BroodRole role) const {
  Tick base = stage == BroodStage::Egg ? 30 * kTicksPerSecond
            : stage == BroodStage::Larva ? 60 * kTicksPerSecond : 45 * kTicksPerSecond;
  if (stage == BroodStage::Egg && traits_.vigor_tier >= 1) base = base * 90 / 100;
  if (stage != BroodStage::Egg && traits_.vigor_tier >= 3) base = base * 85 / 100;
  return role == BroodRole::Gyne ? base * 2 : base;
}

void World::update_biology() {
  bool queen_fed = false;
  const std::int64_t queen_carbs = traits_.vigor_tier >= 4 ? 10 : 20;
  const std::int64_t queen_protein = traits_.vigor_tier >= 4 ? 5 : 10;
  if (queen_alive_) { queen_fed = stores_.carbohydrate >= queen_carbs && stores_.protein >= queen_protein; if (queen_fed) { static_cast<void>(consume(Nutrient::Carbohydrate, queen_carbs)); static_cast<void>(consume(Nutrient::Protein, queen_protein)); queen_starvation_ = 0; } else queen_starvation_ += kTicksPerSecond; if (queen_starvation_ >= 300 * kTicksPerSecond) { queen_alive_ = false; decline_ = true; const entt::entity queen = ordered_entities_.front(); corpses_.push_back({registry_.get<Identity>(queen).id, home_, 0, false}); registry_.destroy(queen); ordered_entities_.erase(ordered_entities_.begin()); ++stats_.deaths; } }
  for (const entt::entity e : ordered_entities_) if (registry_.all_of<Life>(e)) {
    Life& life = registry_.get<Life>(e); life.age += kTicksPerSecond;
    const bool winged = registry_.get<Ant>(e).kind == AntKind::WingedQueen;
    // A ration is all-or-nothing: a partial meal never resets the starvation timer.
    const bool fed = winged ? (stores_.carbohydrate >= 3 && stores_.protein >= 2 &&
                               consume(Nutrient::Carbohydrate, 3) && consume(Nutrient::Protein, 2))
                            : consume(Nutrient::Carbohydrate, 3);
    if (fed) life.starvation = 0; else life.starvation += kTicksPerSecond;
  }
  std::vector<EntityId> matured;
  std::vector<BroodRole> matured_roles;
  for (BroodSnapshot& item : brood_) { item.target = brood_target(item.stage, item.role); bool fed = true; if (item.stage == BroodStage::Larva) { const std::int64_t carbs = item.role == BroodRole::Gyne ? 16 : 8; const std::int64_t protein = item.role == BroodRole::Gyne ? 30 : 15; fed = stores_.carbohydrate >= carbs && stores_.protein >= protein; if (fed) { static_cast<void>(consume(Nutrient::Carbohydrate, carbs)); static_cast<void>(consume(Nutrient::Protein, protein)); item.starvation = 0; } else item.starvation += kTicksPerSecond; }
    const bool cared = item.care_remaining > 0;
    if (item.care_remaining > 0) item.care_remaining -= std::min<Tick>(item.care_remaining, kTicksPerSecond);
    if (fed) { const Tick base = cared ? kTicksPerSecond : kTicksPerSecond / 2; item.progress += base * static_cast<Tick>(100 + 10 * adaptation_levels_[1]) / 100; }
    if (item.stage != BroodStage::Larva || item.starvation < 180 * kTicksPerSecond) if (item.progress >= item.target) { item.progress = 0; if (item.stage == BroodStage::Egg) item.stage = BroodStage::Larva; else if (item.stage == BroodStage::Larva) item.stage = BroodStage::Pupa; else { matured.push_back(item.id); matured_roles.push_back(item.role); } }
  }
  brood_.erase(std::remove_if(brood_.begin(), brood_.end(), [this, &matured](const BroodSnapshot& item) { const bool starved = item.stage == BroodStage::Larva && item.starvation >= 180 * kTicksPerSecond; if (starved) ++stats_.deaths; return starved || std::find(matured.begin(), matured.end(), item.id) != matured.end(); }), brood_.end());
  for (std::size_t index = 0; index < matured.size(); ++index) {
    if (matured_roles[index] == BroodRole::Gyne) { spawn_winged_queen(home_); ++stats_.gynes_born; }
    else { spawn_worker(home_); ++stats_.workers_born; }
  }
  if (queen_alive_ && queen_fed && tick_ + 1 >= next_laying_) { const Tick interval = static_cast<Tick>(12 * kTicksPerSecond * 100 / (100 + 15 * adaptation_levels_[3])); next_laying_ = tick_ + 1 + std::max<Tick>(1, interval); if (brood_.size() < static_cast<std::size_t>(nursery_capacity_)) {
    BroodRole role = BroodRole::Worker;
    if (mature_) {
      ++egg_assignment_counter_;
      // One in five, and only while live winged queens plus winged brood stay under ten.
      if (egg_assignment_counter_ % 5 == 0 && live_winged_queens() + winged_brood() < 10) role = BroodRole::Gyne;
    }
    const GridPos cradle = nursery_target(0).value_or(home_);
    brood_.push_back({next_id_++, BroodStage::Egg, cradle, 0, brood_target(BroodStage::Egg, role), 0, 0, role, 0});
    ++stats_.eggs_laid; } }
  remove_dead_workers(); for (CorpseSnapshot& corpse : corpses_) { corpse.age += kTicksPerSecond; corpse.cleanable = corpse.age >= 30 * kTicksPerSecond; }
  corpses_.erase(std::remove_if(corpses_.begin(), corpses_.end(), [](const CorpseSnapshot& c) { return c.age >= 300 * kTicksPerSecond; }), corpses_.end());
  for (DroppedCargoSnapshot& dropped : dropped_food_) dropped.age += kTicksPerSecond;
  dropped_food_.erase(std::remove_if(dropped_food_.begin(), dropped_food_.end(),
      [this](const DroppedCargoSnapshot& dropped) {
        if (dropped.age < 120 * kTicksPerSecond) return false;
        stats_.decayed_food += dropped.amount;
        return true;
      }), dropped_food_.end());
  update_maturity();
  const bool has_survivors = std::any_of(ordered_entities_.begin(), ordered_entities_.end(), [this](const entt::entity e) { return registry_.all_of<Life>(e); });
  extinct_ = !queen_alive_ && !has_survivors && brood_.empty();
}

bool World::cell_is_occupied(const GridPos cell) const {
  for (const entt::entity entity : ordered_entities_) {
    if (registry_.get<Position>(entity).cell() == cell) return true;
  }
  for (const CorpseSnapshot& corpse : corpses_) if (corpse.position == cell) return true;
  for (const DroppedCargoSnapshot& dropped : dropped_food_) if (dropped.position == cell) return true;
  for (const BroodSnapshot& item : brood_) if (item.position == cell) return true;
  return false;
}

bool World::deposit_spoil() {
  // Excavated grains are carried out and tipped onto the surface, so the crater around the
  // entrance is real terrain the colony built rather than a drawn-on decoration. Grains settle on
  // the lowest nearby pile, which grows a cone outward instead of a tower.
  constexpr int kFirstColumn = 2;   // leaves the entrance shaft and its shoulders clear
  constexpr int kLastColumn = 44;
  // The apron is a cone: tallest beside the entrance and tapering to nothing at the rim, so a
  // forager walks up a slope instead of over a wall. A flat cap instead of a profile made the
  // spoil saturate into a plateau once the colony started digging in earnest.
  constexpr int kPeak = 6;
  constexpr int kColumnsPerStep = 6;
  int best_score = 0;
  bool found = false;
  GridPos best{};
  for (int radius = kFirstColumn; radius <= kLastColumn; ++radius) {
    for (const int side : {-1, 1}) {
      const int column = home_.x + side * radius;
      if (column < 0 || column >= Grid::kWidth) continue;
      int height = 0;
      while (height < 32 && !is_passable(grid_.at({column, 31 - height}))) ++height;
      const int allowed = kPeak - (radius - kFirstColumn) / kColumnsPerStep;
      if (allowed <= 0 || height >= allowed) continue;
      const GridPos target{column, 31 - height};
      if (grid_.at(target) != Material::Sky) continue;
      if (cell_is_occupied(target)) continue;
      // Favour low piles close to the entrance, so the mound stays a mound.
      const int score = height * 3 + radius;
      if (!found || score < best_score) { found = true; best_score = score; best = target; }
    }
  }
  if (!found) return false;
  grid_.set(best, Material::Soil);
  return true;
}

RouteBias World::bias_for(const entt::entity e) const {
  return route_bias(seed_, registry_.get<Identity>(e).id);
}

void World::release_dig_claim(const WorkerMind& mind) {
  if (!mind.has_target) return;
  if (const auto face = dig_plan_.face_of(grid_, mind.target, home_)) dig_plan_.release(*face);
}

void World::refresh_dig_claims() {
  dig_plan_.clear_claims();
  for (const entt::entity entity : ordered_entities_) {
    if (!registry_.all_of<WorkerMind>(entity)) continue;
    const WorkerMind& mind = registry_.get<WorkerMind>(entity);
    if (mind.task != Task::Excavate || !mind.has_target) continue;
    if (const auto face = dig_plan_.face_of(grid_, mind.target, home_)) dig_plan_.note_claim(*face);
  }
}

void World::update_maturity() {
  if (mature_) return; // Latched: a later population dip never revokes it.
  mature_ = living_workers() >= 100 && stats_.workers_born >= 150 &&
            tick_ >= 720 * static_cast<Tick>(kTicksPerSecond);
}

int World::living_workers() const {
  return static_cast<int>(std::count_if(ordered_entities_.begin(), ordered_entities_.end(),
      [this](const entt::entity e) { return registry_.all_of<WorkerMind>(e); }));
}

int World::live_winged_queens() const {
  return static_cast<int>(std::count_if(ordered_entities_.begin(), ordered_entities_.end(),
      [this](const entt::entity e) { return registry_.get<Ant>(e).kind == AntKind::WingedQueen; }));
}

int World::winged_brood() const {
  return static_cast<int>(std::count_if(brood_.begin(), brood_.end(),
      [](const BroodSnapshot& item) { return item.role == BroodRole::Gyne; }));
}

void World::remove_dead_workers() {
  std::vector<entt::entity> dead;
  for (const entt::entity e : ordered_entities_) if (registry_.all_of<Life>(e)) { const Life& life = registry_.get<Life>(e); if (life.age < life.lifespan && life.starvation < 120 * kTicksPerSecond) continue; const Cargo cargo = registry_.get<Cargo>(e); const GridPos p = registry_.get<Position>(e).cell(); if (cargo.kind == CargoKind::Corpse) corpses_.push_back({cargo.entity_id, p, 30 * kTicksPerSecond, true}); else if (cargo.kind == CargoKind::Food && cargo.amount > 0) { dropped_food_.push_back({next_id_++, p, cargo.nutrient, cargo.amount, 0}); carried_food_[static_cast<std::size_t>(cargo.nutrient)] -= cargo.amount; } corpses_.push_back({registry_.get<Identity>(e).id, p, 0, false}); if (registry_.all_of<Forager>(e)) release_reservation(registry_.get<Forager>(e)); dead.push_back(e); ++stats_.deaths; }
  for (const entt::entity e : dead) {
    const EntityId id = registry_.get<Identity>(e).id;
    for (BroodSnapshot& item : brood_) if (item.carried_by == id) item.carried_by = 0;
    registry_.destroy(e);
  }
  ordered_entities_.erase(std::remove_if(ordered_entities_.begin(), ordered_entities_.end(), [&dead](const entt::entity e) { return std::find(dead.begin(), dead.end(), e) != dead.end(); }), ordered_entities_.end());
}

// A loaded forager marks its route the whole way in, from the source to the heap she sets the load
// down on, so the trail survives the leg through the nest as well as the walk home.
void World::update_trails() {
  for (const entt::entity e : ordered_entities_) {
    if (!registry_.all_of<Forager>(e)) continue;
    const ForageState state = registry_.get<Forager>(e).state;
    if (state != ForageState::Returning && state != ForageState::Storing) continue;
    trails_.deposit(registry_.get<Position>(e).cell(), 900);
  }
  trails_.update(grid_);
}

std::int64_t& World::store_for(const Nutrient n) { return n == Nutrient::Carbohydrate ? stores_.carbohydrate : stores_.protein; }
std::int64_t World::store_for(const Nutrient n) const { return n == Nutrient::Carbohydrate ? stores_.carbohydrate : stores_.protein; }
std::int64_t World::capacity_for(const Nutrient n) const { return n == Nutrient::Carbohydrate ? stores_.carbohydrate_capacity : stores_.protein_capacity; }

std::vector<ActorSnapshot> World::actors() const {
  std::vector<ActorSnapshot> out; out.reserve(ordered_entities_.size());
  for (const entt::entity e : ordered_entities_) { const Identity& id = registry_.get<Identity>(e); const Position& p = registry_.get<Position>(e); const Ant& ant = registry_.get<Ant>(e); const Cargo& cargo = registry_.get<Cargo>(e);
    out.push_back({id.id, ant.kind, registry_.all_of<Forager>(e) ? registry_.get<Forager>(e).state : ForageState::AtHome, static_cast<double>(p.x_subcells) / kSubcellsPerCell, static_cast<double>(p.y_subcells) / kSubcellsPerCell, static_cast<double>(p.previous_x_subcells) / kSubcellsPerCell, static_cast<double>(p.previous_y_subcells) / kSubcellsPerCell, cargo.nutrient, cargo.amount, cargo.kind, registry_.all_of<WorkerMind>(e) ? registry_.get<WorkerMind>(e).task : Task::Idle, registry_.all_of<Life>(e) ? registry_.get<Life>(e).age : 0}); }
  return out;
}
std::vector<BroodSnapshot> World::brood() const { return brood_; }

WorldSnapshot World::snapshot() const {
  WorldSnapshot out;
  out.seed = seed_; out.tick = tick_; out.next_id = next_id_; out.home = home_;
  out.terrain = grid_.cells(); out.sources = sources_; out.stores = stores_; out.stats = stats_;
  out.behavior_rng_state = behavior_rng_.state(); out.behavior_rng_increment = behavior_rng_.increment();
  out.lifecycle_rng_state = lifecycle_rng_.state(); out.lifecycle_rng_increment = lifecycle_rng_.increment();
  out.trails = trails_.cells(); out.dig_work = dig_work_; out.task_diagnostics = task_diagnostics_;
  out.dig_faces = dig_plan_.save(); out.brood = brood_; out.corpses = corpses_; out.dropped_food = dropped_food_;
  out.granary = granary_;
  out.spoil_mound = spoil_mound_; out.starting_nest_air = starting_nest_air_;
  out.connected_nest_air = connected_nest_air_; out.nursery_capacity = nursery_capacity_;
  out.next_laying = next_laying_; out.queen_starvation = queen_starvation_;
  out.queen_alive = queen_alive_; out.decline = decline_; out.extinct = extinct_;
  out.focus = focus_; out.adaptation_levels = adaptation_levels_;
  out.traits = traits_; out.mature = mature_; out.egg_assignment_counter = egg_assignment_counter_;
  out.frontiers_valid = frontier_revision_ == grid_.navigation_revision();
  out.actors.reserve(ordered_entities_.size());
  for (const entt::entity entity : ordered_entities_) {
    ActorState actor;
    actor.identity = registry_.get<Identity>(entity); actor.position = registry_.get<Position>(entity);
    actor.ant = registry_.get<Ant>(entity); actor.cargo = registry_.get<Cargo>(entity);
    actor.worker = registry_.all_of<WorkerMind>(entity);
    if (actor.worker) { actor.movement = registry_.get<Movement>(entity); actor.path_valid = actor.movement.path_revision == grid_.navigation_revision(); actor.forager = registry_.get<Forager>(entity); actor.mind = registry_.get<WorkerMind>(entity); actor.life = registry_.get<Life>(entity); }
    else if (actor.ant.kind == AntKind::WingedQueen) actor.life = registry_.get<Life>(entity);
    out.actors.push_back(std::move(actor));
  }
  return out;
}

std::uint64_t World::canonical_hash() const {
  std::uint64_t hash = 1469598103934665603ULL; hash_value(hash, seed_); hash_value(hash, tick_); hash_value(hash, grid_.material_hash()); hash_value(hash, behavior_rng_.state()); hash_value(hash, lifecycle_rng_.state()); hash_signed(hash, stores_.carbohydrate); hash_signed(hash, stores_.protein); hash_value(hash, trails_.mass()); hash_value(hash, queen_alive_); hash_value(hash, queen_starvation_); hash_value(hash, static_cast<std::uint64_t>(focus_));
  for (const FoodSource& source : sources_) { hash_signed(hash, source.amount); hash_signed(hash, source.reserved); hash_value(hash, source.next_refill); }
  for (const entt::entity e : ordered_entities_) { const Identity& id = registry_.get<Identity>(e); const Position& p = registry_.get<Position>(e); const Cargo& cargo = registry_.get<Cargo>(e); hash_value(hash, id.id); hash_value(hash, static_cast<std::uint64_t>(p.x_subcells)); hash_value(hash, static_cast<std::uint64_t>(p.y_subcells)); hash_signed(hash, cargo.amount); if (registry_.all_of<WorkerMind>(e)) { const WorkerMind& mind = registry_.get<WorkerMind>(e); hash_value(hash, static_cast<std::uint64_t>(mind.task)); hash_value(hash, mind.committed_until); } if (registry_.all_of<Life>(e)) { const Life& life = registry_.get<Life>(e); hash_value(hash, life.age); hash_value(hash, life.lifespan); hash_value(hash, life.starvation); } }
  for (const BroodSnapshot& b : brood_) { hash_value(hash, b.id); hash_value(hash, static_cast<std::uint64_t>(b.stage)); hash_value(hash, static_cast<std::uint64_t>(b.role)); hash_value(hash, b.progress); hash_value(hash, b.starvation); hash_signed(hash, b.position.x); hash_signed(hash, b.position.y); hash_value(hash, b.carried_by); }
  for (const FoodPile& pile : granary_) { hash_signed(hash, pile.position.x); hash_signed(hash, pile.position.y); hash_value(hash, static_cast<std::uint64_t>(pile.nutrient)); hash_signed(hash, pile.amount); } for (const CorpseSnapshot& c : corpses_) { hash_value(hash, c.id); hash_value(hash, c.age); } for (const DroppedCargoSnapshot& dropped : dropped_food_) { hash_value(hash, dropped.id); hash_signed(hash, dropped.amount); hash_value(hash, dropped.age); }
  hash_signed(hash, stats_.delivered); hash_value(hash, stats_.cells_excavated); hash_value(hash, stats_.workers_born); hash_value(hash, stats_.deaths); hash_value(hash, stats_.productive_worker_ticks); hash_value(hash, stats_.gynes_born); for (const auto level : adaptation_levels_) hash_value(hash, level); hash_value(hash, traits_.vigor_tier); hash_value(hash, traits_.industry_tier); hash_value(hash, mature_); hash_value(hash, egg_assignment_counter_);
  for (const DigFace& face : dig_plan_.faces()) { hash_value(hash, face.active); if (!face.active) continue; hash_signed(hash, face.anchor.x); hash_signed(hash, face.anchor.y); hash_signed(hash, face.heading.x); hash_signed(hash, face.heading.y); hash_value(hash, face.length); hash_value(hash, face.chamber); }
  return hash;
}

bool World::invariant_holds() const {
  if (stores_.carbohydrate < 0 || stores_.protein < 0 || stores_.carbohydrate > stores_.carbohydrate_capacity || stores_.protein > stores_.protein_capacity) return false; std::array<std::int64_t, 2> reserved{}; EntityId previous = 0;
  for (const entt::entity e : ordered_entities_) { const Identity& id = registry_.get<Identity>(e); const Position& p = registry_.get<Position>(e); const Cargo& cargo = registry_.get<Cargo>(e); if (id.id <= previous || !grid_.passable(p.cell()) || cargo.amount < 0) return false; previous = id.id; if (registry_.all_of<Forager>(e)) { const Forager& f = registry_.get<Forager>(e); if (f.reserved_amount < 0) return false; if (f.source_index >= 0) reserved[static_cast<std::size_t>(f.source_index)] += f.reserved_amount; } }
  for (std::size_t i = 0; i < sources_.size(); ++i) if (sources_[i].amount < 0 || sources_[i].amount > sources_[i].capacity || sources_[i].reserved != reserved[i] || sources_[i].reserved > sources_[i].amount) return false;
  // Stored food is the sum of the heaps in the nest: the counter can never hold grain no cell does.
  std::array<std::int64_t, 2> stored{};
  for (const FoodPile& pile : granary_) {
    if (pile.amount < 0 || pile.amount > kGrainsPerStoreCell || !grid_.passable(pile.position)) return false;
    stored[static_cast<std::size_t>(pile.nutrient)] += pile.amount;
  }
  if (stored[0] != stores_.carbohydrate || stored[1] != stores_.protein) return false;
  return brood_.size() <= 5'000 &&
         ordered_entities_.size() + brood_.size() + corpses_.size() + dropped_food_.size() <= 20'000;
}

void World::debug_set_source_amount(const std::size_t i, const std::int64_t amount) { FoodSource& source = sources_.at(i); if (amount < 0 || amount > source.capacity) throw std::out_of_range("source amount outside capacity"); source.amount = amount; }
void World::debug_set_source_refill(const std::size_t i, const std::int64_t amount) { if (amount < 0) throw std::out_of_range("source refill cannot be negative"); sources_.at(i).refill_amount = amount; }
void World::debug_set_store(const Nutrient n, const std::int64_t amount) { if (amount < 0 || amount > capacity_for(n)) throw std::out_of_range("store amount outside capacity"); stock_granary(n, amount); }
void World::debug_set_worker_lifespan(const EntityId id, const Tick lifespan) { for (const entt::entity e : ordered_entities_) if (registry_.get<Identity>(e).id == id && registry_.all_of<Life>(e)) registry_.get<Life>(e).lifespan = lifespan; }
void World::debug_spawn_brood(const BroodStage stage, const Tick progress, const Tick starvation, const BroodRole role) { brood_.push_back({next_id_++, stage, home_, progress, brood_target(stage, role), 0, starvation, role}); }
void World::debug_spawn_workers(const int count) { for (int i = 0; i < count; ++i) spawn_worker(home_); }
void World::debug_set_workers_born(const std::uint64_t births) { stats_.workers_born = births; }
void World::debug_clear_brood() { brood_.clear(); }
void World::debug_set_mature() { mature_ = true; }
void World::debug_kill_workers(const int count) {
  int removed = 0;
  std::vector<entt::entity> dead;
  for (const entt::entity e : ordered_entities_) {
    if (removed >= count || !registry_.all_of<WorkerMind>(e)) continue;
    release_reservation(registry_.get<Forager>(e));
    corpses_.push_back({registry_.get<Identity>(e).id, registry_.get<Position>(e).cell(), 0, false});
    dead.push_back(e);
    ++removed;
    ++stats_.deaths;
  }
  for (const entt::entity e : dead) registry_.destroy(e);
  ordered_entities_.erase(std::remove_if(ordered_entities_.begin(), ordered_entities_.end(),
      [&dead](const entt::entity e) { return std::find(dead.begin(), dead.end(), e) != dead.end(); }),
      ordered_entities_.end());
}
void World::debug_kill_queen() { if (queen_alive_) { queen_alive_ = false; decline_ = true; const entt::entity queen = ordered_entities_.front(); corpses_.push_back({registry_.get<Identity>(queen).id, home_, 0, false}); registry_.destroy(queen); ordered_entities_.erase(ordered_entities_.begin()); ++stats_.deaths; } }

} // namespace ant::sim
