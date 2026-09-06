#include "game/session.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

std::int64_t cargo_total(const ant::sim::World& world) {
  std::int64_t total = 0;
  for (const auto& actor : world.actors()) {
    total += actor.cargo_amount;
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
  constexpr std::int64_t initial_mass = 28'000;
  world.run_ticks(2'400);

  REQUIRE(world.stats().completed_round_trips > 0);
  CHECK(world.stats().picked_up == world.stats().delivered + cargo_total(world));
  const std::int64_t source_mass = world.sources()[0].amount + world.sources()[1].amount;
  const std::int64_t store_mass = world.stores().carbohydrate + world.stores().protein;
  CHECK(source_mass + store_mass + cargo_total(world) ==
        initial_mass + world.stats().external_refill);
  CHECK(world.invariant_holds());
}

TEST_CASE("only one worker can reserve the final source unit", "[world][forage]") {
  ant::sim::World world(42);
  world.debug_set_source_amount(0, 1'000);
  world.debug_set_source_amount(1, 0);
  world.step();

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
  world.step();

  CHECK(world.sources()[0].reserved == 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("topology changes invalidate active paths before movement", "[world][navigation]") {
  ant::sim::World world(42);
  world.step();
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
      carrying = carrying || actor.cargo_amount > 0;
    }
  }
  REQUIRE(carrying);
  world.debug_set_store(ant::sim::Nutrient::Carbohydrate, world.stores().capacity_per_nutrient);
  world.debug_set_store(ant::sim::Nutrient::Protein, world.stores().capacity_per_nutrient);
  world.run_ticks(1'200);

  bool retained = false;
  for (const auto& actor : world.actors()) {
    retained = retained || (actor.cargo_amount > 0 &&
                            actor.forage_state == ant::sim::ForageState::WaitingForStorage);
  }
  CHECK(retained);
  CHECK(world.invariant_holds());
}
