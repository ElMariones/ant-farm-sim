#include "presentation/ui_state.hpp"

#include <algorithm>
#include <cmath>

namespace ant::presentation {
namespace {

constexpr float kHoverRate = 14.0F;
constexpr float kPressRate = 26.0F;

} // namespace

bool UiRect::contains(const float point_x, const float point_y) const {
  return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
}

float approach(const float current, const float target, const float rate,
               const float delta_seconds) {
  if (!(delta_seconds > 0.0F) || !(rate > 0.0F)) return target;
  // 1 - e^-rt is the exact solution to an exponential approach, so the result does not depend on
  // how the elapsed time was chopped into frames.
  const float step = 1.0F - std::exp(-rate * std::min(delta_seconds, 0.25F));
  return current + (target - current) * step;
}

float ease(const float weight) {
  const float clamped = std::clamp(weight, 0.0F, 1.0F);
  return clamped * clamped * (3.0F - 2.0F * clamped);
}

void UiState::begin_frame(const float pointer_x, const float pointer_y, const bool down,
                          const bool pressed, const bool released, const float delta_seconds) {
  pointer_x_ = pointer_x;
  pointer_y_ = pointer_y;
  press_x_ = pointer_x;
  press_y_ = pointer_y;
  down_ = down;
  pressed_ = pressed;
  released_ = released;
  delta_seconds_ = delta_seconds;
  input_enabled_ = true;
  clip_.reset();
  pointer_over_interface_ = false;
  hovered_id_ = 0;
  // The capture has to survive the release frame, which is the frame a click is reported on.
  if (!down_ && !released_) captured_id_ = 0;
  for (Entry& entry : entries_) entry.seen = false;
}

UiState::Entry& UiState::entry_for(const std::uint32_t id) {
  for (Entry& entry : entries_) {
    if (entry.id == id) return entry;
  }
  entries_.push_back({id, 0.0F, 0.0F, false});
  return entries_.back();
}

WidgetVisual UiState::track(const std::uint32_t id, const UiRect bounds, const bool enabled) {
  Entry& entry = entry_for(id);
  entry.seen = true;

  const bool over = input_enabled_ && bounds.contains(pointer_x_, pointer_y_) &&
                    (!clip_ || clip_->contains(pointer_x_, pointer_y_));
  if (over) pointer_over_interface_ = true;

  WidgetVisual visual;
  visual.enabled = enabled;
  visual.hovered = over && enabled;
  if (visual.hovered) hovered_id_ = id;

  const bool press_over = input_enabled_ && bounds.contains(press_x_, press_y_) &&
                          (!clip_ || clip_->contains(press_x_, press_y_));
  if (enabled && press_over && pressed_) captured_id_ = id;
  visual.held = enabled && down_ && captured_id_ == id && over;
  // A click completes only where it began, so dragging off a control cancels it.
  visual.clicked = enabled && released_ && captured_id_ == id && over;
  if (visual.clicked) captured_id_ = 0;

  entry.hover = approach(entry.hover, visual.hovered ? 1.0F : 0.0F, kHoverRate, delta_seconds_);
  entry.press = approach(entry.press, visual.held ? 1.0F : 0.0F, kPressRate, delta_seconds_);
  visual.hover = ease(entry.hover);
  visual.press = ease(entry.press);
  return visual;
}

} // namespace ant::presentation
