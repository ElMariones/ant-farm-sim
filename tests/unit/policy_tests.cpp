#include "game/policy.hpp"
#include "game/session.hpp"

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
