#pragma once

#include "game/view.hpp"
#include "game/progression.hpp"
#include "presentation/camera.hpp"
#include "presentation/ui_state.hpp"
#include "presentation/interaction.hpp"
#include "presentation/desktop_input.hpp"
#include "presentation/icons.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ant::presentation {

// A short-lived mark on the world: soil thrown from a fresh cut, a grain set down on a heap, a
// bloom at the queen when a worker ecloses. Effects are read off changes in the view, so the
// simulation owes the renderer nothing.
struct Spark {
  enum class Kind : std::uint8_t { Puff, Grain, Bloom, Ring };
  Vector2 position{};
  Vector2 drift{};
  float age{};
  float life{1.0F};
  float size{1.0F};
  Color tint{};
  Kind kind{Kind::Puff};
};

// One line of the colony's recent history, shown in the inspector so the player can see what the
// colony just did rather than only what it currently is.
struct LogEntry {
  std::string text;
  Icon icon{Icon::Worker};
  float age{};
};

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
  [[nodiscard]] bool legacy_panel_open() const { return scenes_.scene() == Scene::Legacy; }
  [[nodiscard]] bool new_colony_requested() const { return new_colony_requested_; }
  // Ends the current colony with no payout. Only set after the player confirms it twice.
  [[nodiscard]] bool abandon_requested() const { return abandon_requested_; }
  void clear_requests();
  // Short save/recovery line shown in the footer, owned by the app coordinator.
  void set_status_line(std::string status) { status_line_ = std::move(status); }
  // While set, the app has an unreadable profile and is waiting for the player to choose.
  void set_recovery_prompt(bool prompt) { scenes_.set_recovery(prompt); }
  void open_legacy_panel() { scenes_.open_legacy(); }
  [[nodiscard]] bool modal_open() const { return scenes_.modal(); }
  void set_run_id(std::string run_id) { run_id_ = std::move(run_id); }
  void close_panels() { scenes_.close(); }
  void set_zoom(float zoom) { camera_.set_zoom(zoom); }
  void focus(sim::GridPos cell) { camera_.focus(cell); }
  [[nodiscard]] Rectangle debug_viewport() const { return camera_.viewport(); }
  [[nodiscard]] Vector2 debug_target() const { return camera_.camera().target; }
  [[nodiscard]] Vector2 debug_offset() const { return camera_.camera().offset; }
  [[nodiscard]] float debug_zoom() const { return camera_.zoom(); }
  [[nodiscard]] Vector2 debug_world_to_screen(Vector2 world) const { return camera_.world_to_screen(world); }

private:
  void draw_world(const game::GameView& view);
  void refresh_terrain_texture(const game::GameView& view);
  [[nodiscard]] float heading_for(const sim::ActorSnapshot& actor, float dx, float dy);
  void draw_ant(const sim::ActorSnapshot& actor, const ActorPose& pose, float zoom,
                bool selected);
  void draw_interface(const game::GameView& view, bool paused, int speed, bool simulation_limited);
  void draw_pause_menu();
  void draw_recovery_prompt();
  void draw_legacy_panel(const game::GameView& view);
  void draw_food_source(const sim::FoodSource& source, bool recruiting) const;
  void draw_granary(const game::GameView& view) const;
  // Reads what changed since the last frame and turns it into sparks and log lines.
  void observe(const game::GameView& view, float delta);
  void note(std::string text, Icon icon);
  void add_spark(Spark spark);
  void draw_sparks() const;
  void draw_log(int panel_x, int panel_width, int top) const;
  void draw_readiness(const game::GameView& view, int panel_x, int panel_width, int top) const;
  // A modal card with a title, a rule under it and a shadow, shared by every overlay.
  [[nodiscard]] Rectangle draw_modal_card(float width, float height, const char* title) const;
  [[nodiscard]] std::string truncate_to_width(const std::string& text, float max_width,
                                              float size) const;
  void update_selection(const game::GameView& view, double interpolation_alpha);
  void draw_text(const char* text, int x, int y, int size = 20,
                 Color color = Color{37, 45, 40, 255}) const;
  // `stacked` puts the icon above the label, for a small square control that still says what it is.
  void draw_button(Rectangle bounds, const char* label, bool active, const WidgetVisual& visual,
                   bool enabled = true, std::optional<Icon> icon = {}, bool stacked = false) const;
  // Tracks, draws and reports one button in a single call, so every clickable control in the
  // interface is declared the same way.
  [[nodiscard]] bool button(std::uint32_t id, Rectangle bounds, const char* label, bool active,
                            bool enabled = true, std::optional<Icon> icon = {},
                            const char* tooltip = nullptr, const char* tooltip_title = nullptr,
                            bool stacked = false);
  void draw_tooltip(const char* title, const char* body, float anchor_x, float anchor_y) const;

  DesktopInput desktop_input_;
  CameraController camera_;
  UiState ui_;
  const char* tooltip_title_{};
  std::string tooltip_body_;
  float tooltip_x_{};
  float tooltip_y_{};
  Font font_{};
  bool font_loaded_{};
  // The ground only changes when a cell is dug, so it is baked once into a texture instead of
  // eighty thousand rectangles per frame. That budget pays for the grain and carved edges.
  Texture2D terrain_texture_{};
  bool terrain_texture_ready_{};
  std::uint64_t terrain_revision_{};
  std::uint64_t rooms_key_{};
  // What the world looked like at the last bake, so a fresh cut can be spotted and puffed.
  std::vector<sim::Material> baked_terrain_;
  std::vector<std::int64_t> pile_amounts_;
  std::vector<Spark> sparks_;
  std::deque<LogEntry> log_;
  game::GameView::Watched watched_{};
  bool observed_{};
  double terrain_built_at_{};
  // Ants keep facing where they were last going, so a stopped ant does not snap to a default.
  std::unordered_map<sim::EntityId, float> heading_;
  AntSelection selection_;
  WorldClick world_click_;
  std::vector<ActorPose> poses_;
  std::string run_id_;
  InterfaceLayout layout_;
  bool select_requested_{};
  Point select_point_{};
  float panel_scroll_{};
  SceneState scenes_;
  Scene input_scene_{Scene::Colony};
  bool inspector_open_{true};
  bool inspector_toggle_requested_{};
  bool toggle_pause_requested_{};
  std::optional<int> speed_requested_;
  std::optional<game::UpgradeId> upgrade_requested_;
  std::optional<sim::Focus> focus_requested_;
  bool save_requested_{};
  bool recover_requested_{};
  bool new_colony_requested_{};
  bool abandon_requested_{};
  // Abandoning a colony is destructive, so the button arms a confirmation first.
  bool abandon_armed_{};
  // Advances only while the colony is running, so a paused ant holds still instead of treading air.
  float animation_clock_{};

  bool flight_requested_{};
  bool new_run_requested_{};
  std::optional<game::TraitBranch> trait_requested_;
  std::string status_line_;
};

} // namespace ant::presentation
