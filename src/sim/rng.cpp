#include "sim/rng.hpp"

#include <stdexcept>

namespace ant::sim {

Pcg32 Pcg32::restore(const std::uint64_t state, const std::uint64_t increment) {
  if ((increment & 1U) == 0) throw std::invalid_argument("PCG increment must be odd");
  Pcg32 rng;
  rng.state_ = state;
  rng.increment_ = increment;
  return rng;
}

Pcg32::Pcg32(const std::uint64_t seed, const std::uint64_t stream)
    : increment_((stream << 1U) | 1U) {
  static_cast<void>(next());
  state_ += seed;
  static_cast<void>(next());
}

std::uint32_t Pcg32::next() {
  const std::uint64_t old_state = state_;
  state_ = old_state * 6364136223846793005ULL + increment_;
  const auto xor_shifted = static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
  const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
  return (xor_shifted >> rotation) | (xor_shifted << ((-rotation) & 31U));
}

std::uint32_t Pcg32::bounded(const std::uint32_t bound) {
  if (bound == 0U) {
    throw std::invalid_argument("PCG bound must be positive");
  }
  const std::uint32_t threshold = static_cast<std::uint32_t>(-bound) % bound;
  for (;;) {
    const std::uint32_t value = next();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

std::uint64_t mix_seed(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

} // namespace ant::sim
