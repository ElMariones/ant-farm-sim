#include "presentation/camera.hpp"

#include "sim/grid.hpp"

#include <algorithm>
#include <cmath>

namespace ant::presentation {

CameraController::CameraController() {
  camera_.target = {sim::Grid::kWidth * 0.5F, sim::Grid::kHeight * 0.5F};
  camera_.offset = {0.0F, 0.0F};
  camera_.rotation = 0.0F;
  camera_.zoom = logical_zoom_;
}

void CameraController::apply_scale() {
  camera_.zoom = logical_zoom_ * backing_scale_;
  camera_.offset = {(viewport_.x + viewport_.width * 0.5F) * backing_scale_,
                    (viewport_.y + viewport_.height * 0.5F) * backing_scale_};
}

void CameraController::layout(const int screen_width, const int screen_height,
                              const bool inspector_open) {
  const auto bounds = interface_layout(screen_width, screen_height, inspector_open).world;
  viewport_ = {bounds.x, bounds.y, bounds.width, bounds.height};
  const Vector2 dpi = GetWindowScaleDPI();
  backing_scale_ = std::max(1.0F, std::max(dpi.x, dpi.y));
  minimum_zoom_ = std::max(viewport_.width / static_cast<float>(sim::Grid::kWidth),
                           viewport_.height / static_cast<float>(sim::Grid::kHeight));
  logical_zoom_ = std::clamp(logical_zoom_, minimum_zoom_, 12.0F);
  apply_scale();
  clamp_target();
}

void CameraController::update(const float delta_seconds, const bool keyboard_enabled,
                              const bool pointer_owned) {
  const Vector2 mouse = GetMousePosition();
  const bool pan_button_down =
      IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
  if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    pan_captured_ = pointer_owned;
  }
  // A pan belongs to whoever the press landed on. Releasing the button is the only thing that ends
  // it, so dragging out over the inspector or off the window does not drop the gesture.
  if (!pan_button_down || !keyboard_enabled) {
    pan_captured_ = false;
  }
  if (pan_captured_) {
    const Vector2 delta = GetMouseDelta();
    camera_.target.x -= delta.x / logical_zoom_;
    camera_.target.y -= delta.y / logical_zoom_;
  }

  if (!keyboard_enabled) {
    clamp_target();
    return;
  }

  Vector2 keyboard{};
  const bool save_chord = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                          IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
  if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
    keyboard.x -= 1.0F;
  }
  if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
    keyboard.x += 1.0F;
  }
  if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
    keyboard.y -= 1.0F;
  }
  if ((!save_chord && IsKeyDown(KEY_S)) || IsKeyDown(KEY_DOWN)) {
    keyboard.y += 1.0F;
  }
  camera_.target.x += keyboard.x * 180.0F * delta_seconds / logical_zoom_;
  camera_.target.y += keyboard.y * 180.0F * delta_seconds / logical_zoom_;

  float wheel = pointer_owned ? GetMouseWheelMove() : 0.0F;
  if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
    wheel += 1.0F;
  }
  if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
    wheel -= 1.0F;
  }
  if (wheel != 0.0F) {
    const Vector2 logical_pivot =
        pointer_owned ? mouse
                      : Vector2{viewport_.x + viewport_.width * 0.5F,
                                viewport_.y + viewport_.height * 0.5F};
    const Vector2 pivot{logical_pivot.x * backing_scale_, logical_pivot.y * backing_scale_};
    const Vector2 before = GetScreenToWorld2D(pivot, camera_);
    logical_zoom_ = std::clamp(logical_zoom_ * (1.0F + wheel * 0.12F), minimum_zoom_, 12.0F);
    apply_scale();
    const Vector2 after = GetScreenToWorld2D(pivot, camera_);
    camera_.target.x += before.x - after.x;
    camera_.target.y += before.y - after.y;
  }
  clamp_target();
}

void CameraController::set_zoom(const float zoom) {
  logical_zoom_ = std::clamp(zoom, minimum_zoom_, 12.0F);
  apply_scale();
  clamp_target();
}

bool CameraController::contains(const Vector2 screen_position) const {
  return CheckCollisionPointRec(screen_position, viewport_);
}

Vector2 CameraController::screen_to_world(const Vector2 position) const {
  const ViewTransform transform{{camera_.target.x, camera_.target.y},
                                {camera_.offset.x, camera_.offset.y}, camera_.zoom, backing_scale_};
  const Point world = transform.to_world({position.x, position.y});
  return {world.x, world.y};
}

Vector2 CameraController::world_to_screen(const Vector2 position) const {
  const ViewTransform transform{{camera_.target.x, camera_.target.y},
                                {camera_.offset.x, camera_.offset.y}, camera_.zoom, backing_scale_};
  const Point screen = transform.to_screen({position.x, position.y});
  return {screen.x, screen.y};
}

sim::GridPos CameraController::screen_to_cell(const Vector2 screen_position) const {
  const Vector2 world = screen_to_world(screen_position);
  return {static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))};
}

void CameraController::focus(const sim::GridPos cell) {
  camera_.target = {static_cast<float>(cell.x) + 0.5F, static_cast<float>(cell.y) + 0.5F};
  clamp_target();
}

void CameraController::clamp_target() {
  const float half_width = viewport_.width > 0.0F ? viewport_.width / (2.0F * logical_zoom_) : 0.0F;
  const float half_height =
      viewport_.height > 0.0F ? viewport_.height / (2.0F * logical_zoom_) : 0.0F;
  if (half_width * 2.0F >= sim::Grid::kWidth) {
    camera_.target.x = sim::Grid::kWidth * 0.5F;
  } else {
    camera_.target.x = std::clamp(camera_.target.x, half_width,
                                  static_cast<float>(sim::Grid::kWidth) - half_width);
  }
  if (half_height * 2.0F >= sim::Grid::kHeight) {
    camera_.target.y = sim::Grid::kHeight * 0.5F;
  } else {
    camera_.target.y = std::clamp(camera_.target.y, half_height,
                                  static_cast<float>(sim::Grid::kHeight) - half_height);
  }
}

} // namespace ant::presentation
