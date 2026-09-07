#include "presentation/interaction.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace ant::presentation;

TEST_CASE("logical picking agrees with a Retina camera after pan and zoom", "[interaction]") {
  for (const float dpi : {1.0F, 2.0F}) {
    for (const float zoom : {3.0F, 6.0F, 12.0F}) {
      const ViewTransform camera{{192, 58}, {560 * dpi, 400 * dpi}, zoom * dpi, dpi};
      const auto screen = camera.to_screen({200, 60});
      CHECK(screen.x == Catch::Approx(560 + 8 * zoom));
      CHECK(screen.y == Catch::Approx(400 + 2 * zoom));
      const auto world = camera.to_world(screen);
      CHECK(world.x == Catch::Approx(200));
      CHECK(world.y == Catch::Approx(60));
    }
  }
}

TEST_CASE("picking uses interpolated ant centres with caste bounds and stable ties", "[interaction]") {
  ant::sim::ActorSnapshot worker;
  worker.id = 5; worker.previous_x = 10; worker.previous_y = 20;
  worker.x = 20; worker.y = 30;
  const auto pose = actor_pose(worker, 0.5);
  CHECK(pose.centre.x > 14.5F);
  CHECK(pose.centre.x < 15.5F);
  const std::array poses{pose};
  CHECK(pick_actor(poses, pose.centre, 12) == 5);
  CHECK_FALSE(pick_actor(poses, {static_cast<float>(worker.x), static_cast<float>(worker.y)}, 12));
  const std::array crowd{ActorPose{9, {10, 10}, 1}, ActorPose{3, {10, 10}, 1}};
  CHECK(pick_actor(crowd, {10, 10}, 12) == 3);
  const std::array queen{ActorPose{1, {10, 10}, 2}};
  CHECK(pick_actor(queen, {13, 10}, 12) == 1);
  CHECK(pick_actor(poses, {pose.centre.x + 3, pose.centre.y}, 2) == 5);
}

TEST_CASE("world clicks require a single owned release without dragging", "[interaction]") {
  WorldClick gesture;
  CHECK_FALSE(gesture.update({10, 10}, true, true, false, true));
  CHECK(gesture.update({12, 10}, false, false, true, true));
  CHECK_FALSE(gesture.update({12, 10}, false, false, true, true));
  CHECK(gesture.update({10, 10}, true, false, true, true)); // native short click
  CHECK_FALSE(gesture.update({10, 10}, true, true, false, false)); // UI press
  CHECK_FALSE(gesture.update({10, 10}, false, false, true, true));
  CHECK_FALSE(gesture.update({10, 10}, true, true, false, true));
  CHECK_FALSE(gesture.update({10, 10}, false, true, false, false)); // leave owner
  CHECK_FALSE(gesture.update({10, 10}, false, false, true, true));
  CHECK_FALSE(gesture.update({10, 10}, true, true, false, true));
  CHECK_FALSE(gesture.update({20, 10}, false, true, false, true));
  CHECK_FALSE(gesture.update({10, 10}, false, false, true, true)); // returning cannot undo drag
  CHECK_FALSE(gesture.update({10, 10}, true, true, false, true));
  gesture.cancel(); // modal opens while held
  CHECK_FALSE(gesture.update({10, 10}, false, false, true, true));
}

TEST_CASE("selection survives ordering but clears after death and across runs", "[interaction]") {
  AntSelection selection;
  const std::array poses{ActorPose{1, {10, 10}, 1}, ActorPose{2, {20, 20}, 1}};
  selection.synchronize("run-a", poses);
  selection.pick(poses, {10, 10}, 12);
  const std::array reversed{poses[1], poses[0]};
  selection.synchronize("run-a", reversed);
  CHECK(selection.id() == 1);
  selection.synchronize("run-b", poses);
  CHECK_FALSE(selection.id());
  selection.pick(poses, {10, 10}, 12);
  selection.synchronize("run-b", std::span(poses).subspan(1));
  CHECK_FALSE(selection.id());
  selection.pick(poses, {20, 20}, 12);
  selection.pick(poses, {80, 80}, 12);
  CHECK_FALSE(selection.id());
}

TEST_CASE("minimum layout pins selection above footer and owns the overlay", "[interaction]") {
  for (const auto size : {Point{1024, 640}, Point{1440, 900}}) {
    const auto layout = interface_layout(static_cast<int>(size.x), static_cast<int>(size.y), true);
    CHECK(layout.selected.y >= layout.panel_body.y + layout.panel_body.height);
    CHECK(layout.selected.y + layout.selected.height <= layout.footer.y);
    CHECK(layout.panel_body.height >= 250.0F);
    CHECK_FALSE(layout.owns_world({size.x - 10, 200}));
    CHECK_FALSE(layout.owns_world({10, 68}));
    CHECK_FALSE(layout.owns_world({10, size.y - 70}));
    CHECK(layout.owns_world({100, 200}));
    const auto closed = interface_layout(static_cast<int>(size.x), static_cast<int>(size.y), false);
    CHECK(closed.owns_world({size.x - 10, 200}));
  }
}

TEST_CASE("the panel body scrolls exactly far enough to reach its last line", "[interaction]") {
  const auto tall = interface_layout(1440, 900, true);
  CHECK(tall.panel_body.y + tall.panel_body.height >= kPanelContentBottom);
  CHECK(tall.panel_scroll_range() == Catch::Approx(0.0F));

  const auto minimum = interface_layout(1024, 640, true);
  CHECK(minimum.panel_scroll_range() > 0.0F);
  // Fully scrolled, the last drawn line sits exactly on the bottom edge of the body — no further.
  CHECK(kPanelContentBottom - minimum.panel_scroll_range() ==
        Catch::Approx(minimum.panel_body.y + minimum.panel_body.height));
  // Scrolling never hides the top of the content behind the header.
  CHECK(minimum.panel_body.y >= 112.0F);
}

TEST_CASE("Escape dismisses top scenes and recovery retains exclusive ownership", "[interaction]") {
  SceneState scene;
  scene.escape(); CHECK(scene.scene() == Scene::PauseMenu); CHECK(scene.modal());
  scene.escape(); CHECK_FALSE(scene.modal());
  scene.open_legacy(); scene.escape(); CHECK_FALSE(scene.modal());
  scene.set_recovery(true);
  scene.escape(); scene.toggle_legacy(); scene.close();
  CHECK(scene.scene() == Scene::Recovery);
  scene.set_recovery(false); CHECK_FALSE(scene.modal());
}

TEST_CASE("modal and clipped controls cannot receive underlying clicks", "[interaction]") {
  UiState ui;
  const UiRect card{0, 100, 100, 40};
  ui.begin_frame(10, 120, true, true, false, 0.016F);
  ui.set_input_region(false);
  CHECK_FALSE(ui.track(1, card).held);
  ui.begin_frame(10, 120, false, false, true, 0.016F);
  ui.set_input_region(true);
  CHECK_FALSE(ui.track(1, card).clicked);
  ui.begin_frame(10, 120, true, true, false, 0.016F);
  ui.set_input_region(true, UiRect{0, 130, 100, 100});
  CHECK_FALSE(ui.track(1, card).held);
  ui.begin_frame(10, 135, false, false, true, 0.016F);
  ui.set_input_region(true, UiRect{0, 130, 100, 100});
  CHECK_FALSE(ui.track(1, card).clicked);
  // Captured controls are cancelled on a scene boundary, even if the pointer never moved.
  ui.begin_frame(10, 120, true, true, false, 0.016F);
  CHECK(ui.track(1, card).held);
  ui.cancel_capture();
  ui.begin_frame(10, 120, false, false, true, 0.016F);
  CHECK_FALSE(ui.track(1, card).clicked);
}

TEST_CASE("a short native click retains its press origin across UI and world boundaries", "[interaction]") {
  UiState ui;
  const UiRect card{100, 100, 80, 40};
  ui.begin_frame(120, 120, false, true, true, 0.016F);
  ui.set_press_origin(10, 10);
  CHECK_FALSE(ui.track(1, card).clicked);
  ui.begin_frame(120, 120, false, true, true, 0.016F);
  ui.set_press_origin(120, 120);
  CHECK(ui.track(1, card).clicked);
}
