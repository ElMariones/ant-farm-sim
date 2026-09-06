#pragma once

#include "sim/grid.hpp"

#include <cstdint>
#include <vector>

namespace ant::sim {

class TrailField {
public:
  TrailField();

  void deposit(GridPos position, std::uint16_t amount);
  void update(const Grid& grid);
  [[nodiscard]] std::uint16_t at(GridPos position) const;
  [[nodiscard]] std::uint64_t mass() const;
  [[nodiscard]] const std::vector<std::uint16_t>& cells() const { return front_; }

private:
  [[nodiscard]] static std::size_t index(GridPos position);
  std::vector<std::uint16_t> front_;
  std::vector<std::uint16_t> back_;
  std::vector<std::size_t> active_;
  std::vector<std::uint8_t> marked_;
};

} // namespace ant::sim
