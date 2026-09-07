#pragma once

#include "presentation/ui_state.hpp"
#include "sim/world.hpp"

#include <optional>
#include <span>
#include <string>

namespace ant::presentation {

struct Point { float x{}; float y{}; };

// The same transform used by the Retina camera, with a logical-pixel public interface.
struct ViewTransform {
  Point target;
  Point backing_offset;
  float backing_zoom{1.0F};
  float backing_scale{1.0F};
  [[nodiscard]] Point to_world(Point logical) const;
  [[nodiscard]] Point to_screen(Point world) const;
};

// The bottom of the last line the colony panel draws, in unscrolled panel coordinates. The layout
// owns it so the scrollable range and the renderer's content cannot disagree.
inline constexpr float kPanelContentBottom = 596.0F;

struct InterfaceLayout {
  UiRect world;
  UiRect panel;
  UiRect panel_body;
  UiRect selected;
  UiRect footer;
  [[nodiscard]] bool owns_world(Point pointer) const;
  // How far the panel body must scroll before its last line is visible. Zero when it all fits.
  [[nodiscard]] float panel_scroll_range() const;
};
[[nodiscard]] InterfaceLayout interface_layout(int width, int height, bool inspector_open);

struct ActorPose {
  sim::EntityId id{};
  Point centre;
  float scale{1.0F};
};
[[nodiscard]] ActorPose actor_pose(const sim::ActorSnapshot& actor, double alpha);
[[nodiscard]] std::optional<sim::EntityId> pick_actor(std::span<const ActorPose> poses,
                                                    Point world, float logical_zoom);

// Capture on press. Leaving the owner or exceeding the slop cancels permanently until release.
class WorldClick {
public:
  [[nodiscard]] bool update(Point pointer, bool pressed, bool down, bool released, bool world_owned);
  void cancel() { captured_ = false; }
private:
  Point origin_;
  bool captured_{};
};

class AntSelection {
public:
  void synchronize(const std::string& run_id, std::span<const ActorPose> poses);
  void pick(std::span<const ActorPose> poses, Point world, float zoom);
  [[nodiscard]] std::optional<sim::EntityId> id() const { return selected_; }
private:
  std::string run_id_;
  std::optional<sim::EntityId> selected_;
};

// Pausing for a modal is additive: closing it never changes the user's pause preference.
enum class Scene { Colony, Legacy, PauseMenu, Recovery };
class SceneState {
public:
  [[nodiscard]] Scene scene() const { return recovery_ ? Scene::Recovery : scene_; }
  [[nodiscard]] bool modal() const { return scene() != Scene::Colony; }
  void set_recovery(bool value) { recovery_ = value; }
  void open_legacy() { if (!recovery_) scene_ = Scene::Legacy; }
  void toggle_legacy();
  void escape();
  void close() { if (!recovery_) scene_ = Scene::Colony; }
private:
  Scene scene_{Scene::Colony};
  bool recovery_{};
};

} // namespace ant::presentation
