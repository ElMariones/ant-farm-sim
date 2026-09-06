#include "sim/rng.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("PCG32 matches the published reference sequence", "[rng]") {
  ant::sim::Pcg32 rng(42, 54);
  constexpr std::array<std::uint32_t, 5> expected{0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                                  0x83d2f293U, 0xbfa4784bU};
  for (const std::uint32_t value : expected) {
    CHECK(rng.next() == value);
  }
}

TEST_CASE("bounded PCG output stays inside the requested range", "[rng]") {
  ant::sim::Pcg32 rng(2026, 7);
  for (int sample = 0; sample < 10'000; ++sample) {
    CHECK(rng.bounded(17) < 17);
  }
  CHECK_THROWS_AS(rng.bounded(0), std::invalid_argument);
}
