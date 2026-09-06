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

  [[nodiscard]] const Camera2D& camera() const { return camera_; }
  [[nodiscard]] Rectangle viewport() const { return viewport_; }
  [[nodiscard]] float zoom() const { return camera_.zoom; }
  [[nodiscard]] bool contains(Vector2 screen_position) const;
  [[nodiscard]] sim::GridPos screen_to_cell(Vector2 screen_position) const;

private:
  void clamp_target();

  Camera2D camera_{};
  Rectangle viewport_{};
};

} // namespace ant::presentation
