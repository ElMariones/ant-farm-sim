#include "sim/world.hpp"

#include "sim/terrain_generation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ant::sim {
namespace {

std::int32_t cell_center(const int coordinate) {
  return static_cast<std::int32_t>(coordinate * kSubcellsPerCell + kSubcellsPerCell / 2);
}

void hash_value(std::uint64_t& hash, const std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> static_cast<unsigned int>(byte * 8)) & 0xffULL;
    hash *= 1099511628211ULL;
  }
}

} // namespace

World::World(const std::uint64_t seed)
    : seed_(seed), grid_(Material::Soil),
      behavior_rng_(mix_seed(seed ^ 0xB3A4107ULL), mix_seed(seed ^ 0x515EEDULL)) {
  GeneratedTerrain generated = generate_terrain(seed);
  grid_ = std::move(generated.grid);
  home_ = generated.home;
  sources_[0] = {next_id_++,
                 generated.source_positions[0],
                 Nutrient::Carbohydrate,
                 12'000,
                 25'000,
                 0,
                 1'000,
                 600,
                 600};
  sources_[1] = {
      next_id_++, generated.source_positions[1], Nutrient::Protein, 12'000, 25'000, 0, 1'000, 600,
      600};
  refresh_home_field();
  spawn_queen();
  spawn_workers();
}

void World::spawn_queen() {
  const entt::entity entity = registry_.create();
  const EntityId id = next_id_++;
  registry_.emplace<Identity>(entity, id);
  registry_.emplace<Ant>(entity, AntKind::Queen);
  registry_.emplace<Position>(entity, cell_center(home_.x), cell_center(home_.y),
                              cell_center(home_.x), cell_center(home_.y));
  registry_.emplace<Cargo>(entity);
  ordered_entities_.push_back(entity);
}

void World::spawn_workers() {
  constexpr std::array<GridPos, 6> kOffsets{{{-3, 0}, {-2, -2}, {0, -3}, {2, -2}, {3, 0}, {0, 3}}};
  for (const GridPos offset : kOffsets) {
    const GridPos position{home_.x + offset.x, home_.y + offset.y};
    const entt::entity entity = registry_.create();
    const EntityId id = next_id_++;
    registry_.emplace<Identity>(entity, id);
    registry_.emplace<Ant>(entity, AntKind::Worker);
    registry_.emplace<Position>(entity, cell_center(position.x), cell_center(position.y),
                                cell_center(position.x), cell_center(position.y));
    registry_.emplace<Cargo>(entity);
    registry_.emplace<Movement>(entity);
    registry_.emplace<Forager>(entity);
    ordered_entities_.push_back(entity);
  }
}

void World::refill_sources() {
  for (FoodSource& source : sources_) {
    while (tick_ >= source.next_refill) {
      const std::int64_t before = source.amount;
      source.amount = std::min(source.capacity, source.amount + source.refill_amount);
      stats_.external_refill += source.amount - before;
      source.next_refill += source.refill_interval;
    }
  }
}

void World::refresh_home_field() {
  if (home_field_.revision() != grid_.navigation_revision()) {
    home_field_.rebuild(grid_, home_);
  }
}

void World::step() {
  refill_sources();
  refresh_home_field();

  for (const entt::entity entity : ordered_entities_) {
    Position& position = registry_.get<Position>(entity);
    position.previous_x_subcells = position.x_subcells;
    position.previous_y_subcells = position.y_subcells;
  }

  int path_budget = 16;
  for (const entt::entity entity : ordered_entities_) {
    if (registry_.all_of<Forager>(entity)) {
      process_forager(entity, path_budget);
    }
  }
  ++tick_;
}

void World::run_ticks(const Tick count) {
  for (Tick index = 0; index < count; ++index) {
    step();
  }
}

void World::process_forager(const entt::entity entity, int& path_budget) {
  Forager& forager = registry_.get<Forager>(entity);
  Movement& movement = registry_.get<Movement>(entity);
  Cargo& cargo = registry_.get<Cargo>(entity);

  if (forager.state == ForageState::ToSource && forager.reserved_amount > 0 &&
      tick_ >= forager.reservation_expiry) {
    release_reservation(forager);
    movement.path.clear();
    movement.next_cell = 0;
    forager.state = ForageState::AtHome;
    forager.retry_after = tick_ + 200;
  }

  if (forager.state == ForageState::WaitingForStorage) {
    const std::int64_t free = stores_.capacity_per_nutrient - store_for(cargo.nutrient);
    const std::int64_t delivered = std::min(free, cargo.amount);
    store_for(cargo.nutrient) += delivered;
    cargo.amount -= delivered;
    stats_.delivered += delivered;
    if (cargo.amount == 0) {
      forager.state = ForageState::AtHome;
      ++stats_.completed_round_trips;
    }
    return;
  }

  if (forager.state == ForageState::AtHome) {
    if (cargo.amount > 0) {
      forager.state = ForageState::Returning;
      movement.path = home_field_.path_home(grid_, registry_.get<Position>(entity).cell());
      movement.next_cell = 0;
      movement.path_revision = grid_.navigation_revision();
    } else if (tick_ >= forager.retry_after) {
      choose_source(entity, path_budget);
    }
  }

  if ((forager.state == ForageState::ToSource || forager.state == ForageState::Returning) &&
      movement.path_revision != grid_.navigation_revision()) {
    ++stats_.navigation_replans;
    refresh_path(entity, path_budget);
  }

  if (forager.state == ForageState::ToSource || forager.state == ForageState::Returning) {
    if (move_one_tick(entity)) {
      arrive(entity, path_budget);
    }
  }
}

void World::choose_source(const entt::entity entity, int& path_budget) {
  if (path_budget <= 0) {
    return;
  }
  const EntityId id = registry_.get<Identity>(entity).id;
  Forager& forager = registry_.get<Forager>(entity);
  Movement& movement = registry_.get<Movement>(entity);
  const GridPos start = registry_.get<Position>(entity).cell();
  const int preferred = static_cast<int>(id % 2ULL);

  for (int attempt = 0; attempt < 2 && path_budget > 0; ++attempt) {
    const int source_index = (preferred + attempt) % 2;
    FoodSource& source = sources_[static_cast<std::size_t>(source_index)];
    const std::int64_t available = source.amount - source.reserved;
    const std::int64_t free_storage = stores_.capacity_per_nutrient - store_for(source.nutrient);
    const std::int64_t reservation = std::min<std::int64_t>({1'000, available, free_storage});
    if (reservation <= 0) {
      continue;
    }

    source.reserved += reservation;
    forager.source_index = source_index;
    forager.reserved_amount = reservation;
    forager.reservation_expiry = tick_ + 1'200;
    ++stats_.path_requests;
    --path_budget;
    const PathResult path = find_path(grid_, start, source.position);
    stats_.path_expansions += path.expanded;
    if (path.status == PathStatus::Complete) {
      movement.path = path.cells;
      movement.next_cell = 0;
      movement.path_revision = grid_.navigation_revision();
      forager.state = ForageState::ToSource;
      return;
    }

    release_reservation(forager);
    forager.retry_after = tick_ + 200;
    if (path.status == PathStatus::BudgetExhausted) {
      return;
    }
  }
}

void World::refresh_path(const entt::entity entity, int& path_budget) {
  Forager& forager = registry_.get<Forager>(entity);
  Movement& movement = registry_.get<Movement>(entity);
  const GridPos start = registry_.get<Position>(entity).cell();
  movement.path.clear();
  movement.next_cell = 0;

  if (forager.state == ForageState::Returning) {
    refresh_home_field();
    movement.path = home_field_.path_home(grid_, start);
    movement.path_revision = grid_.navigation_revision();
    if (movement.path.empty() && start != home_) {
      forager.state = ForageState::AtHome;
      forager.retry_after = tick_ + 200;
    }
    return;
  }

  if (forager.state != ForageState::ToSource || forager.source_index < 0 || path_budget <= 0) {
    return;
  }
  FoodSource& source = sources_[static_cast<std::size_t>(forager.source_index)];
  ++stats_.path_requests;
  --path_budget;
  const PathResult path = find_path(grid_, start, source.position);
  stats_.path_expansions += path.expanded;
  if (path.status == PathStatus::Complete) {
    movement.path = path.cells;
    movement.path_revision = grid_.navigation_revision();
    return;
  }
  release_reservation(forager);
  forager.state = ForageState::AtHome;
  forager.retry_after = tick_ + 200;
}

bool World::move_one_tick(const entt::entity entity) {
  Movement& movement = registry_.get<Movement>(entity);
  Position& position = registry_.get<Position>(entity);
  if (movement.next_cell >= movement.path.size()) {
    return true;
  }
  const GridPos next_cell = movement.path[movement.next_cell];
  if (!grid_.passable(next_cell)) {
    return false;
  }

  movement.speed_residual += 6 * kSubcellsPerCell;
  int travel = movement.speed_residual / kTicksPerSecond;
  movement.speed_residual %= kTicksPerSecond;
  const std::int32_t target_x = cell_center(next_cell.x);
  const std::int32_t target_y = cell_center(next_cell.y);
  const std::int32_t delta_x = target_x - position.x_subcells;
  const std::int32_t delta_y = target_y - position.y_subcells;
  const int distance = std::abs(delta_x) + std::abs(delta_y);
  if (distance <= travel) {
    position.x_subcells = target_x;
    position.y_subcells = target_y;
    ++movement.next_cell;
    travel -= distance;
    static_cast<void>(travel);
  } else if (delta_x != 0) {
    position.x_subcells += delta_x > 0 ? travel : -travel;
  } else if (delta_y != 0) {
    position.y_subcells += delta_y > 0 ? travel : -travel;
  }
  return movement.next_cell >= movement.path.size();
}

void World::arrive(const entt::entity entity, int& path_budget) {
  static_cast<void>(path_budget);
  Forager& forager = registry_.get<Forager>(entity);
  Movement& movement = registry_.get<Movement>(entity);
  Cargo& cargo = registry_.get<Cargo>(entity);

  if (forager.state == ForageState::ToSource) {
    FoodSource& source = sources_[static_cast<std::size_t>(forager.source_index)];
    const std::int64_t picked = std::min(source.amount, forager.reserved_amount);
    source.amount -= picked;
    source.reserved -= forager.reserved_amount;
    forager.reserved_amount = 0;
    if (picked <= 0) {
      forager.source_index = -1;
      forager.state = ForageState::AtHome;
      forager.retry_after = tick_ + 100;
      movement.path.clear();
      movement.next_cell = 0;
      return;
    }
    cargo.nutrient = source.nutrient;
    cargo.amount = picked;
    stats_.picked_up += picked;
    forager.state = ForageState::Returning;
    movement.path = home_field_.path_home(grid_, registry_.get<Position>(entity).cell());
    movement.next_cell = 0;
    movement.path_revision = grid_.navigation_revision();
    return;
  }

  if (forager.state == ForageState::Returning) {
    const std::int64_t free = stores_.capacity_per_nutrient - store_for(cargo.nutrient);
    const std::int64_t delivered = std::min(free, cargo.amount);
    store_for(cargo.nutrient) += delivered;
    cargo.amount -= delivered;
    stats_.delivered += delivered;
    movement.path.clear();
    movement.next_cell = 0;
    if (cargo.amount > 0) {
      forager.state = ForageState::WaitingForStorage;
    } else {
      forager.state = ForageState::AtHome;
      forager.source_index = -1;
      ++stats_.completed_round_trips;
    }
  }
}

void World::release_reservation(Forager& forager) {
  if (forager.source_index >= 0 && forager.reserved_amount > 0) {
    FoodSource& source = sources_[static_cast<std::size_t>(forager.source_index)];
    source.reserved = std::max<std::int64_t>(0, source.reserved - forager.reserved_amount);
  }
  forager.source_index = -1;
  forager.reserved_amount = 0;
}

std::int64_t& World::store_for(const Nutrient nutrient) {
  return nutrient == Nutrient::Carbohydrate ? stores_.carbohydrate : stores_.protein;
}

std::int64_t World::store_for(const Nutrient nutrient) const {
  return nutrient == Nutrient::Carbohydrate ? stores_.carbohydrate : stores_.protein;
}

std::vector<ActorSnapshot> World::actors() const {
  std::vector<ActorSnapshot> snapshots;
  snapshots.reserve(ordered_entities_.size());
  for (const entt::entity entity : ordered_entities_) {
    const Identity& identity = registry_.get<Identity>(entity);
    const Position& position = registry_.get<Position>(entity);
    const Ant& ant = registry_.get<Ant>(entity);
    const Cargo& cargo = registry_.get<Cargo>(entity);
    const ForageState state = registry_.all_of<Forager>(entity)
                                  ? registry_.get<Forager>(entity).state
                                  : ForageState::AtHome;
    snapshots.push_back({identity.id, ant.kind, state,
                         static_cast<double>(position.x_subcells) / kSubcellsPerCell,
                         static_cast<double>(position.y_subcells) / kSubcellsPerCell,
                         static_cast<double>(position.previous_x_subcells) / kSubcellsPerCell,
                         static_cast<double>(position.previous_y_subcells) / kSubcellsPerCell,
                         cargo.nutrient, cargo.amount});
  }
  return snapshots;
}

std::uint64_t World::canonical_hash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  hash_value(hash, seed_);
  hash_value(hash, tick_);
  hash_value(hash, grid_.material_hash());
  hash_value(hash, grid_.navigation_revision());
  hash_value(hash, behavior_rng_.state());
  hash_value(hash, behavior_rng_.increment());
  hash_value(hash, static_cast<std::uint64_t>(stores_.carbohydrate));
  hash_value(hash, static_cast<std::uint64_t>(stores_.protein));
  for (const FoodSource& source : sources_) {
    hash_value(hash, source.id);
    hash_value(hash, static_cast<std::uint64_t>(source.amount));
    hash_value(hash, static_cast<std::uint64_t>(source.reserved));
    hash_value(hash, source.next_refill);
  }
  for (const entt::entity entity : ordered_entities_) {
    const Identity& identity = registry_.get<Identity>(entity);
    const Position& position = registry_.get<Position>(entity);
    const Cargo& cargo = registry_.get<Cargo>(entity);
    hash_value(hash, identity.id);
    hash_value(hash, static_cast<std::uint64_t>(position.x_subcells));
    hash_value(hash, static_cast<std::uint64_t>(position.y_subcells));
    hash_value(hash, static_cast<std::uint64_t>(cargo.amount));
    hash_value(hash, static_cast<std::uint64_t>(cargo.nutrient));
    if (registry_.all_of<Forager>(entity)) {
      const Forager& forager = registry_.get<Forager>(entity);
      const Movement& movement = registry_.get<Movement>(entity);
      hash_value(hash, static_cast<std::uint64_t>(forager.state));
      hash_value(hash, static_cast<std::uint64_t>(forager.source_index + 1));
      hash_value(hash, static_cast<std::uint64_t>(forager.reserved_amount));
      hash_value(hash, forager.reservation_expiry);
      hash_value(hash, static_cast<std::uint64_t>(movement.next_cell));
      hash_value(hash, movement.path_revision);
      for (const GridPos cell : movement.path) {
        hash_value(hash, static_cast<std::uint64_t>(cell.x));
        hash_value(hash, static_cast<std::uint64_t>(cell.y));
      }
    }
  }
  hash_value(hash, static_cast<std::uint64_t>(stats_.picked_up));
  hash_value(hash, static_cast<std::uint64_t>(stats_.delivered));
  hash_value(hash, static_cast<std::uint64_t>(stats_.external_refill));
  return hash;
}

bool World::invariant_holds() const {
  if (stores_.carbohydrate < 0 || stores_.protein < 0 ||
      stores_.carbohydrate > stores_.capacity_per_nutrient ||
      stores_.protein > stores_.capacity_per_nutrient) {
    return false;
  }
  std::array<std::int64_t, 2> reserved_totals{};
  EntityId previous_id = 0;
  for (const entt::entity entity : ordered_entities_) {
    const Identity& identity = registry_.get<Identity>(entity);
    const Position& position = registry_.get<Position>(entity);
    const Cargo& cargo = registry_.get<Cargo>(entity);
    if (identity.id <= previous_id || !grid_.passable(position.cell()) || cargo.amount < 0) {
      return false;
    }
    previous_id = identity.id;
    if (registry_.all_of<Forager>(entity)) {
      const Forager& forager = registry_.get<Forager>(entity);
      if (forager.reserved_amount < 0) {
        return false;
      }
      if (forager.source_index >= 0) {
        reserved_totals[static_cast<std::size_t>(forager.source_index)] += forager.reserved_amount;
      }
    }
  }
  for (std::size_t index = 0; index < sources_.size(); ++index) {
    const FoodSource& source = sources_[index];
    if (source.amount < 0 || source.amount > source.capacity || source.reserved < 0 ||
        source.reserved > source.amount || source.reserved != reserved_totals[index]) {
      return false;
    }
  }
  return true;
}

void World::debug_set_source_amount(const std::size_t index, const std::int64_t amount) {
  FoodSource& source = sources_.at(index);
  if (amount < 0 || amount > source.capacity) {
    throw std::out_of_range("source amount outside capacity");
  }
  source.amount = amount;
}

void World::debug_set_store(const Nutrient nutrient, const std::int64_t amount) {
  if (amount < 0 || amount > stores_.capacity_per_nutrient) {
    throw std::out_of_range("store amount outside capacity");
  }
  store_for(nutrient) = amount;
}

} // namespace ant::sim
