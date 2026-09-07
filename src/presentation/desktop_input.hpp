#pragma once

#include "presentation/interaction.hpp"

#include <array>

struct GLFWwindow;

namespace ant::presentation {

struct DesktopFrame {
  std::array<bool, 512> keys{};
  bool save_chord{};
  bool pressed{};
  bool released{};
  bool down{};
  Point origin;
  Point pointer;
  float travel_squared{};
};

// raylib's level comparisons can miss a press and release delivered by GLFW in one poll.
// Keep bounded edge latches while still chaining every event to raylib's original callbacks.
class DesktopInput {
public:
  DesktopInput();
  ~DesktopInput();
  DesktopInput(const DesktopInput&) = delete;
  DesktopInput& operator=(const DesktopInput&) = delete;
  [[nodiscard]] DesktopFrame take_frame();
private:
  static void button(GLFWwindow* window, int button, int action, int mods);
  static void cursor(GLFWwindow* window, double x, double y);
  static void key(GLFWwindow* window, int key, int scancode, int action, int mods);
  void move(Point point);
  static DesktopInput* active_;
  GLFWwindow* window_{};
  void (*previous_button_)(GLFWwindow*, int, int, int){};
  void (*previous_cursor_)(GLFWwindow*, double, double){};
  void (*previous_key_)(GLFWwindow*, int, int, int, int){};
  DesktopFrame frame_;
};

} // namespace ant::presentation
