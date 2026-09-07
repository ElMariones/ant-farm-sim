#include "presentation/ui_state.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

using ant::presentation::UiRect;
using ant::presentation::UiState;

constexpr UiRect kButton{100.0F, 100.0F, 80.0F, 40.0F};
constexpr UiRect kOther{300.0F, 100.0F, 80.0F, 40.0F};
constexpr float kFrame = 1.0F / 60.0F;

} // namespace

TEST_CASE("a rectangle owns its top-left edge and not its bottom-right", "[ui]") {
  CHECK(kButton.contains(100.0F, 100.0F));
  CHECK(kButton.contains(179.9F, 139.9F));
  CHECK_FALSE(kButton.contains(180.0F, 120.0F));
  CHECK_FALSE(kButton.contains(120.0F, 140.0F));
  CHECK_FALSE(kButton.contains(99.9F, 120.0F));
}

TEST_CASE("hover follows the pointer and is reported per widget", "[ui]") {
  UiState ui;
  ui.begin_frame(140.0F, 120.0F, false, false, false, kFrame);
  const auto over = ui.track(1, kButton);
  const auto away = ui.track(2, kOther);

  CHECK(over.hovered);
  CHECK_FALSE(away.hovered);
  CHECK(ui.hovered_id() == 1);
  CHECK(ui.pointer_over_interface());
}

TEST_CASE("the pointer is only over the interface when it is over a widget", "[ui]") {
  UiState ui;
  ui.begin_frame(10.0F, 10.0F, false, false, false, kFrame);
  static_cast<void>(ui.track(1, kButton));
  CHECK_FALSE(ui.pointer_over_interface());
  CHECK_FALSE(ui.any_hovered());
}

TEST_CASE("a click completes only where it began", "[ui]") {
  UiState ui;

  SECTION("press and release on the same widget clicks once") {
    ui.begin_frame(140.0F, 120.0F, true, true, false, kFrame);
    const auto pressing = ui.track(1, kButton);
    CHECK(pressing.held);
    CHECK_FALSE(pressing.clicked);

    ui.begin_frame(140.0F, 120.0F, false, false, true, kFrame);
    CHECK(ui.track(1, kButton).clicked);

    // And not again on the following frame.
    ui.begin_frame(140.0F, 120.0F, false, false, false, kFrame);
    CHECK_FALSE(ui.track(1, kButton).clicked);
  }

  SECTION("dragging off the widget cancels the click") {
    ui.begin_frame(140.0F, 120.0F, true, true, false, kFrame);
    static_cast<void>(ui.track(1, kButton));

    ui.begin_frame(500.0F, 500.0F, false, false, true, kFrame);
    CHECK_FALSE(ui.track(1, kButton).clicked);
  }

  SECTION("releasing over a widget that was not pressed does not click it") {
    ui.begin_frame(140.0F, 120.0F, true, true, false, kFrame);
    static_cast<void>(ui.track(1, kButton));
    static_cast<void>(ui.track(2, kOther));

    ui.begin_frame(340.0F, 120.0F, false, false, true, kFrame);
    static_cast<void>(ui.track(1, kButton));
    CHECK_FALSE(ui.track(2, kOther).clicked);
  }
}

TEST_CASE("a disabled widget never hovers, holds or clicks", "[ui]") {
  UiState ui;
  ui.begin_frame(140.0F, 120.0F, true, true, false, kFrame);
  const auto pressing = ui.track(1, kButton, false);
  CHECK_FALSE(pressing.hovered);
  CHECK_FALSE(pressing.held);
  CHECK_FALSE(pressing.enabled);

  ui.begin_frame(140.0F, 120.0F, false, false, true, kFrame);
  CHECK_FALSE(ui.track(1, kButton, false).clicked);
  // It still blocks the world underneath, so a dead control is not a hole in the panel.
  CHECK(ui.pointer_over_interface());
}

TEST_CASE("hover weight eases in and out rather than snapping", "[ui]") {
  UiState ui;
  ui.begin_frame(140.0F, 120.0F, false, false, false, kFrame);
  const float first = ui.track(1, kButton).hover;
  CHECK(first > 0.0F);
  CHECK(first < 1.0F);

  float weight = first;
  for (int frame = 0; frame < 40; ++frame) {
    ui.begin_frame(140.0F, 120.0F, false, false, false, kFrame);
    const float next = ui.track(1, kButton).hover;
    CHECK(next >= weight - 1e-4F);
    weight = next;
  }
  CHECK(weight > 0.98F);

  // Leaving the widget eases back down.
  for (int frame = 0; frame < 40; ++frame) {
    ui.begin_frame(10.0F, 10.0F, false, false, false, kFrame);
    static_cast<void>(ui.track(1, kButton));
  }
  ui.begin_frame(10.0F, 10.0F, false, false, false, kFrame);
  CHECK(ui.track(1, kButton).hover < 0.02F);
}

TEST_CASE("hover animation does not depend on frame rate", "[ui]") {
  // The same elapsed time must produce the same weight whether it arrived in many small frames or
  // a few large ones, or a control would animate differently on a slower machine.
  UiState fast;
  for (int frame = 0; frame < 30; ++frame) {
    fast.begin_frame(140.0F, 120.0F, false, false, false, 1.0F / 120.0F);
    static_cast<void>(fast.track(1, kButton));
  }
  UiState slow;
  for (int frame = 0; frame < 5; ++frame) {
    slow.begin_frame(140.0F, 120.0F, false, false, false, 1.0F / 20.0F);
    static_cast<void>(slow.track(1, kButton));
  }

  fast.begin_frame(140.0F, 120.0F, false, false, false, 0.0F);
  slow.begin_frame(140.0F, 120.0F, false, false, false, 0.0F);
  const float fast_weight = fast.track(1, kButton).hover;
  const float slow_weight = slow.track(1, kButton).hover;
  CHECK(fast_weight == Catch::Approx(slow_weight).margin(0.02F));
}

TEST_CASE("a very long frame does not jump the animation past its target", "[ui]") {
  UiState ui;
  ui.begin_frame(140.0F, 120.0F, false, false, false, 10.0F);
  const float weight = ui.track(1, kButton).hover;
  CHECK(weight <= 1.0F);
  CHECK(weight >= 0.0F);
}

TEST_CASE("approach and ease behave at their boundaries", "[ui]") {
  CHECK(ant::presentation::approach(0.0F, 1.0F, 10.0F, 0.0F) == 1.0F);
  CHECK(ant::presentation::approach(0.5F, 0.5F, 10.0F, kFrame) == Catch::Approx(0.5F));
  CHECK(ant::presentation::ease(0.0F) == Catch::Approx(0.0F));
  CHECK(ant::presentation::ease(1.0F) == Catch::Approx(1.0F));
  CHECK(ant::presentation::ease(0.5F) == Catch::Approx(0.5F));
  CHECK(ant::presentation::ease(-1.0F) == Catch::Approx(0.0F));
  CHECK(ant::presentation::ease(2.0F) == Catch::Approx(1.0F));
}
