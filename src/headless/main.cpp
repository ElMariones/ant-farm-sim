#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "persistence/content_loader.hpp"
#include "persistence/profile_codec.hpp"
#include "persistence/save_service.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

struct Options {
  std::uint64_t seed{42};
  ant::sim::Tick ticks{1'000};
  std::optional<std::filesystem::path> save_directory;
  std::optional<std::filesystem::path> config_path;
  bool save{};
  bool resume{};
  bool verify_round_trip{};
};

Options parse_options(const int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: ant_headless [--seed N] [--ticks N] [--save-dir PATH] [--config PATH] "
                   "[--save] [--resume] [--verify-round-trip]\n";
      std::exit(0);
    }
    if (argument == "--save") {
      options.save = true;
      continue;
    }
    if (argument == "--resume") {
      options.resume = true;
      continue;
    }
    if (argument == "--verify-round-trip") {
      options.verify_round_trip = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("incomplete argument: " + argument);
    const std::string value = argv[++index];
    if (argument == "--seed") {
      options.seed = std::stoull(value);
    } else if (argument == "--ticks") {
      options.ticks = std::stoull(value);
    } else if (argument == "--save-dir") {
      options.save_directory = std::filesystem::path(value);
    } else if (argument == "--config") {
      options.config_path = std::filesystem::path(value);
    } else {
      throw std::invalid_argument("unknown argument: " + argument);
    }
  }
  if ((options.save || options.resume) && !options.save_directory) {
    throw std::invalid_argument("--save and --resume require --save-dir");
  }
  return options;
}

ant::game::ProgressionConfig load_content(const Options& options) {
  const std::filesystem::path path =
      options.config_path.value_or(std::filesystem::path(ANT_SOURCE_CONFIG_DIR) /
                                   "progression.json");
  return ant::persistence::load_progression(path);
}

void report(const std::uint64_t seed, const ant::game::Session& session) {
  const ant::sim::World& world = session.world();
  std::size_t carrying_workers = 0;
  std::size_t workers = 0;
  for (const ant::sim::ActorSnapshot& actor : world.actors()) {
    if (actor.kind == ant::sim::AntKind::Worker) {
      ++workers;
      if (actor.cargo_amount > 0) ++carrying_workers;
    }
  }
  std::cout << "{\"seed\":" << seed << ",\"ticks\":" << world.tick() << ",\"hash\":\"" << std::hex
            << std::setw(16) << std::setfill('0') << world.canonical_hash() << std::dec
            << "\",\"workers\":" << workers << ",\"brood\":" << world.brood().size()
            << ",\"roundTrips\":" << world.stats().completed_round_trips
            << ",\"carryingWorkers\":" << carrying_workers
            << ",\"carbohydrate\":" << world.stores().carbohydrate
            << ",\"protein\":" << world.stores().protein
            << ",\"delivered\":" << world.stats().delivered
            << ",\"excavated\":" << world.stats().cells_excavated
            << ",\"births\":" << world.stats().workers_born
            << ",\"deaths\":" << world.stats().deaths
            << ",\"productiveTicks\":" << world.stats().productive_worker_ticks
            << ",\"work\":" << session.work() << ",\"assisted\":"
            << (session.assisted() ? "true" : "false") << "}\n";
}

} // namespace

int main(const int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    const ant::game::ProgressionConfig content = load_content(options);

    std::unique_ptr<ant::persistence::SaveService> save_service;
    if (options.save_directory) {
      save_service = std::make_unique<ant::persistence::SaveService>(*options.save_directory);
    }

    std::optional<ant::game::Session> session;
    std::string profile_id;
    std::string run_id;
    if (options.resume && save_service) {
      const ant::persistence::LoadResult loaded = save_service->load();
      if (loaded.state != ant::persistence::LoadState::Loaded || !loaded.profile->run) {
        std::cerr << "ant_headless: nothing to resume: " << loaded.error << '\n';
        return 3;
      }
      session.emplace(ant::game::Session::restore(*loaded.profile->run));
      profile_id = loaded.profile->profile_id;
      run_id = loaded.profile->run->run_id;
    } else {
      session.emplace(options.seed, content);
    }

    session->step_ticks(options.ticks);
    if (!session->world().invariant_holds()) {
      std::cerr << "simulation invariant failed\n";
      return 2;
    }

    if (options.verify_round_trip) {
      // Prove that serialising and restoring leaves the run's future untouched.
      const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
          ant::persistence::encode_profile(ant::persistence::make_new_profile(*session)));
      if (!decoded.profile || !decoded.profile->run) {
        std::cerr << "ant_headless: snapshot did not survive its own codec: " << decoded.error
                  << '\n';
        return 4;
      }
      ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);
      if (restored.world().canonical_hash() != session->world().canonical_hash()) {
        std::cerr << "ant_headless: restored colony differs at the save tick\n";
        return 4;
      }
      constexpr ant::sim::Tick kContinuation = 2'000;
      restored.step_ticks(kContinuation);
      session->step_ticks(kContinuation);
      if (restored.world().canonical_hash() != session->world().canonical_hash()) {
        std::cerr << "ant_headless: restored colony diverged after " << kContinuation
                  << " further ticks\n";
        return 4;
      }
    }

    if (options.save && save_service) {
      ant::game::ProfileSnapshot candidate = ant::persistence::make_new_profile(*session);
      if (!profile_id.empty()) {
        candidate.profile_id = profile_id;
        candidate.run = session->snapshot(run_id);
      }
      const ant::persistence::SaveResult result = save_service->commit(std::move(candidate));
      if (!result.committed) {
        std::cerr << "ant_headless: save failed: " << result.error << '\n';
        return 5;
      }
      std::cerr << "saved revision " << result.profile->revision << '\n';
    }

    report(options.seed, *session);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ant_headless: " << error.what() << '\n';
    return 1;
  }
}
