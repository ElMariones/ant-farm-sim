#pragma once

#include "sim/rng.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace ant::sim {

enum class Task : std::uint8_t { Forage, Excavate, Nurse, Clean, Idle };
enum class Focus : std::uint8_t { Balanced, Growth, Expansion, Foraging };

struct TaskWeight {
  Task task{Task::Idle};
  std::uint32_t weight{};
};

[[nodiscard]] std::uint32_t response_weight(std::uint16_t stimulus, std::uint16_t threshold);
[[nodiscard]] Task choose_weighted_task(std::span<const TaskWeight> candidates, Pcg32& rng);
[[nodiscard]] const char* task_name(Task task);

} // namespace ant::sim
