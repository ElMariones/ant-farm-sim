#include "game/session.hpp"

namespace ant::game {

Session::Session(const std::uint64_t seed) : world_(seed) {}

void Session::step() { world_.step(); }

void Session::step_ticks(const sim::Tick count) { world_.run_ticks(count); }

} // namespace ant::game
