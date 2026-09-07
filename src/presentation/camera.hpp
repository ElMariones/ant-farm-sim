#pragma once

#include "sim/types.hpp"

#include <raylib.h>

namespace ant::presentation {

class CameraController {
public:
  CameraController();

  void layout(int screen_width, int screen_height, bool inspector_open);
  void update(float delta_seconds, bool input_enabled);
  void set_zoom(float zoom);
  // Centres the view on a cell, so the game opens looking at the colony rather than at subsoil.
  void focus(sim::GridPos cell);

  [[nodiscard]] const Camera2D& camera() const { return camera_; }
  [[nodiscard]] Rectangle viewport() const { return viewport_; }
  // Logical pixels per world cell, independent of the display's backing scale. The raylib camera
  // itself carries the scaled value.
  [[nodiscard]] float zoom() const { return logical_zoom_; }
  [[nodiscard]] bool contains(Vector2 screen_position) const;
  [[nodiscard]] sim::GridPos screen_to_cell(Vector2 screen_position) const;

private:
  void clamp_target();

  void apply_scale();

  Camera2D camera_{};
  Rectangle viewport_{};
  float logical_zoom_{3.0F};
  // BeginMode2D replaces raylib's high-DPI transform, so the world would otherwise draw at half
  // size on a Retina display while the interface drew at full size. The backing scale is folded
  // into the camera instead.
  float backing_scale_{1.0F};
  // Never zoom out past the point where the world stops covering the viewport, or the diorama
  // floats on the clear colour.
  float minimum_zoom_{1.5F};
};

} // namespace ant::presentation
