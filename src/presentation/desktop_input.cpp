#include "presentation/desktop_input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>

namespace ant::presentation {

DesktopInput* DesktopInput::active_ = nullptr;

DesktopInput::DesktopInput() : window_(glfwGetCurrentContext()) {
  if (!window_ || active_) throw std::logic_error("desktop input needs one initialized game window");
  active_ = this;
  previous_button_ = glfwSetMouseButtonCallback(window_, button);
  previous_cursor_ = glfwSetCursorPosCallback(window_, cursor);
  previous_key_ = glfwSetKeyCallback(window_, key);
}
DesktopInput::~DesktopInput() {
  if (glfwGetCurrentContext() == window_) {
    glfwSetMouseButtonCallback(window_, previous_button_);
    glfwSetCursorPosCallback(window_, previous_cursor_);
    glfwSetKeyCallback(window_, previous_key_);
  }
  active_ = nullptr;
}
void DesktopInput::move(const Point point) {
  frame_.pointer = point;
  if (frame_.down) {
    const float dx = point.x - frame_.origin.x, dy = point.y - frame_.origin.y;
    frame_.travel_squared = std::max(frame_.travel_squared, dx * dx + dy * dy);
  }
}
void DesktopInput::button(GLFWwindow* window, const int button_id, const int action, const int mods) {
  auto& self = *active_;
  if (self.previous_button_) self.previous_button_(window, button_id, action, mods);
  if (button_id != GLFW_MOUSE_BUTTON_LEFT) return;
  double x{}, y{};
  glfwGetCursorPos(window, &x, &y);
  self.move({static_cast<float>(x), static_cast<float>(y)});
  if (action == GLFW_PRESS) {
    self.frame_.pressed = true;
    self.frame_.down = true;
    self.frame_.released = false;
    self.frame_.origin = self.frame_.pointer;
    self.frame_.travel_squared = 0;
  } else if (action == GLFW_RELEASE) {
    self.frame_.released = true;
    self.frame_.down = false;
  }
}
void DesktopInput::cursor(GLFWwindow* window, const double x, const double y) {
  auto& self = *active_;
  if (self.previous_cursor_) self.previous_cursor_(window, x, y);
  self.move({static_cast<float>(x), static_cast<float>(y)});
}
void DesktopInput::key(GLFWwindow* window, const int code, const int scancode,
                        const int action, const int mods) {
  auto& self = *active_;
  if (self.previous_key_) self.previous_key_(window, code, scancode, action, mods);
  if (action == GLFW_PRESS && code >= 0 && code < static_cast<int>(self.frame_.keys.size())) {
    self.frame_.keys[static_cast<std::size_t>(code)] = true;
    if (code == GLFW_KEY_S && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)))
      self.frame_.save_chord = true;
  }
}
DesktopFrame DesktopInput::take_frame() {
  const DesktopFrame result = frame_;
  frame_.keys.fill(false);
  frame_.save_chord = false;
  frame_.pressed = false;
  frame_.released = false;
  return result;
}

} // namespace ant::presentation
