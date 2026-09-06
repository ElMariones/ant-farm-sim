#include "game/session.hpp"

namespace ant::game {

Session::Session(const std::uint64_t seed) : world_(seed) {}

void Session::step() { world_.step(); }

void Session::step_ticks(const sim::Tick count) { world_.run_ticks(count); }

bool Session::focus_available() const {
  int workers = 0;
  for (const sim::ActorSnapshot& actor : world_.actors()) {
    workers += actor.kind == sim::AntKind::Worker ? 1 : 0;
  }
  return workers >= 12;
}

bool Session::set_focus(const sim::Focus focus) {
  if (!focus_available()) return false;
  world_.set_focus(focus);
  return true;
}

} // namespace ant::game
