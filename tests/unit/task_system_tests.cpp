#include "sim/pheromones.hpp"
#include "sim/tasks.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("zero stimulus has no response", "[tasks]") {
  CHECK(ant::sim::response_weight(0, 250) == 0);
  CHECK(ant::sim::response_weight(500, 500) == 5'000);
}

TEST_CASE("weighted task choice is independent of candidate order", "[tasks]") {
  std::array<ant::sim::TaskWeight, 4> forward{{
      {ant::sim::Task::Forage, 5'000}, {ant::sim::Task::Excavate, 3'000},
      {ant::sim::Task::Nurse, 2'000}, {ant::sim::Task::Idle, 1'000}}};
  auto reverse = forward;
  std::reverse(reverse.begin(), reverse.end());
  ant::sim::Pcg32 first(44, 7);
  ant::sim::Pcg32 second(44, 7);
  for (int sample = 0; sample < 4'000; ++sample) {
    CHECK(ant::sim::choose_weighted_task(forward, first) ==
          ant::sim::choose_weighted_task(reverse, second));
  }
}

TEST_CASE("weighted task samples follow broad deterministic proportions", "[tasks]") {
  const std::array<ant::sim::TaskWeight, 3> weights{{
      {ant::sim::Task::Forage, 6'000}, {ant::sim::Task::Excavate, 3'000},
      {ant::sim::Task::Nurse, 0}}};
  ant::sim::Pcg32 rng(12, 99);
  int forage = 0;
  int excavate = 0;
  for (int sample = 0; sample < 9'000; ++sample) {
    const auto chosen = ant::sim::choose_weighted_task(weights, rng);
    forage += chosen == ant::sim::Task::Forage ? 1 : 0;
    excavate += chosen == ant::sim::Task::Excavate ? 1 : 0;
    CHECK(chosen != ant::sim::Task::Nurse);
  }
  CHECK(forage > 5'700);
  CHECK(forage < 6'300);
  CHECK(excavate > 2'700);
  CHECK(excavate < 3'300);
}

TEST_CASE("trail field decays and never gains mass during diffusion", "[pheromones]") {
  ant::sim::Grid grid(ant::sim::Material::Air);
  ant::sim::TrailField trails;
  trails.deposit({192, 100}, 50'000);
  const auto before = trails.mass();
  trails.update(grid);
  CHECK(trails.mass() <= before);
  const auto diffused = trails.mass();
  for (int update = 0; update < 100; ++update) trails.update(grid);
  CHECK(trails.mass() < diffused);
}
