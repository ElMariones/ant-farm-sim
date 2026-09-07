#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "persistence/profile_codec.hpp"
#include "sim/world.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using ant::sim::AntKind;
using ant::sim::BroodRole;
using ant::sim::BroodStage;
using ant::sim::World;

constexpr ant::sim::Tick kSecond = ant::sim::kTicksPerSecond;
constexpr ant::sim::Tick kMaturityAge = 720 * kSecond;

int count_kind(const World& world, const AntKind kind) {
  int total = 0;
  for (const auto& actor : world.actors()) {
    if (actor.kind == kind) ++total;
  }
  return total;
}

// Runs the clock out with the small founding colony, then supplies the population and births.
// Simulating twelve minutes with 100+ workers costs minutes of test time, and the order does not
// change what the maturity rule sees.
World aged_colony(const ant::sim::Tick age = kMaturityAge + kSecond) {
  World world(42);
  world.run_ticks(age);
  return world;
}

// A small colony that has been told it is mature, for testing caste assignment on its own.
World mature_small_colony(const std::uint64_t seed = 42) {
  World world(seed);
  world.run_ticks(200);
  world.debug_set_mature();
  return world;
}

} // namespace

TEST_CASE("maturity requires workers, births and run age together", "[world][maturity]") {
  SECTION("age alone is not enough") {
    World world = aged_colony();
    REQUIRE(world.living_workers() < 100);
    CHECK_FALSE(world.mature());
  }
  SECTION("workers and births without the age are not enough") {
    World world(42);
    world.debug_spawn_workers(100);
    world.debug_set_workers_born(150);
    world.run_ticks(2 * kSecond);
    REQUIRE(world.living_workers() >= 100);
    REQUIRE(world.stats().workers_born >= 150);
    REQUIRE(world.tick() < kMaturityAge);
    CHECK_FALSE(world.mature());
  }
  SECTION("one birth short of the threshold is not enough") {
    World world = aged_colony();
    world.debug_spawn_workers(100);
    world.debug_set_workers_born(149);
    world.run_ticks(2 * kSecond);
    REQUIRE(world.living_workers() >= 100);
    CHECK_FALSE(world.mature());
  }
  SECTION("one worker short of the threshold is not enough") {
    World world = aged_colony();
    world.debug_set_workers_born(150);
    while (world.living_workers() < 99) world.debug_spawn_workers(1);
    world.run_ticks(2 * kSecond);
    REQUIRE(world.living_workers() == 99);
    CHECK_FALSE(world.mature());
  }
  SECTION("all three together latch maturity") {
    World world = aged_colony();
    world.debug_spawn_workers(100);
    world.debug_set_workers_born(150);
    REQUIRE_FALSE(world.mature());
    world.run_ticks(2 * kSecond);
    CHECK(world.mature());
  }
}

TEST_CASE("a falling population never revokes maturity", "[world][maturity]") {
  World world = aged_colony();
  world.debug_spawn_workers(100);
  world.debug_set_workers_born(150);
  world.run_ticks(2 * kSecond);
  REQUIRE(world.mature());

  world.debug_kill_workers(80);
  world.run_ticks(2 * kSecond);

  CHECK(world.living_workers() < 100);
  CHECK(world.mature());
}

TEST_CASE("no winged queens are allocated before maturity", "[world][maturity][brood]") {
  World world(42);
  world.run_ticks(600 * kSecond);
  REQUIRE(world.stats().eggs_laid > 10);
  CHECK_FALSE(world.mature());
  CHECK(world.egg_assignment_counter() == 0);
  CHECK(world.winged_brood() == 0);
  CHECK(world.stats().gynes_born == 0);
}

TEST_CASE("one egg in five becomes a winged queen after maturity", "[world][maturity][brood]") {
  World world = mature_small_colony();
  const std::uint64_t eggs_before = world.stats().eggs_laid;
  world.run_ticks(400 * kSecond);

  const std::uint64_t assigned = world.egg_assignment_counter();
  REQUIRE(assigned >= 10);
  // The counter tracks every egg laid since maturity, and only those.
  CHECK(assigned == world.stats().eggs_laid - eggs_before);

  const int gynes = world.winged_brood() + static_cast<int>(world.stats().gynes_born);
  CHECK(gynes == static_cast<int>(assigned / 5));
  CHECK(gynes > 0);
  CHECK(world.invariant_holds());
}

TEST_CASE("worker birth statistics never count winged queens", "[world][maturity][brood]") {
  World world(7);
  const std::uint64_t births_before = world.stats().workers_born;
  world.debug_spawn_brood(BroodStage::Pupa, 1'790, 0, BroodRole::Gyne);
  world.run_ticks(3 * kSecond);

  CHECK(world.stats().workers_born == births_before);
  CHECK(world.stats().gynes_born == 1);
  CHECK(count_kind(world, AntKind::WingedQueen) == 1);
  CHECK(world.live_winged_queens() == 1);
  CHECK(world.invariant_holds());
}

TEST_CASE("winged queens take twice as long to develop as workers", "[world][maturity][brood]") {
  World world(7);
  world.debug_spawn_brood(BroodStage::Egg, 0, 0, BroodRole::Worker);
  world.debug_spawn_brood(BroodStage::Egg, 0, 0, BroodRole::Gyne);
  world.debug_spawn_brood(BroodStage::Larva, 0, 0, BroodRole::Worker);
  world.debug_spawn_brood(BroodStage::Larva, 0, 0, BroodRole::Gyne);
  world.debug_spawn_brood(BroodStage::Pupa, 0, 0, BroodRole::Worker);
  world.debug_spawn_brood(BroodStage::Pupa, 0, 0, BroodRole::Gyne);

  const auto brood = world.brood();
  REQUIRE(brood.size() >= 6);
  for (std::size_t stage = 0; stage < 3; ++stage) {
    CHECK(brood[stage * 2 + 1].target == brood[stage * 2].target * 2);
  }

  // And a gyne really does reach adulthood as a winged queen rather than a worker.
  World emerging(7);
  emerging.debug_spawn_brood(BroodStage::Pupa, 1'790, 0, BroodRole::Gyne);
  emerging.run_ticks(3 * kSecond);
  CHECK(emerging.stats().gynes_born == 1);
  CHECK(emerging.stats().workers_born == 0);
}

TEST_CASE("the combined winged cap of ten counts brood as well as adults",
          "[world][maturity][brood]") {
  World world = mature_small_colony();
  for (int i = 0; i < 10; ++i) world.debug_spawn_brood(BroodStage::Pupa, 1'400, 0, BroodRole::Gyne);
  REQUIRE(world.winged_brood() == 10);

  // While ten are still developing, the cap already blocks further allocation.
  world.run_ticks(8 * kSecond);
  CHECK(world.live_winged_queens() + world.winged_brood() == 10);
  CHECK(world.egg_assignment_counter() > 0);

  // After they emerge the cap is held by the adults instead.
  world.run_ticks(240 * kSecond);
  REQUIRE(world.live_winged_queens() == 10);
  CHECK(world.winged_brood() == 0);
  CHECK(world.stats().gynes_born == 10);
  CHECK(world.live_winged_queens() + world.winged_brood() == 10);
  CHECK(world.invariant_holds());
}

TEST_CASE("winged queens do not work", "[world][maturity]") {
  World world(7);
  world.debug_spawn_brood(BroodStage::Pupa, 1'790, 0, BroodRole::Gyne);
  world.run_ticks(3 * kSecond);
  REQUIRE(world.live_winged_queens() == 1);

  const int workers = world.living_workers();
  world.run_ticks(400);
  CHECK(world.live_winged_queens() == 1);
  CHECK(world.living_workers() == workers);
  for (const auto& actor : world.actors()) {
    if (actor.kind == AntKind::WingedQueen) CHECK(actor.task == ant::sim::Task::Idle);
  }
}

TEST_CASE("the egg assignment sequence survives a save and load",
          "[world][maturity][persistence]") {
  ant::game::Session live(42);
  live.debug_world().run_ticks(200);
  live.debug_world().debug_set_mature();
  live.step_ticks(300 * kSecond);
  REQUIRE(live.world().mature());
  REQUIRE(live.world().egg_assignment_counter() > 0);

  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(live)));
  REQUIRE(decoded.profile.has_value());
  ant::game::Session resumed = ant::game::Session::restore(*decoded.profile->run);

  CHECK(resumed.world().mature());
  CHECK(resumed.world().egg_assignment_counter() == live.world().egg_assignment_counter());
  CHECK(resumed.world().live_winged_queens() == live.world().live_winged_queens());
  CHECK(resumed.world().winged_brood() == live.world().winged_brood());
  CHECK(resumed.world().canonical_hash() == live.world().canonical_hash());

  // The sequence continues from where it left off rather than restarting.
  resumed.step_ticks(200 * kSecond);
  live.step_ticks(200 * kSecond);
  CHECK(resumed.world().egg_assignment_counter() == live.world().egg_assignment_counter());
  CHECK(resumed.world().canonical_hash() == live.world().canonical_hash());
}

TEST_CASE("permanent traits change founding parameters exactly once", "[world][traits]") {
  SECTION("Vigor II adds two founding workers") {
    const World plain(42);
    const World vigorous(42, {2, 0});
    CHECK(plain.living_workers() == 6);
    CHECK(vigorous.living_workers() == 8);
  }
  SECTION("Vigor I shortens the egg stage") {
    World plain(42);
    World vigorous(42, {1, 0});
    plain.debug_spawn_brood(BroodStage::Egg);
    vigorous.debug_spawn_brood(BroodStage::Egg);
    CHECK(plain.brood()[0].target == 600);       // 30 s
    CHECK(vigorous.brood()[0].target == 540);    // 30 s * 0.90
  }
  SECTION("Vigor III shortens the larva and pupa stages but not the egg") {
    World vigorous(42, {3, 0});
    vigorous.debug_spawn_brood(BroodStage::Egg);
    vigorous.debug_spawn_brood(BroodStage::Larva);
    vigorous.debug_spawn_brood(BroodStage::Pupa);
    CHECK(vigorous.brood()[0].target == 540);    // egg, 600 * 0.90
    CHECK(vigorous.brood()[1].target == 1'020);  // larva, 1200 * 0.85
    CHECK(vigorous.brood()[2].target == 765);    // pupa, 900 * 0.85
  }
  SECTION("a gyne still takes twice the trait-adjusted duration") {
    World vigorous(42, {3, 0});
    vigorous.debug_spawn_brood(BroodStage::Pupa, 0, 0, BroodRole::Gyne);
    CHECK(vigorous.brood()[0].target == 1'530);  // 765 * 2
  }
  SECTION("Industry I raises the dig rate") {
    World plain(42, {0, 0});
    World industrious(42, {0, 1});
    plain.run_ticks(6'000);
    industrious.run_ticks(6'000);
    CHECK(industrious.stats().cells_excavated >= plain.stats().cells_excavated);
  }
  SECTION("traits are carried through a save and are not reapplied") {
    ant::game::Session session(42, ant::game::canonical_progression(), {3, 2});
    session.step_ticks(600);
    const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
        ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
    REQUIRE(decoded.profile.has_value());
    const ant::game::Session resumed = ant::game::Session::restore(*decoded.profile->run);
    CHECK(resumed.world().traits() == ant::sim::TraitModifiers{3, 2});
    CHECK(resumed.world().living_workers() == session.world().living_workers());
    CHECK(resumed.world().canonical_hash() == session.world().canonical_hash());
  }
}
