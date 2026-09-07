#include "app/save_coordinator.hpp"

#include <algorithm>
#include <stdexcept>

namespace ant::app {
namespace {

std::string describe(const SaveTrigger trigger) {
  switch (trigger) {
  case SaveTrigger::Autosave: return "Autosaved";
  case SaveTrigger::Manual: return "Saved";
  case SaveTrigger::Exit: return "Saved on exit";
  case SaveTrigger::None: break;
  }
  return "Saved";
}

} // namespace

SaveCoordinator::SaveCoordinator(const double autosave_interval_seconds)
    : autosave_interval_seconds_(autosave_interval_seconds) {
  if (!(autosave_interval_seconds_ > 0.0)) {
    throw std::invalid_argument("autosave interval must be positive");
  }
}

void SaveCoordinator::request_manual() { manual_requested_ = true; }

SaveTrigger SaveCoordinator::poll(const double real_delta_seconds, const bool saving_enabled) {
  if (!saving_enabled) {
    manual_requested_ = false;
    since_autosave_ = 0.0;
    return SaveTrigger::None;
  }
  if (manual_requested_) {
    manual_requested_ = false;
    since_autosave_ = 0.0;
    return SaveTrigger::Manual;
  }
  // A long stall must not queue a burst of catch-up saves.
  since_autosave_ += std::clamp(real_delta_seconds, 0.0, autosave_interval_seconds_);
  if (since_autosave_ < autosave_interval_seconds_) return SaveTrigger::None;
  since_autosave_ = 0.0;
  return SaveTrigger::Autosave;
}

void SaveCoordinator::record_success(const SaveTrigger trigger, const std::uint64_t revision,
                                     const bool durability_uncertain) {
  status_ = describe(trigger) + " (revision " + std::to_string(revision) + ")";
  if (durability_uncertain) status_ += " - written, durability unconfirmed";
  since_autosave_ = 0.0;
}

void SaveCoordinator::record_failure(const SaveTrigger trigger, const std::string_view error) {
  status_ = describe(trigger) + " failed: " + std::string(error);
  since_autosave_ = 0.0;
}

void SaveCoordinator::set_status(std::string message) { status_ = std::move(message); }

double SaveCoordinator::seconds_until_autosave() const {
  return std::max(0.0, autosave_interval_seconds_ - since_autosave_);
}

const char* save_trigger_name(const SaveTrigger trigger) {
  switch (trigger) {
  case SaveTrigger::None: return "none";
  case SaveTrigger::Autosave: return "autosave";
  case SaveTrigger::Manual: return "manual";
  case SaveTrigger::Exit: return "exit";
  }
  return "none";
}

} // namespace ant::app
