#pragma once

#include "sim/types.hpp"
#include "sim/tasks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ant::sim {

enum class AntKind : std::uint8_t { Queen, Worker, WingedQueen };
enum class ForageState : std::uint8_t { AtHome, ToSource, Returning, WaitingForStorage };
enum class Nutrient : std::uint8_t { Carbohydrate, Protein };
enum class CargoKind : std::uint8_t { None, Food, Spoil, Corpse };
enum class BroodStage : std::uint8_t { Egg, Larva, Pupa };
// Which caste an egg was allocated to when it was laid. Fixed for the brood item's life.
enum class BroodRole : std::uint8_t { Worker, Gyne };

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
  CargoKind kind{CargoKind::None};
  Nutrient nutrient{Nutrient::Carbohydrate};
  std::int64_t amount{};
  EntityId entity_id{};
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

struct WorkerMind {
  Task task{Task::Idle};
  Tick committed_until{};
  std::array<std::uint16_t, 4> thresholds{};
  GridPos target{};
  bool has_target{};
  int action_ticks{};
};

struct Life {
  Tick age{};
  Tick lifespan{};
  Tick starvation{};
};

} // namespace ant::sim
