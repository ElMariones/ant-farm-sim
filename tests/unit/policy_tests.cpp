#include "game/policy.hpp"
#include "game/session.hpp"
#include "persistence/profile_codec.hpp"

#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr ant::sim::Tick kSecond = ant::sim::kTicksPerSecond;

} // namespace

TEST_CASE("the scenario policy buys the cheapest adaptation with a fixed tie-break",
          "[game][policy]") {
  ant::game::Session session(42);
  session.debug_grant_work(15);

  ant::game::UpgradeId choice{ant::game::UpgradeId::Queen};
  REQUIRE(ant::game::cheapest_affordable_upgrade(session, choice));
  // All four cost the same at level 0, so the documented order decides.
  CHECK(choice == ant::game::UpgradeId::Excavation);

  REQUIRE(session.buy_upgrade(ant::game::UpgradeId::Excavation).accepted);
  session.debug_grant_work(15);
  REQUIRE(ant::game::cheapest_affordable_upgrade(session, choice));
  // Excavation now costs 24 and the rest still cost 15, so the cheapest is nursing.
  CHECK(choice == ant::game::UpgradeId::Nursing);
}

TEST_CASE("nothing is affordable without Work", "[game][policy]") {
  const ant::game::Session session(42);
  ant::game::UpgradeId choice{ant::game::UpgradeId::Excavation};
  CHECK_FALSE(ant::game::cheapest_affordable_upgrade(session, choice));
}

TEST_CASE("the policy acts once per simulated second, not per frame", "[game][policy]") {
  // Two runs of the same policy over the same number of ticks must agree exactly, whatever
  // cadence the caller uses to reach that tick count.
  ant::game::PolicyOptions options;
  options.buy_upgrades = true;
  options.stop_at_flight = false;

  ant::game::Session one(42);
  ant::game::ScenarioPolicy policy_one(options);
  policy_one.run(one, 600 * kSecond);

  ant::game::Session two(42);
  ant::game::ScenarioPolicy policy_two(options);
  for (int chunk = 0; chunk < 6; ++chunk) policy_two.run(two, 100 * kSecond);

  CHECK(one.world().canonical_hash() == two.world().canonical_hash());
  CHECK(one.work() == two.work());
  CHECK(one.upgrade_levels() == two.upgrade_levels());
  CHECK(policy_one.milestones().purchases == policy_two.milestones().purchases);
  CHECK(policy_one.milestones().first_purchase == policy_two.milestones().first_purchase);
}

TEST_CASE("a no-purchase colony still reaches its milestones", "[game][policy]") {
  ant::game::PolicyOptions options;
  options.buy_upgrades = false;
  options.stop_at_flight = false;

  ant::game::Session session(42);
  ant::game::ScenarioPolicy policy(options);
  policy.run(session, 900 * kSecond);

  const ant::game::RunMilestones& milestones = policy.milestones();
  CHECK(milestones.first_delivery > 0);
  CHECK(milestones.first_birth > 0);
  CHECK(milestones.purchases == 0);
  CHECK_FALSE(milestones.extinct);
  CHECK(session.world().living_workers() > 6);
  CHECK(session.work() > 0);
  CHECK(session.world().invariant_holds());
}

TEST_CASE("foragers follow the nutrient the colony is short of", "[world][forage]") {
  // Regression: source choice used to follow trail strength alone, so a colony could haul one
  // nutrient until its store capped while starving for the other.
  ant::sim::World world(1);
  world.run_ticks(600 * kSecond);

  const ant::sim::FoodStore& stores = world.stores();
  CHECK(stores.carbohydrate > 0);
  CHECK(stores.protein > 0);
  // Neither store is pinned at capacity while the other sits empty.
  CHECK_FALSE((stores.carbohydrate >= stores.carbohydrate_capacity && stores.protein == 0));
  CHECK_FALSE((stores.protein >= stores.protein_capacity && stores.carbohydrate == 0));
  CHECK(world.living_workers() > 6);
  CHECK(world.invariant_holds());
}

TEST_CASE("workers never commit more food to a store than it can accept", "[world][forage]") {
  ant::sim::World world(42);
  world.run_ticks(400 * kSecond);

  // Reserved-at-source plus already-carried can never exceed the room left in the store.
  for (const ant::sim::FoodSource& source : world.sources()) {
    std::int64_t carried = 0;
    for (const auto& actor : world.actors()) {
      if (actor.cargo_kind == ant::sim::CargoKind::Food && actor.cargo_nutrient == source.nutrient) {
        carried += actor.cargo_amount;
      }
    }
    const bool carbohydrate = source.nutrient == ant::sim::Nutrient::Carbohydrate;
    const std::int64_t store = carbohydrate ? world.stores().carbohydrate : world.stores().protein;
    const std::int64_t capacity =
        carbohydrate ? world.stores().carbohydrate_capacity : world.stores().protein_capacity;
    CHECK(store + carried + source.reserved <= capacity + 2'000);
  }
  CHECK(world.invariant_holds());
}


namespace {

int pile_height(const ant::sim::World& world, const int column) {
  int height = 0;
  while (height < 32 && !ant::sim::is_passable(world.grid().at({column, 31 - height}))) ++height;
  return height;
}

} // namespace

TEST_CASE("excavated grains are tipped onto a real surface mound", "[world][excavation]") {
  ant::sim::World world(42);
  const int entrance = world.home().x;
  REQUIRE(pile_height(world, entrance) == 0);

  world.run_ticks(1'200 * kSecond);
  REQUIRE(world.stats().spoil_delivered > 20);

  // Every delivered grain that found a home is a cell of terrain the colony actually placed.
  CHECK(world.spoil_mound() > 20);
  CHECK(world.spoil_mound() <= world.stats().spoil_delivered);

  // The entrance and its shoulders stay clear, so the nest never buries itself.
  CHECK(pile_height(world, entrance) == 0);
  CHECK(pile_height(world, entrance - 1) == 0);
  CHECK(pile_height(world, entrance + 1) == 0);

  // The spoil forms a cone: taller near the entrance than out at the edges.
  const int inner = pile_height(world, entrance - 3);
  const int outer = pile_height(world, entrance - 16);
  CHECK(inner > 0);
  CHECK(inner >= outer);

  // And it is roughly symmetric, because grains settle on whichever side is lower.
  CHECK(std::abs(pile_height(world, entrance - 5) - pile_height(world, entrance + 5)) <= 3);
  CHECK(world.invariant_holds());
}

TEST_CASE("a growing mound never traps the colony", "[world][excavation]") {
  ant::sim::World world(7);
  world.run_ticks(1'500 * kSecond);

  REQUIRE(world.spoil_mound() > 0);
  // Foraging keeps working with the mound in the way: ants climb it rather than being walled in.
  CHECK(world.stats().completed_round_trips > 50);
  CHECK(world.stores().carbohydrate > 0);
  CHECK(world.stores().protein > 0);
  CHECK(world.living_workers() > 6);
  CHECK_FALSE(world.extinct());
  CHECK(world.invariant_holds());
}

TEST_CASE("terrain closing under a stale path survives a save", "[world][excavation][persistence]") {
  // Depositing spoil turns sky into soil, which can close over a path an ant had already planned.
  // That path is stale by construction, so the profile must still be valid.
  ant::game::Session live(42);
  for (int tick = 0; tick < 40'000; ++tick) {
    live.step();
    if (live.world().stats().spoil_delivered > 0) break;
  }
  REQUIRE(live.world().stats().spoil_delivered > 0);

  const auto decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(live)));
  REQUIRE(decoded.profile.has_value());
  ant::game::Session resumed = ant::game::Session::restore(*decoded.profile->run);
  CHECK(resumed.world().canonical_hash() == live.world().canonical_hash());

  live.step_ticks(2'000);
  resumed.step_ticks(2'000);
  CHECK(resumed.world().canonical_hash() == live.world().canonical_hash());
}
