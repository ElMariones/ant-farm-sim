#include "app/save_coordinator.hpp"
#include "app/window_size.hpp"
#include "game/prestige.hpp"
#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "game/view.hpp"
#include "persistence/content_loader.hpp"
#include "persistence/platform_paths.hpp"
#include "persistence/profile_codec.hpp"
#include "persistence/save_service.hpp"
#include "presentation/renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <raylib.h>

namespace {

struct Options {
  std::uint64_t seed{42};
  int width{1440};
  int height{900};
  ant::sim::Tick fast_forward{};
  float zoom{3.0F};
  std::optional<std::string> screenshot;
  int screenshot_delay_frames{12};
  int target_fps{60};
  bool exit_after_screenshot{};
  bool display_metrics{};
  bool start_paused{};
  bool open_legacy_panel{};
  std::optional<std::filesystem::path> save_directory;
  std::optional<std::filesystem::path> config_path;
};

std::filesystem::path resolve_config(const Options& options) {
  if (options.config_path) return *options.config_path;
  const std::filesystem::path beside_binary =
      std::filesystem::path("config") / "progression.json";
  if (std::filesystem::exists(beside_binary)) return beside_binary;
  return std::filesystem::path(ANT_SOURCE_CONFIG_DIR) / "progression.json";
}

ant::game::ProgressionConfig load_content(const Options& options) {
  const std::filesystem::path path = resolve_config(options);
  try {
    return ant::persistence::load_progression(path);
  } catch (const std::exception& error) {
    std::cerr << "ant_farm: using built-in balance (" << error.what() << ")\n";
    return ant::game::canonical_progression();
  }
}

Options parse_options(const int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: ant_farm [--seed N] [--width N] [--height N] [--fast-forward N] "
                   "[--zoom N] [--start-paused] [--screenshot PATH] "
                   "[--exit-after-screenshot] [--fps N] [--save-dir PATH] "
                   "[--config PATH] [--legacy-panel]\n";
      std::exit(0);
    }
    if (argument == "--exit-after-screenshot") {
      options.exit_after_screenshot = true;
      continue;
    }
    if (argument == "--display-metrics") {
      options.display_metrics = true;
      continue;
    }
    if (argument == "--start-paused") {
      options.start_paused = true;
      continue;
    }
    if (argument == "--legacy-panel") {
      options.open_legacy_panel = true;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::invalid_argument("incomplete argument: " + argument);
    }
    const std::string value = argv[++index];
    if (argument == "--seed") {
      options.seed = std::stoull(value);
    } else if (argument == "--width") {
      options.width = std::stoi(value);
    } else if (argument == "--height") {
      options.height = std::stoi(value);
    } else if (argument == "--fast-forward") {
      options.fast_forward = std::stoull(value);
    } else if (argument == "--zoom") {
      options.zoom = std::stof(value);
    } else if (argument == "--screenshot") {
      options.screenshot = value;
    } else if (argument == "--screenshot-delay") {
      options.screenshot_delay_frames = std::stoi(value);
    } else if (argument == "--fps") {
      options.target_fps = std::stoi(value);
    } else if (argument == "--save-dir") {
      options.save_directory = std::filesystem::path(value);
    } else if (argument == "--config") {
      options.config_path = std::filesystem::path(value);
    } else {
      throw std::invalid_argument("unknown argument: " + argument);
    }
  }
  options.width = std::max(options.width, 1024);
  options.height = std::max(options.height, 640);
  options.target_fps = std::clamp(options.target_fps, 30, 1000);
  return options;
}

} // namespace

namespace {

// Everything outside the run itself that a save has to carry forward.
struct ProfileIdentity {
  std::string profile_id;
  std::string run_id;
  ant::game::MetaSnapshot meta;
  ant::game::SettingsSnapshot settings;
  std::uint64_t revision{};
  std::optional<ant::game::FlightReceipt> last_flight_receipt;
};

ant::game::LegacyView legacy_view(const ProfileIdentity& identity, const bool between_runs) {
  ant::game::LegacyView legacy;
  legacy.between_runs = between_runs;
  legacy.wallet = identity.meta.legacy_wallet;
  legacy.vigor_tier = identity.meta.vigor_tier;
  legacy.industry_tier = identity.meta.industry_tier;
  legacy.vigor_cost = ant::game::next_trait_cost(identity.meta, ant::game::TraitBranch::Vigor);
  legacy.industry_cost =
      ant::game::next_trait_cost(identity.meta, ant::game::TraitBranch::Industry);
  legacy.generation = identity.meta.generation;
  legacy.successful_flights = identity.meta.successful_flights;
  return legacy;
}

ant::game::ProfileSnapshot build_candidate(const ProfileIdentity& identity,
                                           const ant::game::Session& session, const int speed,
                                           const bool between_runs) {
  ant::game::ProfileSnapshot candidate;
  candidate.content_version = session.progression().content_version;
  candidate.profile_id = identity.profile_id;
  candidate.meta = identity.meta;
  candidate.settings = identity.settings;
  candidate.settings.preferred_speed = speed;
  candidate.last_flight_receipt = identity.last_flight_receipt;
  candidate.phase = between_runs ? ant::game::ProfilePhase::BetweenRuns
                                 : ant::game::ProfilePhase::ActiveRun;
  if (!between_runs) candidate.run = session.snapshot(identity.run_id);
  return candidate;
}

// Adopts a profile the save service has just committed as the new authoritative view.
void adopt(ProfileIdentity& identity, const ant::game::ProfileSnapshot& profile) {
  identity.profile_id = profile.profile_id;
  identity.meta = profile.meta;
  identity.settings = profile.settings;
  identity.revision = profile.revision;
  identity.last_flight_receipt = profile.last_flight_receipt;
  if (profile.run) identity.run_id = profile.run->run_id;
}

} // namespace

int main(const int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    const ant::game::ProgressionConfig content = load_content(options);
    const std::filesystem::path save_directory =
        options.save_directory.value_or(ant::persistence::default_save_directory());

    ant::app::SaveCoordinator saves;
    std::unique_ptr<ant::persistence::SaveService> save_service;
    try {
      save_service = std::make_unique<ant::persistence::SaveService>(save_directory);
    } catch (const std::exception& error) {
      saves.set_status(std::string("Saving disabled: ") + error.what());
      std::cerr << "ant_farm: " << error.what() << '\n';
    }

    std::optional<ant::game::Session> session;
    ProfileIdentity identity;
    std::optional<ant::game::ProfileSnapshot> recovery_backup;
    bool recovery_prompt = false;
    bool between_runs = false;
    int speed = 1;

    if (save_service) {
      const ant::persistence::LoadResult loaded = save_service->load();
      switch (loaded.state) {
      case ant::persistence::LoadState::Loaded:
        adopt(identity, *loaded.profile);
        speed = loaded.profile->settings.preferred_speed;
        if (loaded.profile->run) {
          session.emplace(ant::game::Session::restore(*loaded.profile->run));
          saves.set_status("Colony resumed (revision " + std::to_string(loaded.profile->revision) +
                           ")");
        } else {
          // Saved between colonies: the shop is the resume point, and the world behind it is only
          // a backdrop that is never stepped or written.
          between_runs = true;
          session.emplace(options.seed, content, ant::game::trait_modifiers(loaded.profile->meta));
          saves.set_status("Choose permanent traits, then found the next colony");
        }
        break;
      case ant::persistence::LoadState::RecoveryAvailable:
        recovery_prompt = true;
        recovery_backup = loaded.backup;
        saves.set_status(loaded.error);
        break;
      case ant::persistence::LoadState::Invalid:
        // Nothing safe to restore; the unreadable files stay on disk untouched.
        recovery_prompt = true;
        saves.set_status(loaded.error);
        break;
      case ant::persistence::LoadState::NewProfile:
        break;
      }
    }

    if (!session) {
      session.emplace(options.seed, content);
      session->step_ticks(options.fast_forward);
      identity.profile_id = ant::persistence::make_new_profile(*session).profile_id;
      identity.run_id = session->snapshot().run_id;
    }

    ant::app::WindowSize initial_size{options.width, options.height};
#if defined(__APPLE__)
    initial_size = ant::app::recommended_initial_window_size(initial_size);
#endif
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(initial_size.width, initial_size.height, "Ant Farm Sim — Living Colony");
    SetWindowMinSize(1024, 640);
    const int monitor = GetCurrentMonitor();
    SetTargetFPS(options.target_fps);
    if (options.display_metrics) {
      const Vector2 dpi = GetWindowScaleDPI();
      std::cout << "screen=" << GetScreenWidth() << 'x' << GetScreenHeight()
                << " render=" << GetRenderWidth() << 'x' << GetRenderHeight()
                << " monitor=" << GetMonitorWidth(monitor) << 'x' << GetMonitorHeight(monitor)
                << " dpi=" << dpi.x << 'x' << dpi.y << '\n';
    }

    ant::presentation::Renderer renderer;
    renderer.set_zoom(options.zoom);
    if (options.open_legacy_panel) renderer.open_legacy_panel();
    bool paused = options.start_paused || recovery_prompt || between_runs;
    double accumulator = 0.0;
    bool simulation_limited = false;
    int rendered_frames = 0;
    bool screenshot_taken = false;

    while (!WindowShouldClose()) {
      const float raw_delta = GetFrameTime();
      if (options.display_metrics) {
        SetWindowTitle(TextFormat("Ant Farm Sim — mouse %d,%d — %dx%d", GetMouseX(), GetMouseY(),
                                  GetScreenWidth(), GetScreenHeight()));
      }
      renderer.set_status_line(saves.status());
      renderer.set_recovery_prompt(recovery_prompt);
      renderer.update_input(raw_delta);

      const auto commit = [&](const ant::app::SaveTrigger trigger) {
        if (!save_service || recovery_prompt) return;
        const ant::persistence::SaveResult result =
            save_service->commit(build_candidate(identity, *session, speed, between_runs));
        if (result.committed) {
          adopt(identity, *result.profile);
          saves.record_success(trigger, result.profile->revision, result.durability_uncertain);
        } else {
          saves.record_failure(trigger, result.error);
        }
      };

      // One durable progression action at a time: each builds a candidate from the committed
      // profile, and the live session only changes once the commit is known to have succeeded.
      const auto commit_transaction = [&](const ant::game::TransactionResult& prepared,
                                          const char* success) -> bool {
        if (!prepared.accepted) {
          saves.set_status(prepared.error);
          return false;
        }
        const ant::persistence::SaveResult result = save_service->commit(*prepared.candidate);
        if (!result.committed) {
          saves.set_status(std::string("Not saved, nothing changed: ") + result.error);
          return false;
        }
        adopt(identity, *result.profile);
        saves.set_status(success);
        return true;
      };

      if (recovery_prompt) {
        if (renderer.recover_requested() && recovery_backup && save_service) {
          const ant::persistence::SaveResult restored =
              save_service->recover_backup(*recovery_backup);
          if (restored.committed) {
            adopt(identity, *restored.profile);
            speed = restored.profile->settings.preferred_speed;
            between_runs = !restored.profile->run.has_value();
            if (restored.profile->run) {
              session.emplace(ant::game::Session::restore(*restored.profile->run));
            } else {
              session.emplace(options.seed, content,
                              ant::game::trait_modifiers(restored.profile->meta));
            }
            recovery_prompt = false;
            recovery_backup.reset();
            saves.set_status("Restored revision " + std::to_string(restored.profile->revision));
          } else {
            saves.set_status("Could not restore the previous revision: " + restored.error);
          }
        } else if (renderer.new_colony_requested() && save_service) {
          session.emplace(options.seed, content);
          identity.profile_id = ant::persistence::make_new_profile(*session).profile_id;
          identity.run_id = session->snapshot().run_id;
          identity.meta = {};
          identity.settings = {};
          speed = 1;
          // Retains the unreadable file for diagnosis instead of overwriting it.
          const ant::persistence::SaveResult fresh =
              save_service->replace_unreadable(build_candidate(identity, *session, speed, false));
          if (fresh.committed) {
            adopt(identity, *fresh.profile);
            recovery_prompt = false;
            between_runs = false;
            recovery_backup.reset();
            saves.set_status("Started a new colony; the damaged file was kept for diagnosis");
          } else {
            saves.set_status("Could not start a new colony: " + fresh.error);
          }
        }
      }

      if (renderer.toggle_pause_requested()) {
        paused = !paused;
      }
      if (renderer.speed_requested().has_value()) {
        speed = *renderer.speed_requested();
      }
      if (!recovery_prompt) {
        if (const auto upgrade = renderer.upgrade_requested()) {
          const ant::game::CommandResult bought = session->buy_upgrade(*upgrade);
          saves.set_status(bought.accepted ? session->last_command_message()
                                           : ant::game::rejection_reason(bought.rejection));
        }
        if (const auto focus = renderer.focus_requested()) {
          const ant::game::CommandResult changed = session->set_focus_command(*focus);
          saves.set_status(changed.accepted ? session->last_command_message()
                                            : ant::game::rejection_reason(changed.rejection));
        }
      }
      if (renderer.save_requested()) saves.request_manual();

      if (save_service && !recovery_prompt) {
        if (renderer.flight_requested() && !between_runs) {
          ant::game::ProfileSnapshot current = build_candidate(identity, *session, speed, false);
          current.revision = identity.revision;
          const auto prepared =
              ant::game::prepare_flight(current, *session, identity.run_id, identity.revision);
          if (commit_transaction(prepared, "The flight succeeded. Genetic Legacy is yours.")) {
            between_runs = true;
            paused = true;
          }
        } else if (const auto branch = renderer.trait_requested(); branch && between_runs) {
          ant::game::ProfileSnapshot current = build_candidate(identity, *session, speed, true);
          current.revision = identity.revision;
          const auto prepared =
              ant::game::prepare_trait_purchase(current, *branch, identity.revision);
          static_cast<void>(commit_transaction(
              prepared, TextFormat("%s improved permanently", ant::game::trait_branch_name(*branch))));
        } else if (renderer.new_run_requested() && between_runs) {
          ant::game::ProfileSnapshot current = build_candidate(identity, *session, speed, true);
          current.revision = identity.revision;
          const auto prepared =
              ant::game::prepare_new_run(current, options.seed + identity.meta.generation, content,
                                         identity.revision);
          if (prepared.accepted && save_service) {
            const ant::persistence::SaveResult result = save_service->commit(*prepared.candidate);
            if (result.committed) {
              adopt(identity, *result.profile);
              session.emplace(ant::game::Session::restore(*result.profile->run));
              between_runs = false;
              paused = false;
              saves.set_status("A new colony is founded");
            } else {
              saves.set_status(std::string("Not saved, nothing changed: ") + result.error);
            }
          } else if (!prepared.accepted) {
            saves.set_status(prepared.error);
          }
        }
      }
      renderer.clear_requests();

      // Real seconds decide only when to write; they never advance the colony.
      const ant::app::SaveTrigger due =
          saves.poll(static_cast<double>(raw_delta), save_service != nullptr && !recovery_prompt);
      if (due != ant::app::SaveTrigger::None) commit(due);

      simulation_limited = false;
      if (!paused && !recovery_prompt && !between_runs) {
        if (raw_delta > 0.5F) {
          accumulator = 0.0;
          simulation_limited = true;
        } else {
          accumulator += static_cast<double>(std::min(raw_delta, 0.25F)) * speed;
          // Budget enough whole ticks for the selected speed to be real at a low frame rate;
          // 20x at 30 Hz needs 13 ticks per frame. Speed still changes the number of whole ticks,
          // never the size of one.
          const double accumulator_cap = std::max(0.5, 0.05 * speed * 2.0);
          const int step_cap = std::max(8, speed * 2);
          if (accumulator > accumulator_cap) {
            accumulator = accumulator_cap;
            simulation_limited = true;
          }
          int steps = 0;
          while (accumulator >= 0.05 && steps < step_cap) {
            session->step();
            accumulator -= 0.05;
            ++steps;
          }
          if (accumulator >= 0.05) {
            simulation_limited = true;
          }
        }
      }

      ant::game::GameView view = ant::game::make_view(*session);
      const ant::game::FlightPreview flight = view.legacy.flight;
      view.legacy = legacy_view(identity, between_runs);
      view.legacy.flight = flight;
      BeginDrawing();
      renderer.draw(view, accumulator / 0.05, paused, speed, simulation_limited);
      EndDrawing();

      ++rendered_frames;
      if (options.screenshot.has_value() && !screenshot_taken &&
          rendered_frames >= options.screenshot_delay_frames) {
        TakeScreenshot(options.screenshot->c_str());
        screenshot_taken = true;
        if (options.exit_after_screenshot) {
          break;
        }
      }
    }
    CloseWindow();

    // Orderly exit save, after the window is down so a slow write cannot stall the last frame.
    if (save_service && !recovery_prompt) {
      const ant::persistence::SaveResult result =
          save_service->commit(build_candidate(identity, *session, speed, between_runs));
      if (!result.committed) {
        std::cerr << "ant_farm: exit save failed: " << result.error << '\n';
        return 1;
      }
      std::cout << "saved revision " << result.profile->revision << " to "
                << save_service->directory().string() << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ant_farm: " << error.what() << '\n';
    return 1;
  }
}
