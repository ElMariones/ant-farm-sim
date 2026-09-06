#include "game/session.hpp"

#include <catch2/catch_test_macros.hpp>

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
  constexpr std::int64_t initial_mass = 290'000;
  world.run_ticks(2'400);

  REQUIRE(world.stats().completed_round_trips > 0);
  CHECK(world.stats().picked_up == world.stats().delivered + cargo_total(world));
  const std::int64_t source_mass = world.sources()[0].amount + world.sources()[1].amount;
  const std::int64_t store_mass = world.stores().carbohydrate + world.stores().protein;
  CHECK(source_mass + store_mass + cargo_total(world) + world.stats().consumed_carbohydrate +
            world.stats().consumed_protein ==
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
  for (int tick = 0; tick < 100 && world.stats().path_requests == 0; ++tick) world.step();
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
  for (int tick = 0; tick < 1'200 && !carrying; ++tick) {
    world.step();
    for (const auto& actor : world.actors()) {
      carrying = carrying || (actor.cargo_kind == ant::sim::CargoKind::Food && actor.cargo_amount > 0);
    }
  }
  REQUIRE(carrying);
  world.debug_set_store(ant::sim::Nutrient::Carbohydrate, world.stores().carbohydrate_capacity);
  world.debug_set_store(ant::sim::Nutrient::Protein, world.stores().protein_capacity);
  world.run_ticks(1'200);

  bool retained = false;
  for (const auto& actor : world.actors()) {
    retained = retained || (actor.cargo_kind == ant::sim::CargoKind::Food && actor.cargo_amount > 0 &&
                            actor.forage_state == ant::sim::ForageState::WaitingForStorage);
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
  world.debug_set_source_refill(0, 0);
  world.debug_set_source_refill(1, 0);
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
