#include "presentation/interaction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ant::presentation {

Point ViewTransform::to_world(const Point logical) const {
  return {(logical.x * backing_scale - backing_offset.x) / backing_zoom + target.x,
          (logical.y * backing_scale - backing_offset.y) / backing_zoom + target.y};
}
Point ViewTransform::to_screen(const Point world) const {
  return {((world.x - target.x) * backing_zoom + backing_offset.x) / backing_scale,
          ((world.y - target.y) * backing_zoom + backing_offset.y) / backing_scale};
}

InterfaceLayout interface_layout(const int width, const int height, const bool inspector_open) {
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const float panel_width = inspector_open ? (width >= 1120 ? 320.0F : 300.0F) : 0.0F;
  const float dock = width >= 1120 ? panel_width : 0.0F;
  const float bottom = h - 88.0F;
  return {{0, 72, w - dock, bottom - 72},
          {w - panel_width, 72, panel_width, bottom - 72},
          {w - panel_width, 112, panel_width, bottom - 112 - 142},
          {w - panel_width, bottom - 142, panel_width, 142},
          {0, bottom, w, 88}};
}
float InterfaceLayout::panel_scroll_range() const {
  return std::max(0.0F, kPanelContentBottom - (panel_body.y + panel_body.height));
}
bool InterfaceLayout::owns_world(const Point pointer) const {
  return world.contains(pointer.x, pointer.y) && !panel.contains(pointer.x, pointer.y);
}

ActorPose actor_pose(const sim::ActorSnapshot& actor, const double alpha) {
  const double t = std::clamp(alpha, 0.0, 1.0);
  return {actor.id,
          {static_cast<float>(actor.previous_x + (actor.x - actor.previous_x) * t) +
               static_cast<float>(static_cast<int>((actor.id * 17ULL) % 7ULL) - 3) * 0.09F,
           static_cast<float>(actor.previous_y + (actor.y - actor.previous_y) * t) +
               static_cast<float>(static_cast<int>((actor.id * 11ULL) % 5ULL) - 2) * 0.08F},
          actor.kind == sim::AntKind::Queen ? 2.0F :
              actor.kind == sim::AntKind::WingedQueen ? 1.45F : 1.0F};
}
std::optional<sim::EntityId> pick_actor(const std::span<const ActorPose> poses,
                                       const Point world, const float logical_zoom) {
  std::optional<sim::EntityId> best;
  float nearest = std::numeric_limits<float>::max();
  for (const auto& pose : poses) {
    const float dx = pose.centre.x - world.x, dy = pose.centre.y - world.y;
    const float distance = dx * dx + dy * dy;
    const float radius = std::max(1.8F * pose.scale, 8.0F / std::max(0.01F, logical_zoom));
    if (distance <= radius * radius &&
        (distance < nearest || (distance == nearest && (!best || pose.id < *best)))) {
      nearest = distance;
      best = pose.id;
    }
  }
  return best;
}

bool WorldClick::update(const Point pointer, const bool pressed, const bool down,
                         const bool released, const bool world_owned) {
  if (pressed) { captured_ = world_owned; origin_ = pointer; }
  const float dx = pointer.x - origin_.x, dy = pointer.y - origin_.y;
  if (!world_owned || dx * dx + dy * dy > 16.0F) captured_ = false;
  const bool clicked = released && captured_;
  if (released || (!down && !pressed)) captured_ = false;
  return clicked;
}

void AntSelection::synchronize(const std::string& run_id, const std::span<const ActorPose> poses) {
  if (run_id != run_id_) { selected_.reset(); run_id_ = run_id; }
  if (selected_ && std::none_of(poses.begin(), poses.end(), [this](const ActorPose& pose) {
        return pose.id == *selected_;
      })) selected_.reset();
}
void AntSelection::pick(const std::span<const ActorPose> poses, const Point world, const float zoom) {
  selected_ = pick_actor(poses, world, zoom);
}
void SceneState::toggle_legacy() {
  if (!recovery_) scene_ = scene_ == Scene::Legacy ? Scene::Colony : Scene::Legacy;
}
void SceneState::escape() {
  if (!recovery_) scene_ = scene_ == Scene::Colony ? Scene::PauseMenu : Scene::Colony;
}

} // namespace ant::presentation
