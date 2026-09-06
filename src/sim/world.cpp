#include "sim/world.hpp"

#include "sim/terrain_generation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <stdexcept>

namespace ant::sim {
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

World::World(const std::uint64_t seed)
    : seed_(seed), grid_(Material::Soil),
      behavior_rng_(mix_seed(seed ^ 0xB3A4107ULL), mix_seed(seed ^ 0x515EEDULL)),
      lifecycle_rng_(mix_seed(seed ^ 0x11FEC1EULL), mix_seed(seed ^ 0xA63ULL)),
      dig_work_(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight)) {
  GeneratedTerrain generated = generate_terrain(seed);
  grid_ = std::move(generated.grid); home_ = generated.home;
  sources_[0] = {next_id_++, generated.source_positions[0], Nutrient::Carbohydrate, 100'000, 100'000, 0, 800, 20, 20};
  sources_[1] = {next_id_++, generated.source_positions[1], Nutrient::Protein, 100'000, 100'000, 0, 400, 20, 20};
  refresh_home_field(); spawn_queen(); spawn_workers(); recompute_needs_and_frontiers();
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

void World::spawn_workers() {
  constexpr std::array<GridPos, 6> offsets{{{-3, 0}, {-2, -2}, {0, -3}, {2, -2}, {3, 0}, {0, 3}}};
  for (std::size_t i = 0; i < offsets.size(); ++i) { spawn_worker({home_.x + offsets[i].x, home_.y + offsets[i].y}); registry_.get<Life>(ordered_entities_.back()).age = static_cast<Tick>(i) * 24U * kTicksPerSecond; }
}

void World::refill_sources() {
  for (FoodSource& source : sources_) while (tick_ >= source.next_refill) {
    const std::int64_t before = source.amount; source.amount = std::min(source.capacity, source.amount + source.refill_amount);
    stats_.external_refill += source.amount - before; source.next_refill += source.refill_interval;
  }
}
void World::refresh_home_field() { if (home_field_.revision() != grid_.navigation_revision()) home_field_.rebuild(grid_, home_); }

void World::step() {
  refill_sources(); refresh_home_field();
  for (const entt::entity e : ordered_entities_) { Position& p = registry_.get<Position>(e); p.previous_x_subcells = p.x_subcells; p.previous_y_subcells = p.y_subcells; }
  if (tick_ % kTicksPerSecond == 0) recompute_needs_and_frontiers();
  task_diagnostics_.workers_by_task.fill(0);
  int path_budget = 16; for (const entt::entity e : ordered_entities_) if (registry_.all_of<WorkerMind>(e)) process_worker(e, path_budget);
  if ((tick_ + 1) % kTicksPerSecond == 0) update_biology();
  if ((tick_ + 1) % 4 == 0) update_trails();
  ++tick_;
}
void World::run_ticks(const Tick count) { for (Tick i = 0; i < count; ++i) step(); }

void World::recompute_needs_and_frontiers() {
  if (frontier_revision_ != grid_.navigation_revision()) {
    frontier_revision_ = grid_.navigation_revision();
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(Grid::kWidth * Grid::kHeight)); std::queue<GridPos> queue;
    queue.push(home_); visited[grid_index(home_)] = 1; connected_nest_air_ = 0; frontiers_.clear();
    while (!queue.empty()) {
      const GridPos p = queue.front(); queue.pop(); if (grid_.at(p) != Material::Air) continue; ++connected_nest_air_;
      for (const GridPos d : kNeighbors) { const GridPos n{p.x + d.x, p.y + d.y}; if (!grid_.in_bounds(n)) continue;
        if (grid_.at(n) == Material::Air && visited[grid_index(n)] == 0) { visited[grid_index(n)] = 1; queue.push(n); }
        else if (is_diggable(grid_.at(n)) && n.y > 32 && std::abs(n.x - home_.x) + std::abs(n.y - home_.y) < 64) frontiers_.push_back(n);
      }
    }
    std::sort(frontiers_.begin(), frontiers_.end(), [this](const GridPos a, const GridPos b) {
      const auto score = [this](const GridPos p) { return (std::abs(p.y - home_.y) <= 9 ? 0 : 20) + std::abs(p.x - home_.x) + 2 * std::abs(p.y - home_.y) + static_cast<int>(mix_seed(seed_ ^ grid_index(p)) % 7ULL); };
      return score(a) != score(b) ? score(a) < score(b) : a < b;
    });
    frontiers_.erase(std::unique(frontiers_.begin(), frontiers_.end()), frontiers_.end()); if (frontiers_.size() > 8) frontiers_.resize(8);
    nursery_capacity_ = std::max(12, connected_nest_air_ / 4);
  }
  const auto shortage = [](const std::int64_t amount, const std::int64_t target) { return amount >= target ? std::uint16_t{0} : static_cast<std::uint16_t>(((target - amount) * 1'000) / target); };
  task_diagnostics_.stimuli[0] = std::max(shortage(stores_.carbohydrate, stores_.carbohydrate_capacity / 2), shortage(stores_.protein, stores_.protein_capacity / 2));
  task_diagnostics_.stimuli[1] = frontiers_.empty() ? 0 : static_cast<std::uint16_t>(brood_.size() * 4 >= static_cast<std::size_t>(nursery_capacity_ * 3) ? 850 : 380);
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
      {Task::Excavate, frontiers_.empty() ? 0 : response_weight(task_diagnostics_.stimuli[1], mind.thresholds[1])},
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
  if (((tick_ + registry_.get<Identity>(e).id) % 10 == 0 && tick_ >= mind.committed_until) || (mind.task == Task::Excavate && frontiers_.empty()) || (mind.task == Task::Nurse && brood_.empty())) choose_task(e);
  ++task_diagnostics_.workers_by_task[static_cast<std::size_t>(mind.task)];
  if (cargo.kind == CargoKind::Spoil || cargo.kind == CargoKind::Corpse) { deliver_non_food(e); return; }
  if (mind.task != Task::Forage) { Forager& f = registry_.get<Forager>(e); if (f.reserved_amount > 0) release_reservation(f); f.state = ForageState::AtHome; }
  switch (mind.task) { case Task::Forage: process_forager(e, path_budget); break; case Task::Excavate: process_excavator(e, path_budget); break; case Task::Nurse: process_nurse(e, path_budget); break; case Task::Clean: process_cleaner(e, path_budget); break; case Task::Idle: break; }
}

void World::process_forager(const entt::entity e, int& path_budget) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); Cargo& cargo = registry_.get<Cargo>(e);
  if (f.state == ForageState::ToSource && f.reserved_amount > 0 && tick_ >= f.reservation_expiry) { release_reservation(f); m.path.clear(); f.state = ForageState::AtHome; f.retry_after = tick_ + 200; }
  if (f.state == ForageState::WaitingForStorage) {
    const std::int64_t delivered = std::min(capacity_for(cargo.nutrient) - store_for(cargo.nutrient), cargo.amount); store_for(cargo.nutrient) += delivered; cargo.amount -= delivered; stats_.delivered += delivered;
    if (cargo.amount == 0) { cargo.kind = CargoKind::None; f.state = ForageState::AtHome; ++stats_.completed_round_trips; } return;
  }
  if (f.state == ForageState::AtHome) {
    if (cargo.amount > 0) { f.state = ForageState::Returning; m.path = home_field_.path_home(grid_, registry_.get<Position>(e).cell()); m.next_cell = 0; m.path_revision = grid_.navigation_revision(); }
    else if (tick_ >= f.retry_after) choose_source(e, path_budget);
  }
  if ((f.state == ForageState::ToSource || f.state == ForageState::Returning) && m.path_revision != grid_.navigation_revision()) { ++stats_.navigation_replans; refresh_path(e, path_budget); }
  if ((f.state == ForageState::ToSource || f.state == ForageState::Returning) && move_one_tick(e)) arrive(e, path_budget);
}

void World::choose_source(const entt::entity e, int& path_budget) {
  if (path_budget <= 0) return; const EntityId id = registry_.get<Identity>(e).id; Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); const GridPos start = registry_.get<Position>(e).cell();
  const std::uint16_t first_trail = trails_.at(sources_[0].position);
  const std::uint16_t second_trail = trails_.at(sources_[1].position);
  const int preferred = first_trail == second_trail ? static_cast<int>(id % 2ULL)
                                                    : (first_trail > second_trail ? 0 : 1);
  for (int attempt = 0; attempt < 2 && path_budget > 0; ++attempt) {
    const int source_index = (preferred + attempt) % 2; FoodSource& source = sources_[static_cast<std::size_t>(source_index)];
    const std::int64_t reservation = std::min<std::int64_t>({2'000, source.amount - source.reserved, capacity_for(source.nutrient) - store_for(source.nutrient)}); if (reservation <= 0) continue;
    source.reserved += reservation; f.source_index = source_index; f.reserved_amount = reservation; f.reservation_expiry = tick_ + 1'200; ++stats_.path_requests; --path_budget;
    const PathResult path = find_path(grid_, start, source.position); stats_.path_expansions += path.expanded;
    if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); f.state = ForageState::ToSource; return; }
    release_reservation(f); f.retry_after = tick_ + 200; if (path.status == PathStatus::BudgetExhausted) return;
  }
}

void World::refresh_path(const entt::entity e, int& path_budget) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); const GridPos start = registry_.get<Position>(e).cell(); m.path.clear(); m.next_cell = 0;
  if (f.state == ForageState::Returning) { m.path = home_field_.path_home(grid_, start); m.path_revision = grid_.navigation_revision(); return; }
  if (f.state != ForageState::ToSource || f.source_index < 0 || path_budget <= 0) return; FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; ++stats_.path_requests; --path_budget;
  const PathResult path = find_path(grid_, start, source.position); stats_.path_expansions += path.expanded;
  if (path.status == PathStatus::Complete) { m.path = path.cells; m.path_revision = grid_.navigation_revision(); }
  else { release_reservation(f); f.state = ForageState::AtHome; f.retry_after = tick_ + 200; }
}

bool World::move_one_tick(const entt::entity e) {
  Movement& m = registry_.get<Movement>(e); Position& p = registry_.get<Position>(e); if (m.next_cell >= m.path.size()) return true; const GridPos next = m.path[m.next_cell]; if (!grid_.passable(next)) return false;
  m.speed_residual += 6 * kSubcellsPerCell; const int travel = m.speed_residual / kTicksPerSecond; m.speed_residual %= kTicksPerSecond;
  const std::int32_t tx = cell_center(next.x), ty = cell_center(next.y), dx = tx - p.x_subcells, dy = ty - p.y_subcells; const int distance = std::abs(dx) + std::abs(dy);
  if (distance <= travel) { p.x_subcells = tx; p.y_subcells = ty; ++m.next_cell; } else if (dx != 0) p.x_subcells += dx > 0 ? travel : -travel; else p.y_subcells += dy > 0 ? travel : -travel;
  return m.next_cell >= m.path.size();
}

void World::arrive(const entt::entity e, int&) {
  Forager& f = registry_.get<Forager>(e); Movement& m = registry_.get<Movement>(e); Cargo& cargo = registry_.get<Cargo>(e);
  if (f.state == ForageState::ToSource) {
    FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; const std::int64_t picked = std::min(source.amount, f.reserved_amount); source.amount -= picked; source.reserved -= f.reserved_amount; f.reserved_amount = 0;
    if (picked <= 0) { f.source_index = -1; f.state = ForageState::AtHome; f.retry_after = tick_ + 100; m.path.clear(); return; }
    cargo.kind = CargoKind::Food; cargo.nutrient = source.nutrient; cargo.amount = picked; stats_.picked_up += picked; f.state = ForageState::Returning;
    m.path = home_field_.path_home(grid_, registry_.get<Position>(e).cell()); m.next_cell = 0; m.path_revision = grid_.navigation_revision(); return;
  }
  if (f.state == ForageState::Returning) {
    const std::int64_t delivered = std::min(capacity_for(cargo.nutrient) - store_for(cargo.nutrient), cargo.amount); store_for(cargo.nutrient) += delivered; cargo.amount -= delivered; stats_.delivered += delivered; m.path.clear(); m.next_cell = 0;
    if (cargo.amount > 0) f.state = ForageState::WaitingForStorage; else { cargo.kind = CargoKind::None; f.state = ForageState::AtHome; f.source_index = -1; ++stats_.completed_round_trips; }
  }
}

void World::release_reservation(Forager& f) { if (f.source_index >= 0 && f.reserved_amount > 0) { FoodSource& source = sources_[static_cast<std::size_t>(f.source_index)]; source.reserved = std::max<std::int64_t>(0, source.reserved - f.reserved_amount); } f.source_index = -1; f.reserved_amount = 0; }

void World::route_to(const entt::entity e, const GridPos target, int& path_budget) {
  Movement& m = registry_.get<Movement>(e); if (m.next_cell < m.path.size() && m.path_revision == grid_.navigation_revision()) { static_cast<void>(move_one_tick(e)); return; }
  if (registry_.get<Position>(e).cell() == target || path_budget <= 0) return; ++stats_.path_requests; --path_budget; const PathResult path = find_path(grid_, registry_.get<Position>(e).cell(), target); stats_.path_expansions += path.expanded;
  if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); static_cast<void>(move_one_tick(e)); }
}

void World::process_excavator(const entt::entity e, int& path_budget) {
  WorkerMind& mind = registry_.get<WorkerMind>(e);
  if (!mind.has_target || !grid_.in_bounds(mind.target) || !is_diggable(grid_.at(mind.target))) { mind.has_target = false; for (const GridPos frontier : frontiers_) { int claims = 0; for (const entt::entity other : ordered_entities_) if (registry_.all_of<WorkerMind>(other)) { const WorkerMind& om = registry_.get<WorkerMind>(other); claims += om.task == Task::Excavate && om.has_target && om.target == frontier ? 1 : 0; } if (claims < 2) { mind.target = frontier; mind.has_target = true; break; } } }
  if (!mind.has_target) return; const GridPos current = registry_.get<Position>(e).cell(); GridPos work = current; bool adjacent = false;
  for (const GridPos d : kNeighbors) { const GridPos candidate{mind.target.x + d.x, mind.target.y + d.y}; if (grid_.passable(candidate)) { work = candidate; adjacent = candidate == current; if (adjacent) break; } }
  if (!adjacent) { route_to(e, work, path_budget); return; } if (++mind.action_ticks < 10) return; mind.action_ticks = 0; std::uint16_t& remaining = dig_work_[grid_index(mind.target)]; if (remaining == 0) remaining = grid_.at(mind.target) == Material::Clay ? 25 : 10;
  if (--remaining == 0) { grid_.set(mind.target, Material::Air); Cargo& cargo = registry_.get<Cargo>(e); cargo.kind = CargoKind::Spoil; cargo.amount = 1'000; ++stats_.cells_excavated; mind.has_target = false; }
}

void World::process_nurse(const entt::entity e, int& path_budget) {
  if (registry_.get<Position>(e).cell() != home_) { route_to(e, home_, path_budget); return; } WorkerMind& mind = registry_.get<WorkerMind>(e); if (++mind.action_ticks < kTicksPerSecond) return; mind.action_ticks = 0; if (brood_.empty()) return;
  auto item = std::min_element(brood_.begin(), brood_.end(), [](const BroodSnapshot& a, const BroodSnapshot& b) { return a.care_remaining != b.care_remaining ? a.care_remaining < b.care_remaining : a.id < b.id; }); item->care_remaining = 5 * kTicksPerSecond;
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
  if (registry_.get<Position>(e).cell() != outlet) { if (m.next_cell >= m.path.size() || m.path_revision != grid_.navigation_revision()) { const PathResult path = find_path(grid_, registry_.get<Position>(e).cell(), outlet); if (path.status == PathStatus::Complete) { m.path = path.cells; m.next_cell = 0; m.path_revision = grid_.navigation_revision(); } } static_cast<void>(move_one_tick(e)); return; }
  if (cargo.kind == CargoKind::Spoil) { ++spoil_mound_; ++stats_.spoil_delivered; } else if (cargo.kind == CargoKind::Corpse) ++stats_.corpses_cleaned; cargo = Cargo{}; m.path.clear();
}

bool World::consume(const Nutrient nutrient, const std::int64_t amount) { std::int64_t& store = store_for(nutrient); if (store < amount) return false; store -= amount; if (nutrient == Nutrient::Carbohydrate) stats_.consumed_carbohydrate += amount; else stats_.consumed_protein += amount; return true; }
Tick World::brood_target(const BroodStage stage) const { if (stage == BroodStage::Egg) return 30 * kTicksPerSecond; if (stage == BroodStage::Larva) return 60 * kTicksPerSecond; return 45 * kTicksPerSecond; }

void World::update_biology() {
  bool queen_fed = false;
  if (queen_alive_) { queen_fed = stores_.carbohydrate >= 20 && stores_.protein >= 10; if (queen_fed) { static_cast<void>(consume(Nutrient::Carbohydrate, 20)); static_cast<void>(consume(Nutrient::Protein, 10)); queen_starvation_ = 0; } else queen_starvation_ += kTicksPerSecond; if (queen_starvation_ >= 300 * kTicksPerSecond) { queen_alive_ = false; decline_ = true; const entt::entity queen = ordered_entities_.front(); corpses_.push_back({registry_.get<Identity>(queen).id, home_, 0, false}); registry_.destroy(queen); ordered_entities_.erase(ordered_entities_.begin()); ++stats_.deaths; } }
  for (const entt::entity e : ordered_entities_) if (registry_.all_of<Life>(e)) { Life& life = registry_.get<Life>(e); life.age += kTicksPerSecond; if (consume(Nutrient::Carbohydrate, 3)) life.starvation = 0; else life.starvation += kTicksPerSecond; }
  std::vector<EntityId> matured;
  for (BroodSnapshot& item : brood_) { item.target = brood_target(item.stage); bool fed = true; if (item.stage == BroodStage::Larva) { fed = stores_.carbohydrate >= 8 && stores_.protein >= 15; if (fed) { static_cast<void>(consume(Nutrient::Carbohydrate, 8)); static_cast<void>(consume(Nutrient::Protein, 15)); item.starvation = 0; } else item.starvation += kTicksPerSecond; }
    const bool cared = item.care_remaining > 0;
    if (item.care_remaining > 0) item.care_remaining -= std::min<Tick>(item.care_remaining, kTicksPerSecond);
    if (fed) item.progress += cared ? kTicksPerSecond : kTicksPerSecond / 2;
    if (item.stage != BroodStage::Larva || item.starvation < 180 * kTicksPerSecond) if (item.progress >= item.target) { item.progress = 0; if (item.stage == BroodStage::Egg) item.stage = BroodStage::Larva; else if (item.stage == BroodStage::Larva) item.stage = BroodStage::Pupa; else matured.push_back(item.id); }
  }
  brood_.erase(std::remove_if(brood_.begin(), brood_.end(), [this, &matured](const BroodSnapshot& item) { const bool starved = item.stage == BroodStage::Larva && item.starvation >= 180 * kTicksPerSecond; if (starved) ++stats_.deaths; return starved || std::find(matured.begin(), matured.end(), item.id) != matured.end(); }), brood_.end());
  for (const EntityId id : matured) { static_cast<void>(id); spawn_worker(home_); ++stats_.workers_born; }
  if (queen_alive_ && queen_fed && tick_ + 1 >= next_laying_) { next_laying_ += 12 * kTicksPerSecond; if (brood_.size() < static_cast<std::size_t>(nursery_capacity_)) { brood_.push_back({next_id_++, BroodStage::Egg, {home_.x + static_cast<int>(stats_.eggs_laid % 7) - 3, home_.y + static_cast<int>((stats_.eggs_laid / 7) % 3) - 1}, 0, brood_target(BroodStage::Egg), 0, 0}); ++stats_.eggs_laid; } }
  remove_dead_workers(); for (CorpseSnapshot& corpse : corpses_) { corpse.age += kTicksPerSecond; corpse.cleanable = corpse.age >= 30 * kTicksPerSecond; }
  corpses_.erase(std::remove_if(corpses_.begin(), corpses_.end(), [](const CorpseSnapshot& c) { return c.age >= 300 * kTicksPerSecond; }), corpses_.end());
  for (DroppedCargoSnapshot& dropped : dropped_food_) dropped.age += kTicksPerSecond;
  dropped_food_.erase(std::remove_if(dropped_food_.begin(), dropped_food_.end(),
      [this](const DroppedCargoSnapshot& dropped) {
        if (dropped.age < 120 * kTicksPerSecond) return false;
        stats_.decayed_food += dropped.amount;
        return true;
      }), dropped_food_.end());
  const bool has_workers = std::any_of(ordered_entities_.begin(), ordered_entities_.end(), [this](const entt::entity e) { return registry_.all_of<Life>(e); });
  extinct_ = !queen_alive_ && !has_workers && brood_.empty();
}

void World::remove_dead_workers() {
  std::vector<entt::entity> dead;
  for (const entt::entity e : ordered_entities_) if (registry_.all_of<Life>(e)) { const Life& life = registry_.get<Life>(e); if (life.age < life.lifespan && life.starvation < 120 * kTicksPerSecond) continue; const Cargo cargo = registry_.get<Cargo>(e); const GridPos p = registry_.get<Position>(e).cell(); if (cargo.kind == CargoKind::Corpse) corpses_.push_back({cargo.entity_id, p, 30 * kTicksPerSecond, true}); else if (cargo.kind == CargoKind::Food && cargo.amount > 0) dropped_food_.push_back({next_id_++, p, cargo.nutrient, cargo.amount, 0}); corpses_.push_back({registry_.get<Identity>(e).id, p, 0, false}); release_reservation(registry_.get<Forager>(e)); dead.push_back(e); ++stats_.deaths; }
  for (const entt::entity e : dead) registry_.destroy(e); ordered_entities_.erase(std::remove_if(ordered_entities_.begin(), ordered_entities_.end(), [&dead](const entt::entity e) { return std::find(dead.begin(), dead.end(), e) != dead.end(); }), ordered_entities_.end());
}

void World::update_trails() { for (const entt::entity e : ordered_entities_) if (registry_.all_of<Forager>(e) && registry_.get<Forager>(e).state == ForageState::Returning) trails_.deposit(registry_.get<Position>(e).cell(), 900); trails_.update(grid_); }

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

std::uint64_t World::canonical_hash() const {
  std::uint64_t hash = 1469598103934665603ULL; hash_value(hash, seed_); hash_value(hash, tick_); hash_value(hash, grid_.material_hash()); hash_value(hash, behavior_rng_.state()); hash_value(hash, lifecycle_rng_.state()); hash_signed(hash, stores_.carbohydrate); hash_signed(hash, stores_.protein); hash_value(hash, trails_.mass()); hash_value(hash, queen_alive_); hash_value(hash, queen_starvation_); hash_value(hash, static_cast<std::uint64_t>(focus_));
  for (const FoodSource& source : sources_) { hash_signed(hash, source.amount); hash_signed(hash, source.reserved); hash_value(hash, source.next_refill); }
  for (const entt::entity e : ordered_entities_) { const Identity& id = registry_.get<Identity>(e); const Position& p = registry_.get<Position>(e); const Cargo& cargo = registry_.get<Cargo>(e); hash_value(hash, id.id); hash_value(hash, static_cast<std::uint64_t>(p.x_subcells)); hash_value(hash, static_cast<std::uint64_t>(p.y_subcells)); hash_signed(hash, cargo.amount); if (registry_.all_of<WorkerMind>(e)) { const WorkerMind& mind = registry_.get<WorkerMind>(e); hash_value(hash, static_cast<std::uint64_t>(mind.task)); hash_value(hash, mind.committed_until); } if (registry_.all_of<Life>(e)) { const Life& life = registry_.get<Life>(e); hash_value(hash, life.age); hash_value(hash, life.lifespan); hash_value(hash, life.starvation); } }
  for (const BroodSnapshot& b : brood_) { hash_value(hash, b.id); hash_value(hash, static_cast<std::uint64_t>(b.stage)); hash_value(hash, b.progress); hash_value(hash, b.starvation); } for (const CorpseSnapshot& c : corpses_) { hash_value(hash, c.id); hash_value(hash, c.age); } for (const DroppedCargoSnapshot& dropped : dropped_food_) { hash_value(hash, dropped.id); hash_signed(hash, dropped.amount); hash_value(hash, dropped.age); }
  hash_signed(hash, stats_.delivered); hash_value(hash, stats_.cells_excavated); hash_value(hash, stats_.workers_born); hash_value(hash, stats_.deaths); return hash;
}

bool World::invariant_holds() const {
  if (stores_.carbohydrate < 0 || stores_.protein < 0 || stores_.carbohydrate > stores_.carbohydrate_capacity || stores_.protein > stores_.protein_capacity) return false; std::array<std::int64_t, 2> reserved{}; EntityId previous = 0;
  for (const entt::entity e : ordered_entities_) { const Identity& id = registry_.get<Identity>(e); const Position& p = registry_.get<Position>(e); const Cargo& cargo = registry_.get<Cargo>(e); if (id.id <= previous || !grid_.passable(p.cell()) || cargo.amount < 0) return false; previous = id.id; if (registry_.all_of<Forager>(e)) { const Forager& f = registry_.get<Forager>(e); if (f.reserved_amount < 0) return false; if (f.source_index >= 0) reserved[static_cast<std::size_t>(f.source_index)] += f.reserved_amount; } }
  for (std::size_t i = 0; i < sources_.size(); ++i) if (sources_[i].amount < 0 || sources_[i].amount > sources_[i].capacity || sources_[i].reserved != reserved[i] || sources_[i].reserved > sources_[i].amount) return false;
  return brood_.size() <= 5'000 &&
         ordered_entities_.size() + brood_.size() + corpses_.size() + dropped_food_.size() <= 20'000;
}

void World::debug_set_source_amount(const std::size_t i, const std::int64_t amount) { FoodSource& source = sources_.at(i); if (amount < 0 || amount > source.capacity) throw std::out_of_range("source amount outside capacity"); source.amount = amount; }
void World::debug_set_source_refill(const std::size_t i, const std::int64_t amount) { if (amount < 0) throw std::out_of_range("source refill cannot be negative"); sources_.at(i).refill_amount = amount; }
void World::debug_set_store(const Nutrient n, const std::int64_t amount) { if (amount < 0 || amount > capacity_for(n)) throw std::out_of_range("store amount outside capacity"); store_for(n) = amount; }
void World::debug_set_worker_lifespan(const EntityId id, const Tick lifespan) { for (const entt::entity e : ordered_entities_) if (registry_.get<Identity>(e).id == id && registry_.all_of<Life>(e)) registry_.get<Life>(e).lifespan = lifespan; }
void World::debug_spawn_brood(const BroodStage stage, const Tick progress, const Tick starvation) { brood_.push_back({next_id_++, stage, home_, progress, brood_target(stage), 0, starvation}); }
void World::debug_kill_queen() { if (queen_alive_) { queen_alive_ = false; decline_ = true; const entt::entity queen = ordered_entities_.front(); corpses_.push_back({registry_.get<Identity>(queen).id, home_, 0, false}); registry_.destroy(queen); ordered_entities_.erase(ordered_entities_.begin()); ++stats_.deaths; } }

} // namespace ant::sim
