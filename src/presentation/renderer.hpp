#pragma once

#include "game/view.hpp"
#include "game/progression.hpp"
#include "presentation/camera.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace ant::presentation {

class Renderer {
public:
  Renderer();
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void update_input(float delta_seconds);
  void draw(const game::GameView& view, double interpolation_alpha, bool paused, int speed,
            bool simulation_limited);

  [[nodiscard]] bool toggle_pause_requested() const { return toggle_pause_requested_; }
  [[nodiscard]] std::optional<int> speed_requested() const { return speed_requested_; }
  [[nodiscard]] std::optional<game::UpgradeId> upgrade_requested() const { return upgrade_requested_; }
  [[nodiscard]] std::optional<sim::Focus> focus_requested() const { return focus_requested_; }
  [[nodiscard]] bool save_requested() const { return save_requested_; }
  [[nodiscard]] bool recover_requested() const { return recover_requested_; }
  [[nodiscard]] bool flight_requested() const { return flight_requested_; }
  [[nodiscard]] bool new_run_requested() const { return new_run_requested_; }
  [[nodiscard]] std::optional<game::TraitBranch> trait_requested() const { return trait_requested_; }
  [[nodiscard]] bool legacy_panel_open() const { return legacy_panel_open_; }
  [[nodiscard]] bool new_colony_requested() const { return new_colony_requested_; }
  void clear_requests();
  // Short save/recovery line shown in the footer, owned by the app coordinator.
  void set_status_line(std::string status) { status_line_ = std::move(status); }
  // While set, the app has an unreadable profile and is waiting for the player to choose.
  void set_recovery_prompt(bool prompt) { recovery_prompt_ = prompt; }
  void open_legacy_panel() { legacy_panel_open_ = true; }
  void set_zoom(float zoom) { camera_.set_zoom(zoom); }
  void focus(sim::GridPos cell) { camera_.focus(cell); }

private:
  void draw_world(const game::GameView& view, double interpolation_alpha);
  void refresh_terrain_texture(const game::GameView& view);
  [[nodiscard]] float heading_for(const sim::ActorSnapshot& actor, float dx, float dy);
  void draw_ant(const sim::ActorSnapshot& actor, double interpolation_alpha, float zoom,
                bool selected);
  void draw_interface(const game::GameView& view, bool paused, int speed, bool simulation_limited);
  void draw_recovery_prompt() const;
  void draw_legacy_panel(const game::GameView& view) const;
  [[nodiscard]] std::string truncate_to_width(const std::string& text, float max_width,
                                              float size) const;
  void update_selection(const game::GameView& view);
  void draw_text(const char* text, int x, int y, int size = 20,
                 Color color = Color{37, 45, 40, 255}) const;
  void draw_button(Rectangle bounds, const char* label, bool active) const;

  CameraController camera_;
  Font font_{};
  bool font_loaded_{};
  // The ground only changes when a cell is dug, so it is baked once into a texture instead of
  // eighty thousand rectangles per frame. That budget pays for the grain and carved edges.
  Texture2D terrain_texture_{};
  bool terrain_texture_ready_{};
  std::uint64_t terrain_revision_{};
  double terrain_built_at_{};
  // Ants keep facing where they were last going, so a stopped ant does not snap to a default.
  std::unordered_map<sim::EntityId, float> heading_;
  std::optional<sim::EntityId> selected_id_;
  bool inspector_open_{true};
  bool toggle_pause_requested_{};
  std::optional<int> speed_requested_;
  std::optional<game::UpgradeId> upgrade_requested_;
  std::optional<sim::Focus> focus_requested_;
  bool save_requested_{};
  bool recover_requested_{};
  bool new_colony_requested_{};
  bool recovery_prompt_{};
  bool legacy_panel_open_{};
  bool flight_requested_{};
  bool new_run_requested_{};
  std::optional<game::TraitBranch> trait_requested_;
  std::string status_line_;
};

} // namespace ant::presentation
