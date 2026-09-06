#include "app/window_size.hpp"
#include "game/session.hpp"
#include "game/view.hpp"
#include "presentation/renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
};

Options parse_options(const int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help") {
      std::cout << "Usage: ant_farm [--seed N] [--width N] [--height N] [--fast-forward N] "
                   "[--zoom N] [--start-paused] [--screenshot PATH] "
                   "[--exit-after-screenshot] [--fps N]\n";
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

int main(const int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    ant::game::Session session(options.seed);
    session.step_ticks(options.fast_forward);

    ant::app::WindowSize initial_size{options.width, options.height};
#if defined(__APPLE__)
    initial_size = ant::app::recommended_initial_window_size(initial_size);
#endif
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(initial_size.width, initial_size.height, "Ant Farm Sim — M1 Watchable Slice");
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
    bool paused = options.start_paused;
    int speed = 1;
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
      renderer.update_input(raw_delta);
      if (renderer.toggle_pause_requested()) {
        paused = !paused;
      }
      if (renderer.speed_requested().has_value()) {
        speed = *renderer.speed_requested();
      }
      renderer.clear_requests();

      simulation_limited = false;
      if (!paused) {
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
            session.step();
            accumulator -= 0.05;
            ++steps;
          }
          if (accumulator >= 0.05) {
            simulation_limited = true;
          }
        }
      }

      const ant::game::GameView view = ant::game::make_view(session);
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
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ant_farm: " << error.what() << '\n';
    return 1;
  }
}
