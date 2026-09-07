#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ant::app {

enum class SaveTrigger : std::uint8_t { None, Autosave, Manual, Exit };

// Schedules durable writes from real elapsed time and owns the short player-facing status line.
// Real seconds only decide when to write; they never advance the simulation.
class SaveCoordinator {
public:
  explicit SaveCoordinator(double autosave_interval_seconds = 30.0);

  void request_manual();

  // Consumes real elapsed time and reports which save, if any, is now due. While saving is
  // disabled — during a recovery prompt — no autosave accumulates and manual requests are dropped,
  // so a failed load can never overwrite the file it failed to read.
  [[nodiscard]] SaveTrigger poll(double real_delta_seconds, bool saving_enabled);

  void record_success(SaveTrigger trigger, std::uint64_t revision, bool durability_uncertain);
  void record_failure(SaveTrigger trigger, std::string_view error);
  void set_status(std::string message);

  [[nodiscard]] const std::string& status() const { return status_; }
  [[nodiscard]] double seconds_until_autosave() const;
  [[nodiscard]] double autosave_interval_seconds() const { return autosave_interval_seconds_; }

private:
  double autosave_interval_seconds_;
  double since_autosave_{};
  bool manual_requested_{};
  std::string status_;
};

[[nodiscard]] const char* save_trigger_name(SaveTrigger trigger);

} // namespace ant::app
