#include "app/save_coordinator.hpp"
#include "app/window_size.hpp"
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
                   "[--config PATH]\n";
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
};

ant::game::ProfileSnapshot build_candidate(const ProfileIdentity& identity,
                                           const ant::game::Session& session, const int speed) {
  ant::game::ProfileSnapshot candidate;
  candidate.content_version = session.progression().content_version;
  candidate.profile_id = identity.profile_id;
  candidate.phase = ant::game::ProfilePhase::ActiveRun;
  candidate.meta = identity.meta;
  candidate.settings = identity.settings;
  candidate.settings.preferred_speed = speed;
  candidate.run = session.snapshot(identity.run_id);
  return candidate;
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
    int speed = 1;

    if (save_service) {
      const ant::persistence::LoadResult loaded = save_service->load();
      switch (loaded.state) {
      case ant::persistence::LoadState::Loaded:
        if (loaded.profile->run) {
          session.emplace(ant::game::Session::restore(*loaded.profile->run));
          identity = {loaded.profile->profile_id, loaded.profile->run->run_id, loaded.profile->meta,
                      loaded.profile->settings};
          speed = loaded.profile->settings.preferred_speed;
          saves.set_status("Colony resumed (revision " + std::to_string(loaded.profile->revision) +
                           ")");
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
    bool paused = options.start_paused || recovery_prompt;
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
            save_service->commit(build_candidate(identity, *session, speed));
        if (result.committed) {
          saves.record_success(trigger, result.profile->revision, result.durability_uncertain);
        } else {
          saves.record_failure(trigger, result.error);
        }
      };

      if (recovery_prompt) {
        if (renderer.recover_requested() && recovery_backup && save_service) {
          const ant::persistence::SaveResult restored =
              save_service->recover_backup(*recovery_backup);
          if (restored.committed && restored.profile->run) {
            session.emplace(ant::game::Session::restore(*restored.profile->run));
            identity = {restored.profile->profile_id, restored.profile->run->run_id,
                        restored.profile->meta, restored.profile->settings};
            speed = restored.profile->settings.preferred_speed;
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
              save_service->replace_unreadable(build_candidate(identity, *session, speed));
          if (fresh.committed) {
            recovery_prompt = false;
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
      renderer.clear_requests();

      // Real seconds decide only when to write; they never advance the colony.
      const ant::app::SaveTrigger due =
          saves.poll(static_cast<double>(raw_delta), save_service != nullptr && !recovery_prompt);
      if (due != ant::app::SaveTrigger::None) commit(due);

      simulation_limited = false;
      if (!paused && !recovery_prompt) {
        if (raw_delta > 0.5F) {
          accumulator = 0.0;
          simulation_limited = true;
        } else {
          accumulator += static_cast<double>(std::min(raw_delta, 0.25F)) * speed;
          if (accumulator > 0.5) {
            accumulator = 0.5;
            simulation_limited = true;
          }
          int steps = 0;
          while (accumulator >= 0.05 && steps < 8) {
            session->step();
            accumulator -= 0.05;
            ++steps;
          }
          if (accumulator >= 0.05) {
            simulation_limited = true;
          }
        }
      }

      const ant::game::GameView view = ant::game::make_view(*session);
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
          save_service->commit(build_candidate(identity, *session, speed));
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
