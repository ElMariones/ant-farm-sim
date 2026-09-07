#include "game/session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

namespace {

std::int64_t cargo_total(const ant::sim::World& world) {
  std::int64_t total = 0;
  for (const auto& actor : world.actors()) {
    if (actor.cargo_kind == ant::sim::CargoKind::Food) {
      total += actor.cargo_amount;
    }
  }
  return total;
}

} // namespace

TEST_CASE("fixed-tick simulation is deterministic regardless of batching", "[world]") {
  ant::game::Session frame_at_one_x(42);
  ant::game::Session frame_at_five_x(42);
  for (int frame = 0; frame < 400; ++frame) {
    frame_at_one_x.step_ticks(5);
  }
  frame_at_five_x.step_ticks(2'000);

  CHECK(frame_at_one_x.world().tick() == frame_at_five_x.world().tick());
  CHECK(frame_at_one_x.world().canonical_hash() == frame_at_five_x.world().canonical_hash());
  CHECK(frame_at_one_x.world().invariant_holds());
}

TEST_CASE("workers remain on passable cells and complete physical food round trips",
          "[world][forage]") {
  ant::sim::World world(7);
  // Two founding sites of 140,000 plus the founding stock the queen brought with her.
  constexpr std::int64_t initial_mass = 370'000;
  world.run_ticks(2'400);

  REQUIRE(world.stats().completed_round_trips > 0);
  CHECK(world.stats().picked_up == world.stats().delivered + cargo_total(world));
  std::int64_t source_mass = 0;
  for (const auto& source : world.sources()) source_mass += source.amount;
  const std::int64_t store_mass = world.stores().carbohydrate + world.stores().protein;
  // Food a site never gave up because nobody found it before it rotted is a recorded sink, like
  // anything the colony ate.
  CHECK(source_mass + store_mass + cargo_total(world) + world.stats().consumed_carbohydrate +
            world.stats().consumed_protein + world.stats().decayed_food ==
        initial_mass + world.stats().external_refill);
  CHECK(world.invariant_holds());
}

TEST_CASE("only one worker can reserve the final source unit", "[world][forage]") {
  ant::sim::World world(42);
  world.debug_set_source_amount(0, 1'000);
  world.debug_set_source_amount(1, 0);
  for (int tick = 0; tick < 200 && world.sources()[0].reserved == 0; ++tick) world.step();

  CHECK(world.sources()[0].reserved == 1'000);
  CHECK(world.sources()[1].reserved == 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("unreachable targets release reservations", "[world][forage][navigation]") {
  ant::sim::World world(9);
  world.debug_set_source_amount(0, 1'000);
  world.debug_set_source_amount(1, 0);
  const ant::sim::GridPos source = world.sources()[0].position;
  world.debug_grid().set({source.x, source.y - 1}, ant::sim::Material::Soil);
  world.debug_grid().set({source.x + 1, source.y}, ant::sim::Material::Soil);
  world.debug_grid().set({source.x, source.y + 1}, ant::sim::Material::Soil);
  world.debug_grid().set({source.x - 1, source.y}, ant::sim::Material::Soil);
  for (int tick = 0; tick < 200; ++tick) world.step();

  CHECK(world.sources()[0].reserved == 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("topology changes invalidate active paths before movement", "[world][navigation]") {
  ant::sim::World world(42);
  // Wait for a forager to actually be walking a planned route, which is what a topology change
  // has to invalidate.
  const auto en_route = [&world] {
    const auto actors = world.actors();
    return std::any_of(actors.begin(), actors.end(), [](const ant::sim::ActorSnapshot& actor) {
      return actor.forage_state == ant::sim::ForageState::ToSource;
    });
  };
  for (int tick = 0; tick < 6'000 && !en_route(); ++tick) world.step();
  REQUIRE(en_route());
  const auto requests_before = world.stats().path_requests;
  world.debug_grid().set({world.home().x, 40}, ant::sim::Material::Soil);
  world.step();

  CHECK(world.stats().navigation_replans > 0);
  CHECK(world.stats().path_requests > requests_before);
  CHECK(world.invariant_holds());
}

TEST_CASE("cargo is retained when storage fills before delivery", "[world][forage]") {
  ant::sim::World world(101);
  bool carrying = false;
  for (int tick = 0; tick < 4'000 && !carrying; ++tick) {
    world.step();
    for (const auto& actor : world.actors()) {
      carrying = carrying || (actor.cargo_kind == ant::sim::CargoKind::Food && actor.cargo_amount > 0);
    }
  }
  REQUIRE(carrying);
  // Excavation raises store capacity as the nest grows, so refill to the current capacity every
  // tick and watch for the moment a loaded worker finds no room rather than sampling only at the
  // end of a fixed window.
  bool retained = false;
  for (int tick = 0; tick < 2'400 && !retained; ++tick) {
    world.debug_set_store(ant::sim::Nutrient::Carbohydrate, world.stores().carbohydrate_capacity);
    world.debug_set_store(ant::sim::Nutrient::Protein, world.stores().protein_capacity);
    world.step();
    for (const auto& actor : world.actors()) {
      retained = retained || (actor.cargo_kind == ant::sim::CargoKind::Food &&
                              actor.cargo_amount > 0 &&
                              actor.forage_state == ant::sim::ForageState::WaitingForStorage);
    }
  }
  CHECK(retained);
  CHECK(world.invariant_holds());
}

TEST_CASE("autonomous colony excavates and raises worker brood", "[world][m2]") {
  ant::sim::World world(42);
  world.run_ticks(8'000);

  CHECK(world.stats().cells_excavated > 0);
  CHECK(world.stats().eggs_laid > 0);
  CHECK(world.stats().workers_born > 0);
  CHECK(world.nursery_capacity() >= 12);
  CHECK(world.invariant_holds());
}

TEST_CASE("colony focus command unlocks at twelve workers", "[world][tasks]") {
  ant::game::Session session(42);
  CHECK_FALSE(session.set_focus(ant::sim::Focus::Expansion));
  session.step_ticks(8'000);
  REQUIRE(session.focus_available());
  CHECK(session.set_focus(ant::sim::Focus::Expansion));
  CHECK(session.world().focus() == ant::sim::Focus::Expansion);
}

TEST_CASE("queen death blocks laying while existing pupae can mature", "[world][brood]") {
  ant::sim::World world(7);
  world.debug_spawn_brood(ant::sim::BroodStage::Pupa, 890);
  world.debug_kill_queen();
  const auto eggs_before = world.stats().eggs_laid;
  world.run_ticks(100);

  CHECK(world.decline());
  CHECK(world.stats().eggs_laid == eggs_before);
  CHECK(world.stats().workers_born == 1);
}

TEST_CASE("larval shortage stalls development then respects starvation grace", "[world][brood]") {
  ant::sim::World world(3);
  world.debug_set_source_amount(0, 0);
  world.debug_set_source_amount(1, 0);
  world.debug_set_store(ant::sim::Nutrient::Carbohydrate, 0);
  world.debug_set_store(ant::sim::Nutrient::Protein, 0);
  world.debug_spawn_brood(ant::sim::BroodStage::Larva, 400,
                          179 * ant::sim::kTicksPerSecond);
  world.run_ticks(20);

  CHECK(world.brood().empty());
  CHECK(world.stats().workers_born == 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("worker aging creates a corpse that respects maturation time", "[world][death]") {
  ant::sim::World world(9);
  const auto actors = world.actors();
  REQUIRE(actors.size() > 1);
  world.debug_set_worker_lifespan(actors[1].id, actors[1].age + 20);
  world.run_ticks(20);
  REQUIRE(world.corpses().size() == 1);
  CHECK_FALSE(world.corpses()[0].cleanable);
  world.run_ticks(29 * ant::sim::kTicksPerSecond);
  REQUIRE(world.corpses().size() == 1);
  CHECK(world.corpses()[0].cleanable);
}

TEST_CASE("sixty simulated minutes stay valid and terminally live", "[world][soak]") {
  ant::sim::World world(101);
  world.run_ticks(60 * 60 * ant::sim::kTicksPerSecond);
  CHECK(world.invariant_holds());
  CHECK_FALSE(world.extinct());
  CHECK(world.stores().carbohydrate >= 0);
  CHECK(world.stores().protein >= 0);
}

TEST_CASE("stored food is exactly the heaps lying in the nest", "[world][granary]") {
  ant::sim::World world(11);
  world.run_ticks(6'000);

  std::int64_t carbohydrate = 0;
  std::int64_t protein = 0;
  std::set<std::pair<int, int>> cells;
  for (const ant::sim::FoodPile& pile : world.granary()) {
    CHECK(pile.amount >= 0);
    CHECK(pile.amount <= ant::sim::kGrainsPerStoreCell);
    CHECK(world.grid().at(pile.position) == ant::sim::Material::Air);
    CHECK(cells.insert({pile.position.x, pile.position.y}).second);
    (pile.nutrient == ant::sim::Nutrient::Carbohydrate ? carbohydrate : protein) += pile.amount;
  }
  CHECK(carbohydrate == world.stores().carbohydrate);
  CHECK(protein == world.stores().protein);
  // Capacity is the room the colony has actually dug, so it is a whole number of store cells.
  CHECK(world.stores().carbohydrate_capacity % ant::sim::kGrainsPerStoreCell == 0);
  CHECK(world.stores().protein_capacity % ant::sim::kGrainsPerStoreCell == 0);
  CHECK(world.stores().carbohydrate_capacity > 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("roots are mined slowly and stone is never mined at all", "[world][dig]") {
  const auto count = [](const ant::sim::Grid& grid, const ant::sim::Material material) {
    return std::count(grid.cells().begin(), grid.cells().end(), material);
  };
  ant::sim::World world(3);
  const ant::sim::Grid before = world.grid();
  REQUIRE(count(before, ant::sim::Material::Stone) > 0);
  REQUIRE(count(before, ant::sim::Material::Root) > 0);

  world.run_ticks(20'000);
  CHECK(count(world.grid(), ant::sim::Material::Stone) ==
        count(before, ant::sim::Material::Stone));
  // Root costs four times what soil does, so a colony that has dug hundreds of cells has still
  // only chewed through a handful of them.
  CHECK(count(world.grid(), ant::sim::Material::Root) <= count(before, ant::sim::Material::Root));
  CHECK(world.invariant_holds());
}

TEST_CASE("a forage site is finite and the colony scouts for the next one", "[world][forage]") {
  ant::sim::World world(5);
  // Empty the known sites and put an undiscovered one on the surface well away from the entrance.
  world.debug_set_source_amount(0, 0);
  world.debug_set_source_amount(1, 0);
  const ant::sim::GridPos hidden{world.home().x - 70, 31};
  const ant::sim::EntityId planted =
      world.debug_add_source(hidden, ant::sim::Nutrient::Carbohydrate, 80'000);
  // Short of sugar, but not so short that the colony starves before anyone can look.
  world.debug_set_store(ant::sim::Nutrient::Carbohydrate,
                        world.stores().carbohydrate_capacity / 4);
  CHECK(world.wants_scouts());

  bool scouted = false;
  for (int tick = 0; tick < 400 && !scouted; ++tick) {
    world.run_ticks(1);
    for (const auto& actor : world.actors()) {
      scouted = scouted || actor.forage_state == ant::sim::ForageState::Scouting;
    }
  }
  CHECK(scouted);

  const auto known = [&] {
    const auto found = std::find_if(world.sources().begin(), world.sources().end(),
        [planted](const ant::sim::FoodSource& s) { return s.id == planted; });
    return found != world.sources().end() && found->known;
  };
  for (int tick = 0; tick < 6'000 && !known(); ++tick) world.run_ticks(1);
  REQUIRE(known());
  // Finding it alerts the colony: the site is the one everybody is being sent to.
  CHECK(world.recruiting_source() == planted);
  CHECK(world.stats().sources_found >= 1);

  // And the spent sites are gone rather than lingering at zero.
  CHECK(world.stats().sources_exhausted >= 2);
  CHECK(world.invariant_holds());
}

TEST_CASE("brood and grain live in the rooms dug for them", "[world][rooms]") {
  ant::sim::World world(13);
  world.run_ticks(8'000);

  const auto owns = [&](const ant::sim::GridPos cell, const ant::sim::RoomKind kind) {
    return std::any_of(world.rooms().begin(), world.rooms().end(),
                       [&](const ant::sim::Room& room) {
                         return room.kind == kind && room.contains(cell);
                       });
  };
  for (const ant::sim::FoodPile& pile : world.granary()) {
    CAPTURE(pile.position.x, pile.position.y);
    CHECK(owns(pile.position, ant::sim::RoomKind::Granary));
  }
  for (const ant::sim::BroodSnapshot& item : world.brood()) {
    CAPTURE(item.position.x, item.position.y);
    // Lying in a brood room, still beside the queen who laid it, or in a nurse's mandibles.
    CHECK((owns(item.position, ant::sim::RoomKind::Nursery) || item.carried_by != 0 ||
           item.position == world.home()));
  }
  CHECK(world.invariant_holds());
}
