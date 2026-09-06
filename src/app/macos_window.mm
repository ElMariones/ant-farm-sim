#include "app/window_size.hpp"

#import <AppKit/AppKit.h>

#include <cmath>

namespace ant::app {

WindowSize recommended_initial_window_size(const WindowSize requested) {
  @autoreleasepool {
    NSScreen* screen = [NSScreen mainScreen];
    if (screen == nil) {
      return requested;
    }
    const NSRect visible_frame = [screen visibleFrame];
    const WindowSize available{static_cast<int>(std::floor(visible_frame.size.width)),
                               static_cast<int>(std::floor(visible_frame.size.height))};
    // GLFW's requested dimensions describe the content area. Leave room for the native title bar.
    return clamp_window_size(requested, available, 36);
  }
}

} // namespace ant::app
