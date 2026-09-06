#include "app/window_size.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("window size stays inside the visible desktop frame") {
  const ant::app::WindowSize requested{1440, 900};
  const ant::app::WindowSize available{1512, 871};

  const ant::app::WindowSize fitted = ant::app::clamp_window_size(requested, available, 36);

  CHECK(fitted.width == 1440);
  CHECK(fitted.height == 835);
}

TEST_CASE("window size respects the supported minimum") {
  const ant::app::WindowSize requested{800, 480};
  const ant::app::WindowSize available{1512, 871};

  const ant::app::WindowSize fitted = ant::app::clamp_window_size(requested, available, 36);

  CHECK(fitted.width == 1024);
  CHECK(fitted.height == 640);
}
