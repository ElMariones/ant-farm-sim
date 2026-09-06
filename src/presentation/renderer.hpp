#pragma once

#include "game/view.hpp"
#include "presentation/camera.hpp"

#include <cstdint>
#include <optional>

namespace ant::presentation {

class Renderer {
public:
  Renderer();
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void update_input(float delta_seconds);
  void draw(const game::GameView& view, double interpolation_alpha, bool paused, int speed,
            bool simulation_limited);

  [[nodiscard]] bool toggle_pause_requested() const { return toggle_pause_requested_; }
  [[nodiscard]] std::optional<int> speed_requested() const { return speed_requested_; }
  void clear_requests();
  void set_zoom(float zoom) { camera_.set_zoom(zoom); }

private:
  void draw_world(const game::GameView& view, double interpolation_alpha);
  void draw_ant(const sim::ActorSnapshot& actor, double interpolation_alpha, float zoom,
                bool selected);
  void draw_interface(const game::GameView& view, bool paused, int speed, bool simulation_limited);
  void update_selection(const game::GameView& view);
  void draw_text(const char* text, int x, int y, int size = 20,
                 Color color = Color{37, 45, 40, 255}) const;
  void draw_button(Rectangle bounds, const char* label, bool active) const;

  CameraController camera_;
  Font font_{};
  bool font_loaded_{};
  std::optional<sim::EntityId> selected_id_;
  bool inspector_open_{true};
  bool toggle_pause_requested_{};
  std::optional<int> speed_requested_;
};

} // namespace ant::presentation
