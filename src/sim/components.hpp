#pragma once

#include "sim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ant::sim {

enum class AntKind : std::uint8_t { Queen, Worker };
enum class ForageState : std::uint8_t { AtHome, ToSource, Returning, WaitingForStorage };
enum class Nutrient : std::uint8_t { Carbohydrate, Protein };

struct Identity {
  EntityId id{};
};

struct Position {
  std::int32_t x_subcells{};
  std::int32_t y_subcells{};
  std::int32_t previous_x_subcells{};
  std::int32_t previous_y_subcells{};

  [[nodiscard]] GridPos cell() const {
    return {x_subcells / kSubcellsPerCell, y_subcells / kSubcellsPerCell};
  }
};

struct Ant {
  AntKind kind{AntKind::Worker};
};

struct Cargo {
  Nutrient nutrient{Nutrient::Carbohydrate};
  std::int64_t amount{};
};

struct Movement {
  std::vector<GridPos> path;
  std::size_t next_cell{};
  std::uint64_t path_revision{};
  int speed_residual{};
};

struct Forager {
  ForageState state{ForageState::AtHome};
  int source_index{-1};
  std::int64_t reserved_amount{};
  Tick reservation_expiry{};
  Tick retry_after{};
};

} // namespace ant::sim
