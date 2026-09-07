#include "app/save_coordinator.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using ant::app::SaveCoordinator;
using ant::app::SaveTrigger;

// One rendered frame at 60 Hz.
constexpr double kFrame = 1.0 / 60.0;

int autosaves_over(SaveCoordinator& coordinator, const double seconds, const double step,
                   const bool enabled = true) {
  int fired = 0;
  for (double elapsed = 0.0; elapsed < seconds; elapsed += step) {
    if (coordinator.poll(step, enabled) == SaveTrigger::Autosave) ++fired;
  }
  return fired;
}

} // namespace

TEST_CASE("autosave fires on the documented interval", "[app][save]") {
  SaveCoordinator coordinator(30.0);
  CHECK(coordinator.seconds_until_autosave() == 30.0);

  // Just under the interval nothing is due.
  CHECK(autosaves_over(coordinator, 29.0, kFrame) == 0);
  CHECK(coordinator.seconds_until_autosave() > 0.0);

  // Two further minutes produce one save per interval, not a backlog.
  CHECK(autosaves_over(coordinator, 120.0, kFrame) == 4);
}

TEST_CASE("a long stall does not queue a burst of catch-up saves", "[app][save]") {
  SaveCoordinator coordinator(30.0);
  // A single frame that took ten minutes of wall clock still schedules exactly one write.
  CHECK(coordinator.poll(600.0, true) == SaveTrigger::Autosave);
  CHECK(coordinator.poll(0.0, true) == SaveTrigger::None);
  CHECK(coordinator.seconds_until_autosave() == 30.0);
}

TEST_CASE("a manual save takes priority and restarts the autosave clock", "[app][save]") {
  SaveCoordinator coordinator(30.0);
  CHECK(autosaves_over(coordinator, 25.0, kFrame) == 0);

  coordinator.request_manual();
  CHECK(coordinator.poll(kFrame, true) == SaveTrigger::Manual);
  CHECK(coordinator.seconds_until_autosave() == 30.0);
  // The request is consumed exactly once.
  CHECK(coordinator.poll(kFrame, true) == SaveTrigger::None);

  CHECK(autosaves_over(coordinator, 25.0, kFrame) == 0);
  CHECK(autosaves_over(coordinator, 10.0, kFrame) == 1);
}

TEST_CASE("no save is scheduled while saving is disabled", "[app][save][recovery]") {
  SaveCoordinator coordinator(30.0);

  // A recovery prompt must never let an autosave run over the file that failed to load.
  CHECK(autosaves_over(coordinator, 300.0, kFrame, false) == 0);
  coordinator.request_manual();
  CHECK(coordinator.poll(kFrame, false) == SaveTrigger::None);

  // A dropped manual request does not fire later once saving resumes.
  CHECK(coordinator.poll(kFrame, true) == SaveTrigger::None);
  CHECK(autosaves_over(coordinator, 31.0, kFrame) == 1);
}

TEST_CASE("save outcomes are reported in the status line", "[app][save]") {
  SaveCoordinator coordinator(30.0);
  CHECK(coordinator.status().empty());

  coordinator.record_success(SaveTrigger::Autosave, 7, false);
  CHECK(coordinator.status() == "Autosaved (revision 7)");

  coordinator.record_success(SaveTrigger::Manual, 8, true);
  CHECK(coordinator.status().find("Saved (revision 8)") == 0);
  CHECK(coordinator.status().find(" - written, durability unconfirmed") != std::string::npos);

  coordinator.record_failure(SaveTrigger::Exit, "disk full");
  CHECK(coordinator.status() == "Saved on exit failed: disk full");

  coordinator.set_status("Colony resumed");
  CHECK(coordinator.status() == "Colony resumed");
}

TEST_CASE("a non-positive autosave interval is rejected", "[app][save]") {
  CHECK_THROWS(SaveCoordinator(0.0));
  CHECK_THROWS(SaveCoordinator(-5.0));
}
