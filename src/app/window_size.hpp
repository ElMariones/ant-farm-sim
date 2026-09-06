#pragma once

namespace ant::app {

struct WindowSize {
  int width{};
  int height{};
};

[[nodiscard]] WindowSize clamp_window_size(WindowSize requested, WindowSize available_frame,
                                           int decoration_height);

#if defined(__APPLE__)
[[nodiscard]] WindowSize recommended_initial_window_size(WindowSize requested);
#endif

} // namespace ant::app
