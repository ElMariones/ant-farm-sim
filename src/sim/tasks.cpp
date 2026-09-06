#include "sim/tasks.hpp"

#include <algorithm>
#include <vector>

namespace ant::sim {

std::uint32_t response_weight(const std::uint16_t stimulus, const std::uint16_t threshold) {
  if (stimulus == 0) {
    return 0;
  }
  const std::uint64_t s2 = static_cast<std::uint64_t>(stimulus) * stimulus;
  const std::uint64_t t2 = static_cast<std::uint64_t>(threshold) * threshold;
  return static_cast<std::uint32_t>((s2 * 10'000ULL) / (s2 + t2));
}

Task choose_weighted_task(const std::span<const TaskWeight> candidates, Pcg32& rng) {
  std::vector<TaskWeight> ordered(candidates.begin(), candidates.end());
  std::sort(ordered.begin(), ordered.end(), [](const TaskWeight lhs, const TaskWeight rhs) {
    return static_cast<std::uint8_t>(lhs.task) < static_cast<std::uint8_t>(rhs.task);
  });
  std::uint64_t total = 0;
  for (const TaskWeight candidate : ordered) {
    total += candidate.weight;
  }
  if (total == 0) {
    return Task::Idle;
  }
  const std::uint64_t draw = rng.bounded(static_cast<std::uint32_t>(total));
  std::uint64_t cursor = 0;
  for (const TaskWeight candidate : ordered) {
    cursor += candidate.weight;
    if (draw < cursor) {
      return candidate.task;
    }
  }
  return Task::Idle;
}

const char* task_name(const Task task) {
  switch (task) {
  case Task::Forage: return "Foraging";
  case Task::Excavate: return "Excavating";
  case Task::Nurse: return "Nursing brood";
  case Task::Clean: return "Cleaning nest";
  case Task::Idle: return "Resting in the nest";
  }
  return "Resting";
}

} // namespace ant::sim
