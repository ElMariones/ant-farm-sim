#include "game/session.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
  std::uint64_t seed{42};
  ant::sim::Tick ticks{1'000};
};

Options parse_options(const int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: ant_headless [--seed N] [--ticks N]\n";
      std::exit(0);
    }
    if ((argument == "--seed" || argument == "--ticks") && index + 1 < argc) {
      const auto value = std::stoull(argv[++index]);
      if (argument == "--seed") {
        options.seed = value;
      } else {
        options.ticks = value;
      }
      continue;
    }
    throw std::invalid_argument("unknown or incomplete argument: " + argument);
  }
  return options;
}

} // namespace

int main(const int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    ant::game::Session session(options.seed);
    session.step_ticks(options.ticks);
    const ant::sim::World& world = session.world();
    if (!world.invariant_holds()) {
      std::cerr << "simulation invariant failed\n";
      return 2;
    }
    std::size_t carrying_workers = 0;
    std::size_t workers = 0;
    for (const ant::sim::ActorSnapshot& actor : world.actors()) {
      if (actor.kind == ant::sim::AntKind::Worker) {
        ++workers;
        if (actor.cargo_amount > 0) ++carrying_workers;
      }
    }
    std::cout << "{\"seed\":" << options.seed << ",\"ticks\":" << world.tick() << ",\"hash\":\""
              << std::hex << std::setw(16) << std::setfill('0') << world.canonical_hash()
              << std::dec
              << "\",\"workers\":" << workers << ",\"brood\":" << world.brood().size()
              << ",\"roundTrips\":" << world.stats().completed_round_trips
              << ",\"carryingWorkers\":" << carrying_workers
              << ",\"carbohydrate\":" << world.stores().carbohydrate
              << ",\"protein\":" << world.stores().protein
              << ",\"delivered\":" << world.stats().delivered
              << ",\"excavated\":" << world.stats().cells_excavated
              << ",\"births\":" << world.stats().workers_born
              << ",\"deaths\":" << world.stats().deaths << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ant_headless: " << error.what() << '\n';
    return 1;
  }
}
