#pragma once

#include <cstdint>

namespace ant::sim {

class Pcg32 {
public:
  Pcg32(std::uint64_t seed, std::uint64_t stream);

  [[nodiscard]] std::uint32_t next();
  [[nodiscard]] std::uint32_t bounded(std::uint32_t bound);
  [[nodiscard]] std::uint64_t state() const { return state_; }
  [[nodiscard]] std::uint64_t increment() const { return increment_; }

private:
  std::uint64_t state_{0};
  std::uint64_t increment_{0};
};

[[nodiscard]] std::uint64_t mix_seed(std::uint64_t value);

} // namespace ant::sim
