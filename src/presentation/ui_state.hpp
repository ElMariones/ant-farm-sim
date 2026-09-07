#pragma once

#include <cstdint>
#include <vector>
#include <optional>

// Deliberately free of raylib so the interaction rules can be tested in the headless build.
namespace ant::presentation {

struct UiRect {
  float x{};
  float y{};
  float width{};
  float height{};

  [[nodiscard]] bool contains(float point_x, float point_y) const;
};

// How a widget should be drawn this frame. `hover` and `press` are eased 0..1 weights rather than
// booleans, so a caller can lift, tint or inset a control smoothly.
struct WidgetVisual {
  bool hovered{};
  // Pointer is over the widget whether or not it can be used, so a disabled control can still
  // explain itself on hover.
  bool over{};
  bool held{};
  bool clicked{};
  bool enabled{true};
  float hover{};
  float press{};
};

// Pointer state and per-widget hover/press animation for one frame. Widgets are identified by a
// stable id supplied by the caller, so a control keeps its animation across frames without the
// caller storing anything.
class UiState {
public:
  // `pressed` and `released` are edges from this frame, `down` is the level.
  void begin_frame(float pointer_x, float pointer_y, bool down, bool pressed, bool released,
                   float delta_seconds);
  // Registers an interactive region and reports how to draw it. A click is reported on release
  // over the same widget that was pressed, which is what a desktop control should do.
  [[nodiscard]] WidgetVisual track(std::uint32_t id, UiRect bounds, bool enabled = true);
  // Scene and clipping ownership are resolved before tracking controls. Hidden controls cannot
  // receive hover/press/release even if their unscrolled rectangle covers the pointer.
  void set_input_region(bool enabled, std::optional<UiRect> clip = {}) {
    input_enabled_ = enabled; clip_ = clip;
  }
  void set_press_origin(float x, float y) { press_x_ = x; press_y_ = y; }
  void cancel_capture() { captured_id_ = 0; }
  // True when the pointer is over any widget registered this frame, so the world can ignore it.
  [[nodiscard]] bool pointer_over_interface() const { return pointer_over_interface_; }
  [[nodiscard]] bool any_hovered() const { return hovered_id_ != 0; }
  [[nodiscard]] std::uint32_t hovered_id() const { return hovered_id_; }

private:
  struct Entry {
    std::uint32_t id{};
    float hover{};
    float press{};
    bool seen{};
  };

  [[nodiscard]] Entry& entry_for(std::uint32_t id);

  std::vector<Entry> entries_;
  float pointer_x_{};
  float pointer_y_{};
  float delta_seconds_{};
  float press_x_{};
  float press_y_{};
  bool input_enabled_{true};
  std::optional<UiRect> clip_;
  bool down_{};
  bool pressed_{};
  bool released_{};
  bool pointer_over_interface_{};
  std::uint32_t hovered_id_{};
  std::uint32_t captured_id_{};
};

// Framerate-independent approach toward a target. `rate` is the fraction of the remaining distance
// covered per second, so the motion looks the same at 30 and 144 Hz.
[[nodiscard]] float approach(float current, float target, float rate, float delta_seconds);

// Smoothstep, for easing a hover weight into a size or colour change.
[[nodiscard]] float ease(float weight);

} // namespace ant::presentation
