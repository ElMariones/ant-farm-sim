#pragma once

#include "sim/types.hpp"
#include "sim/world.hpp"

#include <cstdint>

namespace ant::game {

class Session {
public:
  explicit Session(std::uint64_t seed);

  void step();
  void step_ticks(sim::Tick count);

  [[nodiscard]] const sim::World& world() const { return world_; }

private:
  sim::World world_;
};

} // namespace ant::game
