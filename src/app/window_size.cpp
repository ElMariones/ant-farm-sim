#include "app/window_size.hpp"

#include <algorithm>

namespace ant::app {

WindowSize clamp_window_size(const WindowSize requested, const WindowSize available_frame,
                             const int decoration_height) {
  const int usable_width = std::max(1024, available_frame.width);
  const int usable_height = std::max(640, available_frame.height - decoration_height);
  return {std::clamp(requested.width, 1024, usable_width),
          std::clamp(requested.height, 640, usable_height)};
}

} // namespace ant::app
