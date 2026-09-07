#include "presentation/renderer.hpp"

#include "sim/grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace ant::presentation {
namespace {

constexpr Color kDeepSoil{48, 41, 35, 255};
// Earth reads as layers, not one swatch: mossy turf, warm topsoil, cooling subsoil, dark depths,
// then the grey clay bed under all of it.
constexpr Color kTurf{104, 116, 74, 255};
constexpr Color kTopsoil{150, 112, 74, 255};
constexpr Color kSubsoil{124, 92, 62, 255};
constexpr Color kDeepEarth{78, 62, 48, 255};
constexpr Color kClayLight{118, 100, 88, 255};
constexpr Color kClayDeep{78, 66, 60, 255};
// Underground is warm dark earth, not black: a tunnel should look carved out of soil.
constexpr Color kTunnel{47, 39, 32, 255};
// The trodden floor of a gallery, warmed by the soil it was cut from.
constexpr Color kTunnelFloor{92, 71, 52, 255};
// The rooms the colony keeps: the brood room is warmer than the granary, and both are warmer than
// the passages between them.
constexpr Color kBroodRoom{72, 55, 41, 255};
constexpr Color kGranaryRoom{62, 51, 37, 255};
constexpr Color kRoot{67, 52, 40, 255};
constexpr Color kBedrock{37, 33, 30, 255};
constexpr Color kStone{96, 99, 104, 255};
constexpr Color kFoliage{112, 132, 87, 255};
constexpr Color kSky{201, 213, 195, 255};
// A calm sky with a warmer band at the horizon, so the surface feels lit from above.
constexpr Color kSkyHigh{178, 197, 190, 255};
constexpr Color kSkyLow{221, 224, 197, 255};
constexpr Color kPaper{238, 228, 204, 255};
constexpr Color kInk{37, 45, 40, 255};
constexpr Color kCarbohydrate{221, 189, 103, 255};
constexpr Color kProtein{199, 127, 101, 255};
constexpr Color kBerry{168, 74, 82, 255};
constexpr Color kHusk{92, 74, 52, 255};
constexpr Color kBrood{234, 219, 192, 255};
constexpr Color kSelection{152, 203, 195, 255};
constexpr Color kPaperSoft{246, 239, 220, 255};
constexpr Color kMutedInk{85, 91, 84, 255};
constexpr Color kWarmLine{193, 176, 143, 255};

// Ants read as warm dark chitin rather than UI ink, with a sheen so the segments separate.
constexpr Color kChitin{74, 55, 40, 255};
constexpr Color kChitinSheen{126, 98, 72, 255};
constexpr Color kQueenChitin{88, 57, 47, 255};
constexpr Color kWing{206, 220, 216, 120};

// Each terrain cell becomes a 3x3 patch of texels, which is what makes grain and carved tunnel
// edges possible at close zoom without a per-frame cost.
constexpr int kTerrainDetail = 3;

std::uint64_t hash_pixel(const int x, const int y, const std::uint64_t seed) {
  std::uint64_t value = static_cast<std::uint64_t>(x) * 73856093ULL ^
                        static_cast<std::uint64_t>(y) * 19349663ULL ^ seed * 83492791ULL;
  value ^= value >> 29U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 32U;
  return value;
}

// A rotated ellipse, which raylib has no primitive for. Fourteen points is smooth at the zoom
// levels where a body segment is actually visible.
void draw_oriented_ellipse(const Vector2 center, const float along, const float across,
                           const float heading, const Color color) {
  constexpr int kPoints = 14;
  const float cos_h = std::cos(heading);
  const float sin_h = std::sin(heading);
  std::array<Vector2, kPoints + 2> fan{};
  fan[0] = center;
  for (int index = 0; index <= kPoints; ++index) {
    // Negative sweep: raylib wants counter-clockwise winding, and screen Y points down, so a
    // positive parametric sweep would wind the wrong way and every segment would be culled.
    const float angle = -static_cast<float>(index) * 2.0F * PI / static_cast<float>(kPoints);
    const float local_x = std::cos(angle) * along;
    const float local_y = std::sin(angle) * across;
    fan[static_cast<std::size_t>(index) + 1] = {center.x + local_x * cos_h - local_y * sin_h,
                                                center.y + local_x * sin_h + local_y * cos_h};
  }
  // raylib's triangle fan needs counter-clockwise winding in screen space.
  DrawTriangleFan(fan.data(), static_cast<int>(fan.size()), color);
}

Vector2 offset_along(const Vector2 origin, const float heading, const float along,
                     const float across) {
  const float cos_h = std::cos(heading);
  const float sin_h = std::sin(heading);
  return {origin.x + along * cos_h - across * sin_h, origin.y + along * sin_h + across * cos_h};
}

Color mix(const Color from, const Color to, const float weight) {
  const float t = std::clamp(weight, 0.0F, 1.0F);
  const auto blend = [t](const unsigned char a, const unsigned char b) {
    return static_cast<unsigned char>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
  };
  return {blend(from.r, to.r), blend(from.g, to.g), blend(from.b, to.b), blend(from.a, to.a)};
}

Color shade(const Color base, const int delta) {
  return {static_cast<unsigned char>(std::clamp(static_cast<int>(base.r) + delta, 0, 255)),
          static_cast<unsigned char>(std::clamp(static_cast<int>(base.g) + delta, 0, 255)),
          static_cast<unsigned char>(std::clamp(static_cast<int>(base.b) + delta, 0, 255)),
          base.a};
}

// The earth a cell is cut from, before anything is done to it: a run of layers from turf down to
// the clay bed, banded so the cross-section reads as ground that was laid down rather than filled.
Color earth_color(const int x, const int y, const std::uint64_t seed) {
  // Seams wander, on two scales, so a stratum reads as laid-down ground rather than a ruled line.
  const int drift = static_cast<int>(hash_pixel(x / 7, 3, seed) % 7ULL) - 3 +
                    static_cast<int>(hash_pixel(x / 29, 5, seed ^ 0x11ULL) % 9ULL) - 4;
  const int banded = y + drift;
  const int coarse = static_cast<int>(hash_pixel(0, banded / 11, seed) % 21ULL) - 10;
  const int seam = static_cast<int>(hash_pixel(2, banded / 5, seed ^ 0x5EAULL) % 11ULL) - 5;
  const int fine = static_cast<int>(hash_pixel(1, banded / 3, seed ^ 0xB4ULL) % 7ULL) - 3;

  Color base;
  if (y <= 34) {
    base = mix(kTurf, kTopsoil, std::clamp(static_cast<float>(y - 32) / 3.0F, 0.0F, 1.0F));
  } else if (y < 90) {
    base = mix(kTopsoil, kSubsoil, static_cast<float>(y - 34) / 56.0F);
  } else if (y < 138) {
    base = mix(kSubsoil, kDeepEarth, static_cast<float>(y - 90) / 48.0F);
  } else {
    base = mix(kClayLight, kClayDeep, std::clamp(static_cast<float>(y - 138) / 74.0F, 0.0F, 1.0F));
  }
  return shade(base, coarse + seam + fine);
}

Color material_color(const sim::Material material, const int x, const int y,
                     const std::uint64_t seed) {
  if (material == sim::Material::Sky) return kSky;
  if (material == sim::Material::Air) return kTunnel;
  if (material == sim::Material::Bedrock) {
    const std::uint64_t speck = hash_pixel(x, y, seed ^ 0xBED0CULL);
    return shade(kBedrock, static_cast<int>(speck % 9ULL) - 4);
  }
  if (material == sim::Material::Stone) {
    // Cool grey flecked against the warm soil, so a lens reads as rock and not as darker dirt.
    const std::uint64_t speck = hash_pixel(x, y, seed ^ 0x570E5ULL);
    return shade(kStone, static_cast<int>(speck % 19ULL) - 9);
  }
  const Color earth = earth_color(x, y, seed);
  if (material == sim::Material::Root) {
    // Woody, and only a little darker than the ground it runs through: a root should read as a
    // strand growing down, not as a crack in the wall.
    return mix(earth, kRoot, 0.62F);
  }
  return earth;
}

const char* nutrient_name(const sim::Nutrient nutrient) {
  return nutrient == sim::Nutrient::Carbohydrate ? "carbohydrate" : "protein";
}

const char* focus_name(const sim::Focus focus) {
  switch (focus) {
  case sim::Focus::Balanced: return "Balanced";
  case sim::Focus::Growth: return "Growth";
  case sim::Focus::Expansion: return "Expansion";
  case sim::Focus::Foraging: return "Foraging";
  }
  return "Balanced";
}

} // namespace

Renderer::Renderer() {
  const std::filesystem::path application_directory = GetApplicationDirectory();
  const std::filesystem::path bundled =
      application_directory / "assets" / "fonts" / "Nunito-SemiBold.ttf";
  const std::filesystem::path source =
      std::filesystem::path(ANT_SOURCE_ASSET_DIR) / "fonts" / "Nunito-SemiBold.ttf";
  const std::filesystem::path selected = std::filesystem::exists(bundled) ? bundled : source;
  // A generously sized atlas scales down cleanly on Retina and fractional window scaling.
  const Vector2 dpi = GetWindowScaleDPI();
  const int atlas_size = static_cast<int>(std::ceil(128.0F * std::max(dpi.x, dpi.y)));
  font_ = LoadFontEx(selected.string().c_str(), atlas_size, nullptr, 0);
  font_loaded_ = font_.texture.id != 0;
  if (font_loaded_) {
    SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);
  } else {
    font_ = GetFontDefault();
  }
}

Renderer::~Renderer() {
  if (font_loaded_) {
    UnloadFont(font_);
  }
  if (terrain_texture_ready_) {
    UnloadTexture(terrain_texture_);
  }
}

void Renderer::draw_text(const char* text, const int x, const int y, const int size,
                         const Color color) const {
  DrawTextEx(font_, text, {static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(size),
             0.0F, color);
}

void Renderer::draw_button(const Rectangle bounds, const char* label, const bool active,
                          const WidgetVisual& visual, const bool enabled,
                          const std::optional<Icon> icon, const bool stacked) const {
  // Lift on hover, settle on press. Both are eased weights, so the motion is smooth and matches at
  // any frame rate.
  const float lift = visual.hover * 2.0F - visual.press * 2.6F;
  const Rectangle body{bounds.x, bounds.y - lift, bounds.width, bounds.height};
  const Color resting = active ? kSelection : Color{229, 219, 195, 255};
  const Color warmed = active ? shade(kSelection, 14) : Color{243, 235, 214, 255};
  const Color fill = enabled ? mix(resting, warmed, visual.hover) : Color{226, 219, 202, 255};

  if (enabled && visual.hover > 0.01F) {
    const float spread = 1.0F + visual.hover * 2.5F;
    DrawRectangleRounded({body.x - spread * 0.5F, body.y + 2.0F + spread * 0.5F,
                          body.width + spread, body.height},
                         0.24F, 8, Fade(BLACK, 0.10F * visual.hover));
  }
  DrawRectangleRounded(body, 0.22F, 8, fill);
  const Color edge = active ? Color{69, 111, 103, 255} : mix(kWarmLine, Color{146, 128, 96, 255}, visual.hover);
  DrawRectangleRoundedLinesEx(body, 0.22F, 8, 1.0F + visual.hover * 0.6F, edge);

  const Color foreground = enabled ? kInk : kMutedInk;
  const bool has_label = label != nullptr && label[0] != '\0';
  if (stacked) {
    const float glyph = 17.0F;
    if (icon) {
      draw_icon(*icon, {body.x + (body.width - glyph) * 0.5F, body.y + 6.0F, glyph, glyph},
                foreground);
    }
    if (has_label) {
      const Vector2 measured = MeasureTextEx(font_, label, 13.0F, 0.0F);
      DrawTextEx(font_, label, {body.x + (body.width - measured.x) * 0.5F, body.y + 25.0F}, 13.0F,
                 0.0F, foreground);
    }
    return;
  }
  const float glyph = std::min(body.height - 14.0F, 20.0F);
  const Vector2 measured =
      has_label ? MeasureTextEx(font_, label, 19.0F, 0.0F) : Vector2{0.0F, 0.0F};
  const float gap = icon && has_label ? 7.0F : 0.0F;
  const float content = (icon ? glyph : 0.0F) + gap + measured.x;
  float cursor = body.x + (body.width - content) * 0.5F;
  if (icon) {
    draw_icon(*icon, {cursor, body.y + (body.height - glyph) * 0.5F, glyph, glyph}, foreground);
    cursor += glyph + gap;
  }
  if (has_label) {
    DrawTextEx(font_, label, {cursor, body.y + (body.height - measured.y) * 0.5F}, 19.0F, 0.0F,
               foreground);
  }
}

bool Renderer::button(const std::uint32_t id, const Rectangle bounds, const char* label,
                      const bool active, const bool enabled, const std::optional<Icon> icon,
                      const char* tooltip, const char* tooltip_title, const bool stacked) {
  const WidgetVisual visual =
      ui_.track(id, {bounds.x, bounds.y, bounds.width, bounds.height}, enabled);
  draw_button(bounds, label, active, visual, enabled, icon, stacked);
  // An icon-only control still has to say what it does, so hover always carries a name.
  const bool named = label != nullptr && label[0] != '\0';
  if (visual.over && tooltip != nullptr) {
    tooltip_title_ = tooltip_title != nullptr ? tooltip_title : (named ? label : tooltip);
    tooltip_body_ = tooltip;
    tooltip_x_ = bounds.x;
    tooltip_y_ = bounds.y;
  }
  return visual.clicked && enabled;
}

Rectangle Renderer::draw_modal_card(const float width, const float height,
                                    const char* title) const {
  const float screen_width = static_cast<float>(GetScreenWidth());
  const float screen_height = static_cast<float>(GetScreenHeight());
  DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{24, 28, 25, 185});
  const float panel_width = std::min(screen_width - 80.0F, width);
  const Rectangle panel{(screen_width - panel_width) * 0.5F, (screen_height - height) * 0.5F,
                        panel_width, height};
  DrawRectangleRounded({panel.x + 2.0F, panel.y + 8.0F, panel.width, panel.height}, 0.05F, 8,
                       Fade(BLACK, 0.28F));
  DrawRectangleRounded(panel, 0.05F, 8, kPaper);
  DrawRectangleRoundedLinesEx(panel, 0.05F, 8, 1.0F, kWarmLine);
  draw_text(title, static_cast<int>(panel.x) + 28, static_cast<int>(panel.y) + 24, 24);
  DrawLine(static_cast<int>(panel.x) + 28, static_cast<int>(panel.y) + 60,
           static_cast<int>(panel.x + panel.width) - 28, static_cast<int>(panel.y) + 60, kWarmLine);
  return panel;
}

void Renderer::draw_tooltip(const char* title, const char* body, const float anchor_x,
                            const float anchor_y) const {
  const Vector2 title_size = MeasureTextEx(font_, title, 17.0F, 0.0F);
  const Vector2 body_size = MeasureTextEx(font_, body, 15.0F, 0.0F);
  const float width = std::max(title_size.x, body_size.x) + 24.0F;
  const float height = 52.0F;
  const float x = std::clamp(anchor_x, 8.0F, static_cast<float>(GetScreenWidth()) - width - 8.0F);
  const float y = std::max(8.0F, anchor_y - height - 8.0F);
  DrawRectangleRounded({x + 1.0F, y + 3.0F, width, height}, 0.16F, 6, Fade(BLACK, 0.16F));
  DrawRectangleRounded({x, y, width, height}, 0.16F, 6, kPaperSoft);
  DrawRectangleRoundedLinesEx({x, y, width, height}, 0.16F, 6, 1.0F, kWarmLine);
  DrawTextEx(font_, title, {x + 12.0F, y + 8.0F}, 17.0F, 0.0F, kInk);
  DrawTextEx(font_, body, {x + 12.0F, y + 29.0F}, 15.0F, 0.0F, kMutedInk);
}

namespace {

constexpr float kUpgradeCardTop = 470.0F;
constexpr float kUpgradeCardHeight = 30.0F;
constexpr float kUpgradeCardStride = 34.0F;
constexpr float kFocusButtonTop = 374.0F;

Rectangle upgrade_card_bounds(const int panel_x, const int panel_width, const int index) {
  return {static_cast<float>(panel_x + 18), kUpgradeCardTop + kUpgradeCardStride * index,
          static_cast<float>(panel_width - 36), kUpgradeCardHeight};
}

Rectangle focus_button_bounds(const int panel_x, const int index) {
  return {static_cast<float>(panel_x + 18 + index * 70), kFocusButtonTop, 64.0F, 46.0F};
}


} // namespace

void Renderer::update_input(const float delta_seconds) {
  const DesktopFrame input = desktop_input_.take_frame();
  const auto key_pressed = [&input](const int key) { return input.keys[static_cast<std::size_t>(key)]; };
  if (inspector_toggle_requested_) {
    inspector_open_ = !inspector_open_;
    inspector_toggle_requested_ = false;
    ui_.cancel_capture();
    world_click_.cancel();
  }
  const Scene before = scenes_.scene();
  if (key_pressed(KEY_ESCAPE)) scenes_.escape();
  else if (key_pressed(KEY_L)) scenes_.toggle_legacy();
  const bool transitioned = before != scenes_.scene() || input_scene_ != scenes_.scene();
  input_scene_ = scenes_.scene();
  if (transitioned) {
    clear_requests();
    ui_.cancel_capture();
    world_click_.cancel();
    // Leaving the menu disarms the confirmation, so it can never fire on a later visit.
    abandon_armed_ = false;
  }
  const Vector2 mouse = GetMousePosition();
  const bool colony = !scenes_.modal() && !transitioned;
  if (colony && key_pressed(KEY_I)) inspector_open_ = !inspector_open_;
  layout_ = interface_layout(GetScreenWidth(), GetScreenHeight(), inspector_open_);
  camera_.layout(GetScreenWidth(), GetScreenHeight(), inspector_open_);
  const bool world_owned = colony && layout_.owns_world({mouse.x, mouse.y});
  // The camera answers to the keyboard for as long as the colony owns input, but only takes the
  // pointer when the pointer is actually over the world.
  camera_.update(delta_seconds, colony, world_owned);
  if (input.pressed) {
    static_cast<void>(world_click_.update(input.origin, true, true, false,
        colony && layout_.owns_world(input.origin)));
  }
  if (input.travel_squared > 16.0F) world_click_.cancel();
  select_requested_ = world_click_.update({mouse.x, mouse.y}, false, input.down,
      input.released, world_owned && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) &&
      !IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
  if (select_requested_) select_point_ = {mouse.x, mouse.y};
  if (colony && layout_.panel_body.contains(mouse.x, mouse.y)) {
    panel_scroll_ -= GetMouseWheelMove() * 36.0F;
  }
  panel_scroll_ = std::clamp(panel_scroll_, 0.0F, layout_.panel_scroll_range());
  if (colony) {
    if (key_pressed(KEY_SPACE)) toggle_pause_requested_ = true;
    if (key_pressed(KEY_ONE)) speed_requested_ = 1;
    else if (key_pressed(KEY_TWO)) speed_requested_ = 5;
    else if (key_pressed(KEY_THREE)) speed_requested_ = 20;
    if (key_pressed(KEY_F1)) upgrade_requested_ = game::UpgradeId::Excavation;
    else if (key_pressed(KEY_F2)) upgrade_requested_ = game::UpgradeId::Nursing;
    else if (key_pressed(KEY_F3)) upgrade_requested_ = game::UpgradeId::Foraging;
    else if (key_pressed(KEY_F4)) upgrade_requested_ = game::UpgradeId::Queen;
    if (key_pressed(KEY_B)) focus_requested_ = sim::Focus::Balanced;
    else if (key_pressed(KEY_G)) focus_requested_ = sim::Focus::Growth;
    else if (key_pressed(KEY_X)) focus_requested_ = sim::Focus::Expansion;
    else if (key_pressed(KEY_F)) focus_requested_ = sim::Focus::Foraging;
    if (input.save_chord) save_requested_ = true;
  } else if (!transitioned && scenes_.scene() == Scene::Legacy) {
    if (key_pressed(KEY_ENTER)) flight_requested_ = true;
    if (key_pressed(KEY_V)) trait_requested_ = game::TraitBranch::Vigor;
    if (key_pressed(KEY_Y)) trait_requested_ = game::TraitBranch::Industry;
    if (key_pressed(KEY_C)) new_run_requested_ = true;
  } else if (scenes_.scene() == Scene::Recovery) {
    if (key_pressed(KEY_R)) recover_requested_ = true;
    if (key_pressed(KEY_N)) new_colony_requested_ = true;
  }
  ui_.begin_frame(mouse.x, mouse.y, input.down,
                  !transitioned && input.pressed, !transitioned && input.released, delta_seconds);
  ui_.set_press_origin(input.origin.x, input.origin.y);
  if (input.travel_squared > 16.0F) ui_.cancel_capture();
}

void Renderer::draw(const game::GameView& view, const double interpolation_alpha, const bool paused,
                    const int speed, const bool simulation_limited) {
  // Gait and other world motion run off this clock rather than wall time, so a paused colony is
  // genuinely still: legs stop mid-stride instead of treading air.
  const float delta = paused ? 0.0F : GetFrameTime();
  animation_clock_ += delta;
  observe(view, delta);
  update_selection(view, interpolation_alpha);
  ui_.set_input_region(!scenes_.modal());
  tooltip_title_ = nullptr;
  ClearBackground(kDeepSoil);
  draw_world(view);
  draw_interface(view, paused, speed, simulation_limited);
  if (legacy_panel_open()) draw_legacy_panel(view);
  if (scenes_.scene() == Scene::Recovery) draw_recovery_prompt();
  if (scenes_.scene() == Scene::PauseMenu) draw_pause_menu();
  // Drawn last so a tooltip raised by a modal control sits above the modal that owns it.
  if (tooltip_title_ != nullptr) {
    draw_tooltip(tooltip_title_, tooltip_body_.c_str(), tooltip_x_, tooltip_y_);
  }
}

void Renderer::draw_legacy_panel(const game::GameView& view) {
  const game::LegacyView& legacy = view.legacy;
  const Rectangle panel =
      draw_modal_card(660.0F, 460.0F, legacy.between_runs ? "Between colonies" : "Nuptial flight");
  const int left = static_cast<int>(panel.x) + 28;
  const float right = panel.x + panel.width - 28.0F;
  int line = static_cast<int>(panel.y) + 74;
  ui_.set_input_region(true);

  draw_text(TextFormat("Generation %llu   |   Flights %llu   |   Legacy %lld",
                       static_cast<unsigned long long>(legacy.generation),
                       static_cast<unsigned long long>(legacy.successful_flights),
                       static_cast<long long>(legacy.wallet)),
            left, line, 17, kMutedInk);
  line += 32;

  if (!legacy.between_runs) {
    const game::FlightPreview& flight = legacy.flight;
    draw_text("READINESS", left, line, 14, kMutedInk);
    line += 24;
    const auto requirement = [&](const bool done, const char* text) {
      draw_icon(done ? Icon::Check : Icon::Cross, {static_cast<float>(left), static_cast<float>(line) + 2.0F, 16.0F, 16.0F},
                done ? Color{104, 142, 96, 255} : kMutedInk);
      draw_text(text, left + 24, line, 17, done ? kInk : kMutedInk);
      line += 25;
    };
    requirement(flight.mature, "Mature colony");
    requirement(flight.living_workers >= 100,
                TextFormat("Living workers  %d / 100", flight.living_workers));
    requirement(flight.births >= 150,
                TextFormat("Workers born  %llu / 150", static_cast<unsigned long long>(flight.births)));
    requirement(flight.run_ticks >= 720 * static_cast<unsigned>(sim::kTicksPerSecond),
                TextFormat("Run time  %llu / 720 s",
                           static_cast<unsigned long long>(flight.run_ticks / sim::kTicksPerSecond)));
    requirement(flight.live_winged_queens >= 3,
                TextFormat("Winged queens  %d / 3", flight.live_winged_queens));
    line += 12;
    draw_text(TextFormat("Legacy on flight: %lld   (2 base + %lld births + %lld queens)",
                         static_cast<long long>(flight.payout.total),
                         static_cast<long long>(flight.payout.birth_bonus),
                         static_cast<long long>(flight.payout.queen_bonus)),
              left, line, 18);
    line += 25;
    if (flight.next_birth_threshold > 0) {
      draw_text(TextFormat("Next step at %llu births%s",
                           static_cast<unsigned long long>(flight.next_birth_threshold),
                           flight.next_queen_threshold > 0
                               ? TextFormat(" or %d winged queens", flight.next_queen_threshold)
                               : ""),
                left, line, 15, kMutedInk);
    }
    line += 30;
    if (!flight.eligible) {
      draw_text(game::flight_block_reason(flight.block), left, line, 16, kMutedInk);
    }
    if (button(110, {static_cast<float>(left), panel.y + panel.height - 76.0F, 250.0F, 44.0F},
               "Send the flight", false, flight.eligible, Icon::Flight,
               flight.eligible ? "Ends this colony and pays Legacy (Enter)"
                               : game::flight_block_reason(flight.block))) {
      flight_requested_ = true;
    }
  } else {
    draw_text("PERMANENT TRAITS", left, line, 14, kMutedInk);
    line += 26;
    const auto trait = [&](const std::uint32_t id, const game::TraitBranch branch, const Icon icon,
                           const char* name, const int tier, const std::int64_t cost,
                           const char* effect) {
      const bool maxed = cost < 0;
      const bool affordable = !maxed && legacy.wallet >= cost;
      draw_text(TextFormat("%s  tier %d/4", name, tier), left, line, 18, affordable ? kInk : kMutedInk);
      draw_text(effect, left, line + 23, 14, kMutedInk);
      const char* price = maxed ? "Complete" : TextFormat("%lld Legacy", static_cast<long long>(cost));
      if (button(id, {right - 176.0F, static_cast<float>(line) - 4.0F, 176.0F, 40.0F}, price, false,
                 affordable, icon,
                 maxed ? "Every tier is bought"
                       : affordable ? "Buy this tier permanently" : "Not enough Legacy yet",
                 name)) {
        trait_requested_ = branch;
      }
      line += 62;
    };
    trait(111, game::TraitBranch::Vigor, Icon::Queen, "Vigor", legacy.vigor_tier,
          legacy.vigor_cost, "Faster eggs, more founders, faster brood, thriftier queen");
    trait(112, game::TraitBranch::Industry, Icon::Mandibles, "Industry", legacy.industry_tier,
          legacy.industry_cost, "Faster digging, faster movement, bigger loads, cheaper Work");
    if (button(113, {static_cast<float>(left), panel.y + panel.height - 76.0F, 260.0F, 44.0F},
               "Found the next colony", false, true, Icon::Colony,
               "Starts a fresh colony with the traits you own (C)")) {
      new_run_requested_ = true;
    }
  }

  if (button(114, {right - 116.0F, panel.y + panel.height - 76.0F, 116.0F, 44.0F}, "Close", false,
             true, Icon::Close, "Back to the colony (Esc)")) {
    scenes_.close();
    ui_.cancel_capture();
    world_click_.cancel();
  }
}

std::string Renderer::truncate_to_width(const std::string& text, const float max_width,
                                        const float size) const {
  if (MeasureTextEx(font_, text.c_str(), size, 0.0F).x <= max_width) return text;
  std::string fitted = text;
  while (!fitted.empty() &&
         MeasureTextEx(font_, (fitted + "...").c_str(), size, 0.0F).x > max_width) {
    fitted.pop_back();
  }
  return fitted + "...";
}

void Renderer::draw_recovery_prompt() {
  const Rectangle panel = draw_modal_card(640.0F, 268.0F, "Saved colony could not be read");
  const int left = static_cast<int>(panel.x) + 28;
  int line = static_cast<int>(panel.y) + 78;
  draw_text("The current save file is damaged. It has been left untouched", left, line, 17,
            kMutedInk);
  line += 24;
  draw_text("for diagnosis, and nothing is being saved right now.", left, line, 17, kMutedInk);
  ui_.set_input_region(true);
  if (button(120, {static_cast<float>(left), panel.y + panel.height - 82.0F, 268.0F, 44.0F},
             "Restore last revision", false, true, Icon::Restore,
             "Loads the previous validated save (R)")) {
    recover_requested_ = true;
  }
  if (button(121, {static_cast<float>(left) + 284.0F, panel.y + panel.height - 82.0F, 240.0F, 44.0F},
             "Start a new colony", false, true, Icon::Reset,
             "Keeps the damaged file for diagnosis (N)")) {
    new_colony_requested_ = true;
  }
  if (!status_line_.empty()) {
    const std::string fitted = truncate_to_width(status_line_, panel.width - 56.0F, 15.0F);
    draw_text(fitted.c_str(), left, static_cast<int>(panel.y + panel.height) - 28, 15, kMutedInk);
  }
}

void Renderer::refresh_terrain_texture(const game::GameView& view) {
  // Rooms are painted into the ground rather than drawn over it, so a chamber reads as a lit space
  // and the corridor between two of them stays a passage. They change rarely, so they join the
  // terrain revision in deciding when the bake is stale.
  std::uint64_t rooms_key = 1469598103934665603ULL;
  for (const sim::Room& room : view.rooms) {
    rooms_key = (rooms_key ^ static_cast<std::uint64_t>(room.centre.x * 397 + room.centre.y * 31 +
                                                        room.radius * 7 +
                                                        static_cast<int>(room.kind) * 3 +
                                                        (room.complete ? 1 : 0))) *
                1099511628211ULL;
  }
  if (terrain_texture_ready_ && terrain_revision_ == view.terrain_revision &&
      rooms_key_ == rooms_key) {
    return;
  }
  const double now = GetTime();
  if (terrain_texture_ready_ && now - terrain_built_at_ < 0.15) return;
  terrain_built_at_ = now;

  const int width = view.grid_width * kTerrainDetail;
  const int height = view.grid_height * kTerrainDetail;
  Image image = GenImageColor(width, height, kSky);
  auto* pixels = static_cast<Color*>(image.data);

  const auto material_at = [&view](const int x, const int y) {
    if (x < 0 || y < 0 || x >= view.grid_width || y >= view.grid_height) return sim::Material::Bedrock;
    return view.terrain[static_cast<std::size_t>(y * view.grid_width + x)];
  };
  // 0 corridor, 1 brood room, 2 granary.
  std::vector<std::uint8_t> room_of(
      static_cast<std::size_t>(view.grid_width * view.grid_height), 0);
  for (const sim::Room& room : view.rooms) {
    const int reach = room.reach();
    for (int dy = -reach; dy <= reach; ++dy) {
      for (int dx = -reach; dx <= reach; ++dx) {
        const sim::GridPos cell{room.centre.x + dx, room.centre.y + dy};
        if (cell.x < 0 || cell.y < 0 || cell.x >= view.grid_width || cell.y >= view.grid_height) continue;
        if (!room.contains(cell)) continue;
        room_of[static_cast<std::size_t>(cell.y * view.grid_width + cell.x)] =
            room.kind == sim::RoomKind::Nursery ? 1 : 2;
      }
    }
  }

  for (int cell_y = 0; cell_y < view.grid_height; ++cell_y) {
    // Light falls from the surface, so the ground darkens with depth on top of its own layering.
    const float depth = std::clamp(static_cast<float>(cell_y - 32) / 170.0F, 0.0F, 1.0F);
    const int depth_shade = -static_cast<int>(depth * depth * 22.0F);
    for (int cell_x = 0; cell_x < view.grid_width; ++cell_x) {
      // A gentle darkening toward the left and right rims, so the diorama sits in its frame.
      const int rim = -static_cast<int>(
          std::max(0.0F, 1.0F - static_cast<float>(std::min(cell_x, view.grid_width - 1 - cell_x)) /
                             46.0F) * 16.0F);
      const sim::Material material = material_at(cell_x, cell_y);
      const bool solid = material != sim::Material::Sky && material != sim::Material::Air;
      const bool open_above = !solid && material_at(cell_x, cell_y - 1) != material;
      const bool lit = solid && (material_at(cell_x, cell_y - 1) == sim::Material::Air ||
                                 material_at(cell_x, cell_y - 1) == sim::Material::Sky);
      const bool shadowed = solid && material_at(cell_x, cell_y + 1) == sim::Material::Air;
      // A tunnel is a space, not a hole: it takes a trodden floor where the ground carries it and
      // a little warmth bounced off the walls beside it.
      const auto is_solid = [&](const int x, const int y) {
        const sim::Material at = material_at(x, y);
        return at != sim::Material::Sky && at != sim::Material::Air;
      };
      const bool floored = !solid && is_solid(cell_x, cell_y + 1);
      const int walls = !solid ? (is_solid(cell_x - 1, cell_y) ? 1 : 0) +
                                     (is_solid(cell_x + 1, cell_y) ? 1 : 0) +
                                     (is_solid(cell_x, cell_y - 1) ? 1 : 0)
                               : 0;

      for (int sub_y = 0; sub_y < kTerrainDetail; ++sub_y) {
        for (int sub_x = 0; sub_x < kTerrainDetail; ++sub_x) {
          const int px = cell_x * kTerrainDetail + sub_x;
          const int py = cell_y * kTerrainDetail + sub_y;
          Color color = material_color(material, cell_x, cell_y, view.seed);
          if (solid) {
            const std::uint64_t noise = hash_pixel(px, py, view.seed);
            // Sparse flecks rather than uniform static, so the soil has grain without fizzing.
            const int grain = (noise % 23ULL) == 0 ? 16 : ((noise >> 8U) % 17ULL) == 0 ? -13
                                                                                      : (static_cast<int>((noise >> 16U) % 7ULL) - 3);
            color = shade(color, grain + depth_shade + rim);
            // A root keeps a paler core, which is what makes it read as a strand rather than a slab.
            if (material == sim::Material::Root && sub_x == 1) color = shade(color, 13);
            if (lit && sub_y == 0) color = shade(color, 24);
            if (shadowed && sub_y == kTerrainDetail - 1) color = shade(color, -18);
          } else if (material == sim::Material::Air) {
            const std::uint64_t dust = hash_pixel(px, py, view.seed ^ 0xA1EULL);
            const bool floor_row = floored && sub_y == kTerrainDetail - 1;
            const std::uint8_t room = room_of[static_cast<std::size_t>(cell_y * view.grid_width + cell_x)];
            // A room is a place the colony keeps: its floor is trodden warm and the air above it
            // carries a little of that warmth. A corridor stays plainer.
            const Color chamber = room == 1 ? kBroodRoom : room == 2 ? kGranaryRoom : kTunnel;
            const bool ceiling = sub_y == 0 && is_solid(cell_x, cell_y - 1);
            color = floor_row ? mix(chamber, kTunnelFloor, room == 0 ? 0.62F : 0.78F)
                              : shade(chamber, (ceiling ? -12 : 0) + walls * 3 +
                                                   (open_above && sub_y == 0 ? 8 : 0) +
                                                   static_cast<int>(dust % 5ULL) - 2);
          } else {
            // A calm sky that warms toward the horizon, so the surface reads as lit from above.
            const float toward_horizon = std::clamp(static_cast<float>(py) / 96.0F, 0.0F, 1.0F);
            color = mix(kSkyHigh, kSkyLow, toward_horizon * toward_horizon);
          }
          pixels[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(px)] = color;
        }
      }
    }
  }

  // Cells that opened since the last bake get a puff of thrown soil, which is what makes digging
  // read as work rather than as the ground quietly changing colour.
  if (baked_terrain_.size() == view.terrain.size()) {
    int puffs = 0;
    for (std::size_t index = 0; index < view.terrain.size() && puffs < 24; ++index) {
      if (view.terrain[index] != sim::Material::Air || baked_terrain_[index] == sim::Material::Air) continue;
      const int cell_x = static_cast<int>(index) % view.grid_width;
      const int cell_y = static_cast<int>(index) / view.grid_width;
      for (int grain = 0; grain < 3; ++grain) {
        const std::uint64_t noise = hash_pixel(cell_x * 3 + grain, cell_y, view.seed);
        add_spark({{static_cast<float>(cell_x) + 0.5F, static_cast<float>(cell_y) + 0.5F},
                   {(static_cast<float>(noise % 100ULL) / 100.0F - 0.5F) * 1.6F,
                    -0.3F - static_cast<float>((noise >> 8U) % 60ULL) / 100.0F},
                   0.0F, 0.55F + static_cast<float>((noise >> 16U) % 40ULL) / 100.0F, 1.0F,
                   earth_color(cell_x, cell_y, view.seed), Spark::Kind::Puff});
      }
      ++puffs;
    }
  }
  baked_terrain_ = view.terrain;

  if (terrain_texture_ready_) UnloadTexture(terrain_texture_);
  terrain_texture_ = LoadTextureFromImage(image);
  SetTextureFilter(terrain_texture_, TEXTURE_FILTER_POINT);
  UnloadImage(image);
  terrain_texture_ready_ = true;
  terrain_revision_ = view.terrain_revision;
  rooms_key_ = rooms_key;
}

void Renderer::draw_food_source(const sim::FoodSource& source, const bool recruiting) const {
  const Vector2 ground{static_cast<float>(source.position.x) + 0.5F,
                       static_cast<float>(source.position.y) + 1.0F};
  const float fullness =
      source.capacity <= 0
          ? 0.0F
          : std::clamp(static_cast<float>(source.amount) / static_cast<float>(source.capacity),
                       0.0F, 1.0F);
  const std::uint64_t noise = hash_pixel(source.position.x, source.position.y, 0x50D1CEULL);
  if (!source.known) {
    // The colony has not found it yet. Shown as a hint rather than a landmark, so watching a scout
    // stumble onto it is the moment it becomes real.
    const float hint = 0.9F + fullness * 1.2F;
    DrawCircleLinesV({ground.x, ground.y - hint * 0.6F}, hint,
                     Fade(source.nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein,
                          0.35F));
    return;
  }
  if (recruiting) {
    // A site the colony is calling everyone to, pulsing while the alert lasts.
    const float beat = 0.5F + 0.5F * std::sin(animation_clock_ * 4.0F);
    for (int ring = 1; ring <= 2; ++ring) {
      DrawCircleLinesV({ground.x, ground.y - 1.2F}, 2.6F + static_cast<float>(ring) * 1.5F + beat,
                       Fade(kSelection, 0.30F - static_cast<float>(ring) * 0.09F));
    }
  }

  if (source.nutrient == sim::Nutrient::Carbohydrate) {
    // A fallen berry with a spill of seed husks around it: the sweet, replenishing staple.
    const float radius = 0.9F + fullness * 1.5F;
    draw_oriented_ellipse(ground, radius * 1.5F, 0.45F, 0.0F, Fade(BLACK, 0.20F));
    const int grains = 4 + static_cast<int>(fullness * 12.0F);
    for (int grain = 0; grain < grains; ++grain) {
      const std::uint64_t spread = hash_pixel(grain, source.position.x, noise);
      const float offset = static_cast<float>(spread % 100ULL) / 100.0F - 0.5F;
      const float depth = static_cast<float>((spread >> 12U) % 100ULL) / 100.0F;
      const Vector2 seed{ground.x + offset * (2.0F + radius * 2.2F), ground.y - 0.16F - depth * 0.5F};
      draw_oriented_ellipse(seed, 0.30F, 0.17F, offset * 1.6F,
                            shade(kCarbohydrate, static_cast<int>((spread >> 20U) % 30ULL) - 15));
    }
    DrawCircleV({ground.x, ground.y - radius * 0.72F}, radius, kBerry);
    DrawCircleV({ground.x - radius * 0.3F, ground.y - radius * 1.05F}, radius * 0.32F,
                shade(kBerry, 46));
    DrawLineEx({ground.x + radius * 0.2F, ground.y - radius * 1.5F},
               {ground.x + radius * 0.75F, ground.y - radius * 2.2F}, 0.2F, kFoliage);
    return;
  }

  // A dead beetle: the protein prize, picked apart as the colony carries it away.
  const float body = 0.7F + fullness * 1.4F;
  draw_oriented_ellipse(ground, body * 1.7F, 0.45F, 0.0F, Fade(BLACK, 0.20F));
  const Vector2 centre{ground.x, ground.y - body * 0.62F};
  for (int side = -1; side <= 1; side += 2) {
    const float sign = static_cast<float>(side);
    for (int leg = 0; leg < 3; ++leg) {
      const float along = (static_cast<float>(leg) - 1.0F) * body * 0.55F;
      DrawLineEx({centre.x + along, centre.y},
                 {centre.x + along + sign * body * 0.9F, ground.y - 0.1F}, 0.18F, kHusk);
    }
  }
  draw_oriented_ellipse(centre, body * 1.25F, body * 0.72F, 0.0F, kProtein);
  draw_oriented_ellipse({centre.x, centre.y - body * 0.2F}, body * 0.9F, body * 0.34F, 0.0F,
                        shade(kProtein, 26));
  DrawLineEx({centre.x - body * 1.2F, centre.y}, {centre.x + body * 1.2F, centre.y}, 0.16F,
             shade(kProtein, -34));
  draw_oriented_ellipse({centre.x - body * 1.25F, centre.y}, body * 0.42F, body * 0.4F, 0.0F,
                        shade(kProtein, -22));
}

void Renderer::draw_granary(const game::GameView& view) const {
  const float zoom = camera_.zoom();
  for (const sim::FoodPile& pile : view.granary) {
    if (pile.amount <= 0) continue;
    const Color color = pile.nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein;
    const float fullness = std::clamp(
        static_cast<float>(pile.amount) / static_cast<float>(sim::kGrainsPerStoreCell), 0.0F, 1.0F);
    const float x = static_cast<float>(pile.position.x) + 0.5F;
    const float floor = static_cast<float>(pile.position.y) + 0.98F;
    if (zoom < 3.0F) {
      // Too small for grain: a band on the chamber floor that rises as the heap fills.
      const float height = 0.25F + fullness * 0.7F;
      DrawRectangleV({x - 0.5F, floor - height}, {1.0F, height}, Fade(color, 0.9F));
      continue;
    }
    // Heaps are drawn wider than their cell so a stocked chamber reads as one bank of grain
    // rather than a tiled row of identical little pyramids.
    const std::uint64_t noise = hash_pixel(pile.position.x, pile.position.y, 0x6A1EULL);
    const float lean = (static_cast<float>(noise % 100ULL) / 100.0F - 0.5F) * 0.34F;
    const float reach = 0.56F + fullness * 0.14F + static_cast<float>((noise >> 7U) % 9ULL) * 0.012F;
    const float height = 0.30F + fullness * 0.60F;
    const Color tone = shade(color, static_cast<int>((noise >> 13U) % 17ULL) - 8);
    std::array<Vector2, 11> mound{};
    mound[0] = {x + lean, floor};
    for (std::size_t step = 1; step < mound.size(); ++step) {
      const float t = static_cast<float>(step - 1) / static_cast<float>(mound.size() - 2);
      // A peak that is off centre reads as a heap somebody piled, not a moulded cone.
      const float shape = std::pow(std::sin(t * PI), 0.8F);
      mound[step] = {x + lean - reach + 2.0F * reach * t, floor - shape * height};
    }
    // Screen y grows downward, so the fan has to be wound the other way to survive culling.
    std::reverse(mound.begin() + 1, mound.end());
    DrawTriangleFan(mound.data(), static_cast<int>(mound.size()), color);
    if (zoom < 6.0F) continue;
    const int grains = 2 + static_cast<int>(fullness * 3.0F);
    for (int grain = 0; grain < grains; ++grain) {
      const std::uint64_t speck = hash_pixel(grain, pile.position.x, noise);
      const float across = (static_cast<float>(speck % 100ULL) / 100.0F - 0.5F) * reach * 1.4F;
      const float lift = std::pow(std::sin((across / reach + 1.0F) * 0.5F * PI), 0.8F) * height;
      draw_oriented_ellipse({x + lean + across, floor - lift * 0.66F},
                            0.16F, 0.12F, static_cast<float>((speck >> 17U) % 30ULL) * 0.1F,
                            shade(tone, 16 + static_cast<int>((speck >> 24U) % 12ULL)));
    }
  }
}

void Renderer::note(std::string text, const Icon icon) {
  // The same thing happening again refreshes its line and moves it to the top, rather than filling
  // the list with copies of whatever the colony does most often.
  const auto seen = std::find_if(log_.begin(), log_.end(),
                                 [&text](const LogEntry& entry) { return entry.text == text; });
  if (seen != log_.end()) {
    LogEntry refreshed = *seen;
    refreshed.age = 0.0F;
    log_.erase(seen);
    log_.push_front(std::move(refreshed));
    return;
  }
  log_.push_front({std::move(text), icon, 0.0F});
  while (log_.size() > 5) log_.pop_back();
}

void Renderer::add_spark(Spark spark) {
  // Bounded: a busy colony must never turn into a particle storm.
  if (sparks_.size() >= 220) return;
  sparks_.push_back(spark);
}

void Renderer::observe(const game::GameView& view, const float delta) {
  for (Spark& spark : sparks_) spark.age += delta;
  sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                               [](const Spark& spark) { return spark.age >= spark.life; }),
                sparks_.end());
  for (LogEntry& entry : log_) entry.age += delta;

  const game::GameView::Watched now = view.watched();
  const Vector2 nest{static_cast<float>(view.home.x) + 0.5F, static_cast<float>(view.home.y) + 0.5F};
  if (!observed_) {
    // The first frame of a colony is not news; it is just the colony.
    observed_ = true;
    watched_ = now;
    pile_amounts_.clear();
    for (const sim::FoodPile& pile : view.granary) pile_amounts_.push_back(pile.amount);
    return;
  }

  if (now.workers_born > watched_.workers_born) {
    for (std::uint64_t born = 0; born < std::min<std::uint64_t>(3, now.workers_born - watched_.workers_born); ++born) {
      add_spark({nest, {0.0F, -1.1F}, 0.0F, 1.1F, 1.0F, kBrood, Spark::Kind::Bloom});
    }
    note("A worker emerged", Icon::Worker);
  }
  if (now.gynes_born > watched_.gynes_born) {
    add_spark({nest, {0.0F, -1.4F}, 0.0F, 1.6F, 1.6F, Color{226, 214, 232, 255}, Spark::Kind::Bloom});
    note("A winged queen emerged", Icon::Flight);
  }
  if (now.sources_found > watched_.sources_found) note("Scouts found food", Icon::Seed);
  if (now.sources_exhausted > watched_.sources_exhausted) note("A food site ran dry", Icon::Prey);
  if (now.rooms_built > watched_.rooms_built) note("Started a new chamber", Icon::Colony);
  if (now.complete_rooms > watched_.complete_rooms) {
    for (const sim::Room& room : view.rooms) {
      if (!room.complete) continue;
      add_spark({{static_cast<float>(room.centre.x) + 0.5F, static_cast<float>(room.centre.y) + 0.5F},
                 {0.0F, 0.0F}, 0.0F, 1.4F, static_cast<float>(room.radius), kSelection,
                 Spark::Kind::Ring});
    }
    note("A chamber is finished", Icon::Colony);
  }
  for (std::size_t index = 0; index < now.upgrade_levels.size(); ++index) {
    if (now.upgrade_levels[index] > watched_.upgrade_levels[index]) {
      note("The colony adapted", Icon::Mandibles);
    }
  }
  watched_ = now;

  // Grain set down on a heap. Piles are append-only, so the same index is the same cell.
  pile_amounts_.resize(view.granary.size(), 0);
  for (std::size_t index = 0; index < view.granary.size(); ++index) {
    const sim::FoodPile& pile = view.granary[index];
    if (pile.amount > pile_amounts_[index]) {
      add_spark({{static_cast<float>(pile.position.x) + 0.5F,
                  static_cast<float>(pile.position.y) + 0.4F},
                 {0.0F, -0.5F}, 0.0F, 0.7F, 1.0F,
                 pile.nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein,
                 Spark::Kind::Grain});
    }
    pile_amounts_[index] = pile.amount;
  }
}

void Renderer::draw_sparks() const {
  for (const Spark& spark : sparks_) {
    const float life = std::clamp(spark.age / spark.life, 0.0F, 1.0F);
    const float fade = 1.0F - life;
    const Vector2 at{spark.position.x + spark.drift.x * life,
                     spark.position.y + spark.drift.y * life};
    switch (spark.kind) {
    case Spark::Kind::Puff:
      // Loose soil thrown out of a fresh cut, settling as it fades.
      DrawCircleV({at.x, at.y + life * life * 0.7F}, (0.18F + life * 0.42F) * spark.size,
                  Fade(spark.tint, 0.5F * fade));
      break;
    case Spark::Kind::Grain:
      DrawCircleV(at, 0.22F * fade * spark.size, Fade(spark.tint, 0.9F * fade));
      break;
    case Spark::Kind::Bloom:
      DrawCircleLinesV(at, (0.5F + life * 1.6F) * spark.size, Fade(spark.tint, 0.8F * fade));
      break;
    case Spark::Kind::Ring:
      DrawCircleLinesV(at, spark.size * (0.6F + life * 0.7F), Fade(spark.tint, 0.55F * fade));
      break;
    }
  }
}

void Renderer::draw_world(const game::GameView& view) {
  refresh_terrain_texture(view);
  BeginMode2D(camera_.camera());

  if (terrain_texture_ready_) {
    DrawTexturePro(terrain_texture_,
                   {0.0F, 0.0F, static_cast<float>(terrain_texture_.width),
                    static_cast<float>(terrain_texture_.height)},
                   {0.0F, 0.0F, static_cast<float>(view.grid_width),
                    static_cast<float>(view.grid_height)},
                   {0.0F, 0.0F}, 0.0F, WHITE);
  }

  // Grass tufts: a few blades per clump, leaning and varying in tone. Blades sit on whatever the
  // real surface height is, so they grow on top of the spoil mound instead of being buried in it.
  for (int x = 0; x < view.grid_width; ++x) {
    const std::uint64_t noise = hash_pixel(x, 3, view.seed);
    if (noise % 3ULL != 0) continue;
    int surface = 32;
    for (int y = 8; y < 40; ++y) {
      const sim::Material material = view.terrain[static_cast<std::size_t>(y * view.grid_width + x)];
      if (material != sim::Material::Sky && material != sim::Material::Air) { surface = y; break; }
    }
    const float ground = static_cast<float>(surface);
    const int blades = 2 + static_cast<int>((noise >> 6U) % 3ULL);
    for (int blade = 0; blade < blades; ++blade) {
      const float offset = static_cast<float>((noise >> (8U + 3U * static_cast<unsigned>(blade))) % 9ULL) * 0.1F;
      const float height = 0.9F + static_cast<float>((noise >> (5U + static_cast<unsigned>(blade))) % 6ULL) * 0.22F;
      const float lean = (static_cast<float>((noise >> (11U + static_cast<unsigned>(blade))) % 7ULL) - 3.0F) * 0.14F;
      const Color tone = shade(kFoliage, static_cast<int>((noise >> (13U + static_cast<unsigned>(blade))) % 25ULL) - 12);
      DrawLineEx({static_cast<float>(x) + offset, ground},
                 {static_cast<float>(x) + offset + lean, ground - height}, 0.28F, tone);
    }
  }

  // The nursery reads as a softly lit hollow rather than a drawn-on marker.
  const Vector2 nest{static_cast<float>(view.home.x) + 0.5F, static_cast<float>(view.home.y) + 0.5F};
  for (int ring = 4; ring >= 1; --ring) {
    DrawCircleV(nest, 1.6F * static_cast<float>(ring), Fade(Color{92, 74, 54, 255}, 0.06F));
  }

  for (const sim::FoodSource& source : view.sources) {
    draw_food_source(source, source.id == view.recruiting_source);
  }
  draw_granary(view);

  for (const sim::BroodSnapshot& brood : view.brood) {
    Vector2 center{static_cast<float>(brood.position.x) + 0.5F,
                   static_cast<float>(brood.position.y) + 0.5F};
    if (brood.carried_by != 0) {
      // Held brood rides on the nurse's interpolated pose, so it travels smoothly with her rather
      // than hopping from cell to cell.
      const auto carrier = std::find_if(poses_.begin(), poses_.end(),
          [&brood](const ActorPose& pose) { return pose.id == brood.carried_by; });
      if (carrier == poses_.end()) continue;
      center = {carrier->centre.x, carrier->centre.y - 0.9F};
    }
    const Color shell = brood.role == sim::BroodRole::Gyne ? Color{226, 214, 232, 255} : kBrood;
    DrawCircleV({center.x + 0.1F, center.y + 0.14F}, 0.62F, Fade(BLACK, 0.28F));
    if (brood.stage == sim::BroodStage::Egg) {
      draw_oriented_ellipse(center, 0.34F, 0.24F, 0.5F, shell);
    } else if (brood.stage == sim::BroodStage::Larva) {
      draw_oriented_ellipse(center, 0.62F, 0.34F, 0.35F, shell);
      draw_oriented_ellipse({center.x - 0.28F, center.y - 0.1F}, 0.26F, 0.22F, 0.35F,
                            shade(shell, -18));
    } else {
      draw_oriented_ellipse(center, 0.72F, 0.4F, 0.2F, shell);
      DrawLineEx({center.x - 0.3F, center.y - 0.22F}, {center.x + 0.35F, center.y - 0.12F}, 0.1F,
                 shade(shell, -30));
    }
  }

  for (const sim::CorpseSnapshot& corpse : view.corpses) {
    const Vector2 center{static_cast<float>(corpse.position.x) + 0.5F,
                         static_cast<float>(corpse.position.y) + 0.5F};
    DrawLineEx({center.x - 0.8F, center.y - 0.6F}, {center.x + 0.8F, center.y + 0.6F}, 0.25F,
               Color{124, 102, 82, 255});
    DrawLineEx({center.x + 0.8F, center.y - 0.6F}, {center.x - 0.8F, center.y + 0.6F}, 0.25F,
               Color{124, 102, 82, 255});
  }

  for (const sim::DroppedCargoSnapshot& dropped : view.dropped_food) {
    const Vector2 center{static_cast<float>(dropped.position.x) + 0.5F,
                         static_cast<float>(dropped.position.y) + 0.5F};
    const Color color = dropped.nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein;
    DrawCircleV(center, 0.65F, color);
    DrawCircleLinesV(center, 0.85F, kPaper);
  }

  // Dead ants never reuse an id, so the heading cache would creep upward over a long session.
  // Dropping it wholesale is safe: a moving ant re-derives its heading on the very next frame.
  if (heading_.size() > view.actors.size() * 4 + 64) heading_.clear();

  // Drawn and picked poses are the same list, indexed together, so the two cannot drift apart.
  for (std::size_t index = 0; index < view.actors.size(); ++index) {
    const sim::ActorSnapshot& actor = view.actors[index];
    if (actor.kind == sim::AntKind::Queen && !view.queen_alive) {
      continue;
    }
    draw_ant(actor, poses_[index], camera_.zoom(), selection_.id() == actor.id);
  }
  draw_sparks();

  EndMode2D();
}

float Renderer::heading_for(const sim::ActorSnapshot& actor, const float dx, const float dy) {
  const float travelled = dx * dx + dy * dy;
  auto stored = heading_.find(actor.id);
  if (travelled > 1e-6F) {
    const float heading = std::atan2(dy, dx);
    if (stored == heading_.end()) heading_.emplace(actor.id, heading);
    else stored->second = heading;
    return heading;
  }
  if (stored != heading_.end()) return stored->second;
  // A never-moved ant still faces somewhere definite, and always the same somewhere.
  const float heading = static_cast<float>(actor.id % 16ULL) * (2.0F * PI / 16.0F);
  heading_.emplace(actor.id, heading);
  return heading;
}

void Renderer::draw_ant(const sim::ActorSnapshot& actor, const ActorPose& pose,
                        const float zoom, const bool selected) {
  const float heading = heading_for(actor, static_cast<float>(actor.x - actor.previous_x),
                                    static_cast<float>(actor.y - actor.previous_y));
  const bool winged = actor.kind == sim::AntKind::WingedQueen;
  const bool royal = actor.kind == sim::AntKind::Queen;
  const float scale = pose.scale;
  const Color body = royal || winged ? kQueenChitin : kChitin;
  // One phase drives the whole ant: the tripod gait, the bob it makes, and the antennae casting
  // about. Phased per ant so a column does not march in lockstep.
  const float phase = animation_clock_ * 7.0F + static_cast<float>(actor.id % 13ULL) * 0.48F;
  const bool walking = std::abs(actor.x - actor.previous_x) + std::abs(actor.y - actor.previous_y) >
                       1e-4;
  const float bob = walking ? std::sin(phase * 2.0F) * 0.055F * scale : 0.0F;
  const Vector2 centre{pose.centre.x, pose.centre.y + bob};

  if (selected) {
    // A ring that breathes, so the selected ant stays findable in a crowd.
    const float pulse = 0.5F + 0.5F * std::sin(animation_clock_ * 3.0F);
    DrawCircleLinesV(centre, (2.7F + pulse * 0.45F) * scale, kSelection);
    DrawCircleLinesV(centre, 2.4F * scale, Fade(kSelection, 0.45F));
  }

  // Far out an ant is a moving dot; the cargo accent is what stays readable. These thresholds are
  // in logical pixels per cell, so they hold on any display scale.
  if (zoom < 1.8F) {
    DrawCircleV(centre, 0.8F * scale, body);
    if (actor.cargo_amount > 0 && actor.cargo_kind == sim::CargoKind::Food) {
      DrawCircleV(offset_along(centre, heading, 0.9F * scale, 0.0F), 0.45F * scale,
                  actor.cargo_nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein);
    }
    return;
  }

  const bool detailed = zoom >= 3.0F;
  const Vector2 gaster = offset_along(centre, heading, -0.95F * scale, 0.0F);
  const Vector2 thorax = offset_along(centre, heading, 0.05F * scale, 0.0F);
  const Vector2 head = offset_along(centre, heading, 0.85F * scale, 0.0F);

  if (detailed) {
    for (int leg = 0; leg < 3; ++leg) {
      const float root_along = (0.45F - static_cast<float>(leg) * 0.42F) * scale;
      for (int side = -1; side <= 1; side += 2) {
        const float swing =
            std::sin(phase + ((leg + (side > 0 ? 1 : 0)) % 2 == 0 ? 0.0F : PI)) * 0.34F * scale;
        const Vector2 root = offset_along(centre, heading, root_along, 0.22F * scale * static_cast<float>(side));
        const Vector2 knee = offset_along(centre, heading, root_along + swing * 0.6F,
                                          0.85F * scale * static_cast<float>(side));
        const Vector2 foot = offset_along(centre, heading, root_along + swing,
                                          1.35F * scale * static_cast<float>(side));
        const Color limb = shade(body, -20);
        DrawLineEx(root, knee, 0.11F * scale, limb);
        DrawLineEx(knee, foot, 0.09F * scale, limb);
      }
    }
  }

  if (winged) {
    // Two long wings swept back over the gaster, translucent so the body still reads.
    for (int side = -1; side <= 1; side += 2) {
      const Vector2 wing = offset_along(centre, heading, -0.7F * scale, 0.85F * scale * static_cast<float>(side));
      draw_oriented_ellipse(wing, 1.5F * scale, 0.42F * scale,
                            heading + 0.42F * static_cast<float>(side), kWing);
    }
  }

  draw_oriented_ellipse(gaster, 0.82F * scale, 0.6F * scale, heading, body);
  draw_oriented_ellipse(offset_along(gaster, heading, -0.06F * scale, -0.12F * scale),
                        0.5F * scale, 0.26F * scale, heading, shade(body, 26));
  // Petiole: the waist that makes it an ant rather than a beetle.
  DrawLineEx(offset_along(centre, heading, -0.42F * scale, 0.0F),
             offset_along(centre, heading, -0.2F * scale, 0.0F), 0.2F * scale, body);
  draw_oriented_ellipse(thorax, 0.52F * scale, 0.38F * scale, heading, body);
  draw_oriented_ellipse(offset_along(thorax, heading, 0.05F * scale, -0.1F * scale), 0.3F * scale,
                        0.16F * scale, heading, shade(body, 30));
  draw_oriented_ellipse(head, 0.42F * scale, 0.4F * scale, heading, body);

  if (detailed) {
    for (int side = -1; side <= 1; side += 2) {
      // Antennae cast about as the ant walks; they are the part that makes it look alive.
      const float sweep = std::sin(phase * 0.8F + static_cast<float>(side)) * 0.16F * scale;
      const Vector2 base = offset_along(head, heading, 0.25F * scale, 0.16F * scale * static_cast<float>(side));
      const Vector2 elbow = offset_along(head, heading, 0.75F * scale, 0.5F * scale * static_cast<float>(side) + sweep * 0.4F);
      const Vector2 tip = offset_along(head, heading, 1.15F * scale + sweep,
                                       0.42F * scale * static_cast<float>(side) + sweep);
      DrawLineEx(base, elbow, 0.09F * scale, shade(body, -20));
      DrawLineEx(elbow, tip, 0.08F * scale, shade(body, -20));
    }
    // Mandibles.
    for (int side = -1; side <= 1; side += 2) {
      DrawLineEx(offset_along(head, heading, 0.3F * scale, 0.2F * scale * static_cast<float>(side)),
                 offset_along(head, heading, 0.62F * scale, 0.1F * scale * static_cast<float>(side)),
                 0.1F * scale, shade(body, 18));
    }
    DrawCircleV(offset_along(head, heading, 0.16F * scale, 0.24F * scale), 0.09F * scale,
                kChitinSheen);
    DrawCircleV(offset_along(head, heading, 0.16F * scale, -0.24F * scale), 0.09F * scale,
                kChitinSheen);
  }

  // Cargo is carried at the mandibles, in front of the head, not floating overhead.
  if (actor.cargo_amount > 0) {
    const Vector2 held = offset_along(head, heading, 0.62F * scale, 0.0F);
    switch (actor.cargo_kind) {
    case sim::CargoKind::Food: {
      const Color cargo_color =
          actor.cargo_nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein;
      if (actor.cargo_nutrient == sim::Nutrient::Carbohydrate) {
        DrawCircleV(held, 0.5F * scale, cargo_color);
        DrawCircleV(offset_along(held, heading, 0.12F * scale, -0.12F * scale), 0.18F * scale,
                    shade(cargo_color, 30));
      } else {
        DrawPoly(held, 4, 0.58F * scale, heading * RAD2DEG + 45.0F, cargo_color);
      }
      break;
    }
    case sim::CargoKind::Spoil:
      DrawPoly(held, 6, 0.5F * scale, heading * RAD2DEG, Color{126, 91, 58, 255});
      DrawPolyLines(held, 6, 0.52F * scale, heading * RAD2DEG, Color{96, 68, 44, 255});
      break;
    case sim::CargoKind::Corpse:
      draw_oriented_ellipse(held, 0.55F * scale, 0.32F * scale, heading, Color{124, 102, 82, 255});
      break;
    case sim::CargoKind::None:
      break;
    }
  }
}

void Renderer::draw_interface(const game::GameView& view, const bool paused, const int speed,
                              const bool simulation_limited) {
  const int width = GetScreenWidth();
  const int worker_count = static_cast<int>(std::count_if(
      view.actors.begin(), view.actors.end(), [](const sim::ActorSnapshot& actor) {
        return actor.kind == sim::AntKind::Worker;
      }));
  DrawRectangle(0, 0, width, 72, kPaperSoft);
  DrawRectangle(0, 68, width, 4, Color{112, 132, 87, 255});
  draw_text("ANT FARM", 22, 15, 27);
  draw_text("LIVING COLONY", 23, 44, 13, kMutedInk);
  const int metric_start = width >= 1180 ? 186 : 168;
  struct Metric { Icon icon; const char* label; std::string value; };
  std::vector<Metric> metrics{
      {Icon::Worker, "WORKERS", std::to_string(worker_count)},
      {Icon::Egg, "BROOD", std::to_string(view.brood.size())},
      {Icon::Seed, "CARBS", TextFormat("%lld", static_cast<long long>(view.stores.carbohydrate / 1000))},
      {Icon::Prey, "PROTEIN", TextFormat("%lld", static_cast<long long>(view.stores.protein / 1000))},
      {Icon::Mandibles, "WORK", std::to_string(view.work)}};
  // Winged queens and Legacy only appear once they mean something, so an early colony is not shown
  // counters it cannot act on.
  if (view.legacy.flight.live_winged_queens > 0) {
    metrics.push_back({Icon::Flight, "WINGED", std::to_string(view.legacy.flight.live_winged_queens)});
  }
  if (view.legacy.wallet > 0 || view.legacy.successful_flights > 0) {
    metrics.push_back({Icon::Queen, "LEGACY", std::to_string(view.legacy.wallet)});
  }
  // The colony's live condition sits at the right of the header, where it is readable even with the
  // inspector closed. The counters take the space that leaves.
  const char* headline = view.extinct      ? "The colony is still"
                         : view.decline    ? "The queen is gone"
                         : view.recruiting_source != 0 ? "New food found"
                         : !view.knows_any_food        ? "Scouts are searching"
                         : view.brood.size() >= static_cast<std::size_t>(view.nursery_capacity)
                             ? "Brood rooms are full"
                         : view.stores.protein < 10'000 ? "Protein is low"
                                                        : "Growing steadily";
  const bool brood_full = view.brood.size() >= static_cast<std::size_t>(view.nursery_capacity);
  const bool alarm = view.decline || view.extinct;
  const bool caution = !alarm && (brood_full || view.stores.protein < 10'000 || !view.knows_any_food);
  const Icon headline_icon = alarm                        ? Icon::Cross
                             : view.recruiting_source != 0 ? Icon::Seed
                             : !view.knows_any_food        ? Icon::Trail
                             : brood_full                  ? Icon::Nursery
                             : view.stores.protein < 10'000 ? Icon::Prey
                                                            : Icon::Check;
  const Vector2 headline_size = MeasureTextEx(font_, headline, 16.0F, 0.0F);
  const float chip_width = headline_size.x + 46.0F;
  const float chip_x = static_cast<float>(width) - chip_width - 18.0F;
  const int metric_width =
      std::clamp((static_cast<int>(chip_x) - metric_start - 24) / static_cast<int>(metrics.size()),
                 92, 158);
  for (std::size_t index = 0; index < metrics.size(); ++index) {
    const int x = metric_start + static_cast<int>(index) * metric_width;
    draw_icon(metrics[index].icon, {static_cast<float>(x), 13.0F, 13.0F, 13.0F}, kMutedInk);
    draw_text(metrics[index].label, x + 17, 14, 12, kMutedInk);
    draw_text(metrics[index].value.c_str(), x, 32, 23);
  }
  if (chip_x > static_cast<float>(metric_start + metric_width * static_cast<int>(metrics.size()))) {
    const Rectangle chip{chip_x, 16.0F, chip_width, 34.0F};
    const Color edge = alarm ? kProtein : caution ? Color{192, 156, 96, 255} : kWarmLine;
    const Color ink = alarm ? kProtein : kInk;
    DrawRectangleRounded(chip, 0.42F, 8, alarm ? Color{242, 224, 216, 255}
                                        : caution ? Color{247, 238, 213, 255} : kPaper);
    DrawRectangleRoundedLinesEx(chip, 0.42F, 8, 1.0F, edge);
    draw_icon(headline_icon, {chip.x + 13.0F, chip.y + 10.0F, 14.0F, 14.0F},
              alarm ? kProtein : caution ? Color{150, 118, 62, 255} : kMutedInk);
    draw_text(headline, static_cast<int>(chip.x) + 33, static_cast<int>(chip.y) + 8, 16, ink);
  }

  const float footer_y = layout_.footer.y;
  DrawRectangle(0, static_cast<int>(footer_y), width, 88, kPaperSoft);
  DrawLine(0, static_cast<int>(footer_y), width, static_cast<int>(footer_y), kWarmLine);
  if (button(1, {18, footer_y + 8, 108, 40}, paused ? "Resume" : "Pause", paused, true,
             paused ? Icon::Play : Icon::Pause, "Space"))
    toggle_pause_requested_ = true;
  if (button(2, {138, footer_y + 8, 52, 40}, "", speed == 1, true, Icon::SpeedOne,
             "Real time (key 1)")) speed_requested_ = 1;
  if (button(3, {196, footer_y + 8, 52, 40}, "", speed == 5, true, Icon::SpeedFast,
             "Five times speed (key 2)")) speed_requested_ = 5;
  if (button(4, {254, footer_y + 8, 52, 40}, "", speed == 20, true, Icon::SpeedFastest,
             "Twenty times speed (key 3)")) speed_requested_ = 20;
  if (button(5, {320, footer_y + 8, 92, 40}, "Save", false, true, Icon::Save, "Save now (S)"))
    save_requested_ = true;
  const unsigned long long elapsed = view.tick / sim::kTicksPerSecond;
  draw_text(TextFormat("Generation %llu", static_cast<unsigned long long>(view.legacy.generation)),
            428, static_cast<int>(footer_y) + 10, 18);
  draw_text(TextFormat("%llu:%02llu in the ground", elapsed / 60, elapsed % 60), 428,
            static_cast<int>(footer_y) + 30, 15, kMutedInk);
  if (button(6, {static_cast<float>(width - 232), footer_y + 8, 108, 40}, "Flight", false, true,
             Icon::Flight, "Nuptial flight and Legacy (L)")) {
    scenes_.open_legacy();
    ui_.cancel_capture();
    world_click_.cancel();
    ui_.set_input_region(false);
  }
  if (button(7, {static_cast<float>(width - 112), footer_y + 8, 94, 40}, "Colony", inspector_open_,
             true, Icon::Colony, "Show or hide the inspector (I)")) {
    // Apply next frame, together with camera/layout and input ownership.
    inspector_toggle_requested_ = true;
  }
  const std::string status = status_line_.empty()
      ? "Left click selects | Right/middle drag pans | Scroll zooms | Esc menu"
      : status_line_;
  draw_text(truncate_to_width(status, static_cast<float>(width - 36), 16).c_str(),
            18, static_cast<int>(footer_y) + 61, 16, kMutedInk);

  if (simulation_limited) {
    DrawRectangle(width / 2 - 110, 66, 220, 34, Color{238, 228, 204, 235});
    DrawRectangleLines(width / 2 - 110, 66, 220, 34, kProtein);
    draw_text("Simulation limited", width / 2 - 86, 72, 17);
  }

  if (!inspector_open_) {
    return;
  }
  const int panel_width = width >= 1120 ? 320 : 300;
  const int panel_x = width - panel_width;
  DrawRectangle(panel_x - 7, 72, 7, static_cast<int>(layout_.panel.height), Color{37, 45, 40, 35});
  DrawRectangle(panel_x, 72, panel_width, static_cast<int>(layout_.panel.height), kPaperSoft);
  draw_text("COLONY", panel_x + 22, 92, 15, kMutedInk);
  if (layout_.panel_scroll_range() > 0.0F)
    draw_text("Scroll for more", width - 134, 92, 14, kMutedInk);
  const auto py = [this](const int y) { return y - static_cast<int>(panel_scroll_); };
  BeginScissorMode(static_cast<int>(layout_.panel_body.x), static_cast<int>(layout_.panel_body.y),
                   static_cast<int>(layout_.panel_body.width), static_cast<int>(layout_.panel_body.height));
  ui_.set_input_region(!scenes_.modal(), layout_.panel_body);
  const char* condition = view.extinct ? "The colony is still" : view.decline ? "The queen is gone" :
                          view.recruiting_source != 0 ? "A new food source was found" :
                          !view.knows_any_food ? "Scouts are out searching" :
                          view.brood.size() >= static_cast<std::size_t>(view.nursery_capacity) ? "Brood rooms are full" :
                          view.stores.protein < 10'000 ? "Protein is running low" : "Growing steadily";
  draw_text(condition, panel_x + 22, py(116), 22, view.decline ? kProtein : kInk);
  const int nurseries = static_cast<int>(std::count_if(view.rooms.begin(), view.rooms.end(),
      [](const sim::Room& room) { return room.kind == sim::RoomKind::Nursery; }));
  const int granaries = static_cast<int>(view.rooms.size()) - nurseries;
  draw_text(TextFormat("%d workers  |  %d brood room%s, %d granar%s", worker_count, nurseries,
                       nurseries == 1 ? "" : "s", granaries, granaries == 1 ? "y" : "ies"),
            panel_x + 22, py(142), 16, kMutedInk);
  draw_text(game::bottleneck_name(view.bottleneck), panel_x + 22, py(162), 14, kMutedInk);

  const Rectangle nursery_card{static_cast<float>(panel_x + 16), static_cast<float>(py(182)),
                                static_cast<float>(panel_width - 32), 64.0F};
  DrawRectangleRounded(nursery_card, 0.12F, 6, kPaper);
  DrawRectangleRoundedLinesEx(nursery_card, 0.12F, 6, 1.0F, kWarmLine);
  draw_icon(Icon::Nursery, {nursery_card.x + 13.0F, nursery_card.y + 10.0F, 12.0F, 12.0F}, kMutedInk);
  draw_text("BROOD ROOMS", panel_x + 48, py(192), 13, kMutedInk);
  draw_text(TextFormat("%zu of %d cradles", view.brood.size(), view.nursery_capacity),
            panel_x + 30, py(210), 18);
  const float ratio = view.nursery_capacity == 0 ? 0.0F : std::min(1.0F, static_cast<float>(view.brood.size()) / static_cast<float>(view.nursery_capacity));
  DrawRectangle(panel_x + 30, py(233), panel_width - 60, 6, Color{214, 200, 170, 255});
  DrawRectangle(panel_x + 30, py(233), static_cast<int>(static_cast<float>(panel_width - 60) * ratio), 6, kFoliage);

  draw_readiness(view, panel_x, panel_width, py(258));

  draw_text("COLONY ACTIVITY", panel_x + 22, py(312), 14, kMutedInk);
  draw_text(TextFormat("Forage %u   Dig %u   Nurse %u", view.tasks.workers_by_task[0],
                       view.tasks.workers_by_task[1], view.tasks.workers_by_task[2]),
            panel_x + 22, py(332), 16);
  draw_text(TextFormat("%llu cells dug  |  %llu sites found",
                       static_cast<unsigned long long>(view.stats.cells_excavated),
                       static_cast<unsigned long long>(view.stats.sources_found)),
            panel_x + 22, py(352), 16, kMutedInk);

  const std::array<Icon, 4> focus_icons{
      {Icon::Balance, Icon::Growth, Icon::Expansion, Icon::Seed}};
  const std::array<const char*, 4> focus_names{{"Even", "Brood", "Dig", "Food"}};
  const std::array<const char*, 4> focus_hints{{"Even effort across every job (B)",
                                                "Tend the brood first (G)",
                                                "Dig new chambers first (X)",
                                                "Bring food home first (F)"}};
  for (int index = 0; index < 4; ++index) {
    Rectangle bounds = focus_button_bounds(panel_x, index);
    bounds.y -= panel_scroll_;
    const std::size_t slot = static_cast<std::size_t>(index);
    if (button(20 + static_cast<std::uint32_t>(index), bounds, focus_names[slot],
               static_cast<int>(view.focus) == index, view.focus_available, focus_icons[slot],
               view.focus_available ? focus_hints[slot] : "Unlocks at 12 workers",
               focus_name(static_cast<sim::Focus>(index)), true)) {
      focus_requested_ = static_cast<sim::Focus>(index);
    }
  }
  if (view.focus_cooldown_remaining > 0) {
    draw_text(TextFormat("Focus settles in %llu s",
                         static_cast<unsigned long long>(view.focus_cooldown_remaining /
                                                         sim::kTicksPerSecond) + 1),
              panel_x + 22, py(424), 13, kMutedInk);
  }
  DrawLine(panel_x + 20, py(442), width - 20, py(442), kWarmLine);
  draw_text(TextFormat("WORK  %lld", static_cast<long long>(view.work)), panel_x + 22, py(446), 16,
            kMutedInk);
  const std::array<const char*, 4> upgrades{{"Mandibles", "Nursery", "Trails", "Queen"}};
  const std::array<Icon, 4> upgrade_icons{
      {Icon::Mandibles, Icon::Nursery, Icon::Trail, Icon::Queen}};
  const std::array<const char*, 4> upgrade_effects{{"+25% dig rate per level",
                                                    "-10% brood time per level",
                                                    "+20% carry per level",
                                                    "-15% laying time per level"}};
  for (int index = 0; index < 4; ++index) {
    Rectangle card = upgrade_card_bounds(panel_x, panel_width, index);
    card.y -= panel_scroll_;
    const std::size_t slot = static_cast<std::size_t>(index);
    const std::int64_t cost = view.upgrade_costs[slot];
    const bool maxed = cost < 0;
    const bool affordable = !maxed && view.work >= cost;
    // Tracked as enabled so an unaffordable card still lifts and explains itself on hover; the
    // purchase itself is gated below.
    const WidgetVisual visual =
        ui_.track(30 + static_cast<std::uint32_t>(index), {card.x, card.y, card.width, card.height});
    const float lift = visual.hover * 1.6F - visual.press * 2.0F;
    const Rectangle body{card.x, card.y - lift, card.width, card.height};
    const Color resting = affordable ? Color{224, 231, 207, 255} : kPaper;
    const Color warmed = affordable ? Color{236, 242, 219, 255} : Color{243, 236, 217, 255};
    if (visual.hover > 0.01F) {
      DrawRectangleRounded({body.x - 1.0F, body.y + 2.0F, body.width + 2.0F, body.height}, 0.16F, 5,
                           Fade(BLACK, 0.09F * visual.hover));
    }
    DrawRectangleRounded(body, 0.15F, 5, mix(resting, warmed, visual.hover));
    DrawRectangleRoundedLinesEx(body, 0.15F, 5, 0.6F + visual.hover,
                                affordable ? Fade(Color{122, 148, 96, 255}, 0.5F + visual.hover * 0.5F)
                                           : Fade(kWarmLine, 0.7F));
    const Color ink = affordable ? kInk : kMutedInk;
    draw_icon(upgrade_icons[slot], {body.x + 7.0F, body.y + 6.0F, 18.0F, 18.0F}, ink);
    draw_text(TextFormat("%s  L%d", upgrades[slot], view.upgrade_levels[slot]),
              static_cast<int>(body.x) + 31, static_cast<int>(body.y) + 7, 15, ink);
    const char* price = maxed ? "max" : TextFormat("%lld", static_cast<long long>(cost));
    const Vector2 measured = MeasureTextEx(font_, price, 15.0F, 0.0F);
    const float price_x = body.x + body.width - measured.x - 10.0F;
    draw_text(price, static_cast<int>(price_x), static_cast<int>(body.y) + 7, 15, ink);
    if (!maxed) {
      draw_icon(Icon::Mandibles, {price_x - 16.0F, body.y + 8.0F, 12.0F, 12.0F},
                Fade(ink, 0.75F));
    }
    if (visual.clicked && affordable) upgrade_requested_ = static_cast<game::UpgradeId>(index);
    if (visual.over) {
      tooltip_title_ = upgrades[slot];
      tooltip_body_ = maxed ? "Fully adapted"
                    : affordable ? upgrade_effects[slot]
                                 : std::string("Needs ") + std::to_string(cost) + " Work";
      tooltip_x_ = body.x;
      tooltip_y_ = body.y;
    }
  }
  EndScissorMode();
  ui_.set_input_region(!scenes_.modal());
  const int selected_y = static_cast<int>(layout_.selected.y);
  DrawRectangle(panel_x, selected_y, panel_width, 142, kPaperSoft);
  DrawLine(panel_x + 20, selected_y, width - 20, selected_y, kWarmLine);
  const sim::ActorSnapshot* selected = nullptr;
  for (const auto& actor : view.actors) {
    if (selection_.id() == actor.id) { selected = &actor; break; }
  }
  draw_text(selected ? "SELECTED ANT" : "RECENT", panel_x + 22, selected_y + 10, 14, kMutedInk);
  if (!selected) {
    // The space under an empty selection is where the colony says what it has just done.
    draw_log(panel_x, panel_width, selected_y + 32);
  } else {
    const char* kind = selected->kind == sim::AntKind::Queen ? "Queen" :
                       selected->kind == sim::AntKind::WingedQueen ? "Winged queen" : "Worker";
    draw_text(kind, panel_x + 22, selected_y + 32, 20);
    const char* activity =
        selected->forage_state == sim::ForageState::Scouting ? "Searching for food" :
        selected->kind == sim::AntKind::Worker ? sim::task_name(selected->task) :
        selected->kind == sim::AntKind::WingedQueen ? "Waiting for the flight" : "Founding queen";
    draw_text(activity, panel_x + 22, selected_y + 60, 17);
    if (selected->kind == sim::AntKind::Worker) {
      const char* cargo = selected->cargo_amount <= 0 ? "Carrying nothing" :
          selected->cargo_kind == sim::CargoKind::Spoil ? "Carrying spoil" :
          selected->cargo_kind == sim::CargoKind::Corpse ? "Carrying a fallen sister" :
          TextFormat("Carrying %lld %s", static_cast<long long>(selected->cargo_amount / 1000),
                     nutrient_name(selected->cargo_nutrient));
      draw_text(cargo, panel_x + 22, selected_y + 85, 16, kMutedInk);
      draw_text(TextFormat("%llu seconds old", static_cast<unsigned long long>(selected->age / sim::kTicksPerSecond)),
                panel_x + 22, selected_y + 109, 16, kMutedInk);
    }
  }
}

void Renderer::draw_readiness(const game::GameView& view, const int panel_x,
                              const int panel_width, const int top) const {
  const game::FlightPreview& flight = view.legacy.flight;
  const int met = (flight.mature ? 1 : 0) + (flight.living_workers >= 100 ? 1 : 0) +
                  (flight.births >= 150 ? 1 : 0) +
                  (flight.run_ticks >= 720 * static_cast<unsigned>(sim::kTicksPerSecond) ? 1 : 0) +
                  (flight.live_winged_queens >= 3 ? 1 : 0);
  draw_text("FLIGHT READINESS", panel_x + 22, top, 14, kMutedInk);
  const char* score = TextFormat("%d / 5", met);
  const Vector2 measured = MeasureTextEx(font_, score, 14.0F, 0.0F);
  draw_text(score, panel_x + panel_width - 22 - static_cast<int>(measured.x), top, 14,
            flight.eligible ? Color{104, 142, 96, 255} : kMutedInk);
  const int bar_y = top + 21;
  const int bar_width = panel_width - 44;
  DrawRectangle(panel_x + 22, bar_y, bar_width, 6, Color{214, 200, 170, 255});
  DrawRectangle(panel_x + 22, bar_y, bar_width * met / 5, 6,
                flight.eligible ? Color{104, 142, 96, 255} : kFoliage);
  // The bundled atlas is ASCII only, so an em dash would draw as a question mark.
  draw_text(flight.eligible ? "Ready - open the Flight panel to send it"
                            : game::flight_block_reason(flight.block),
            panel_x + 22, bar_y + 12, 14, kMutedInk);
}

void Renderer::draw_log(const int panel_x, const int panel_width, const int top) const {
  if (log_.empty()) {
    draw_text("Click an ant to inspect it.", panel_x + 22, top, 16, kMutedInk);
    return;
  }
  int line = top;
  for (const LogEntry& entry : log_) {
    // Older lines fade rather than vanish, so the list settles instead of flickering.
    const float weight = std::clamp(1.0F - entry.age / 26.0F, 0.25F, 1.0F);
    draw_icon(entry.icon, {static_cast<float>(panel_x + 22), static_cast<float>(line) + 2.0F, 13.0F,
                           13.0F},
              Fade(kMutedInk, weight));
    DrawTextEx(font_, truncate_to_width(entry.text, static_cast<float>(panel_width - 66), 16.0F).c_str(),
               {static_cast<float>(panel_x + 41), static_cast<float>(line)}, 16.0F, 0.0F,
               Fade(kInk, weight));
    line += 22;
  }
  // A quiet colony leaves room here, so the hint about selecting an ant keeps its place.
  if (log_.size() < 3) {
    draw_text("Click an ant to inspect it.", panel_x + 22, line + 8, 16, kMutedInk);
  }
}

void Renderer::update_selection(const game::GameView& view, const double interpolation_alpha) {
  poses_.clear();
  poses_.reserve(view.actors.size());
  for (const auto& actor : view.actors) poses_.push_back(actor_pose(actor, interpolation_alpha));
  selection_.synchronize(run_id_, poses_);
  if (select_requested_ && !scenes_.modal()) {
    const Vector2 world = camera_.screen_to_world({select_point_.x, select_point_.y});
    selection_.pick(poses_, {world.x, world.y}, camera_.zoom());
  }
  select_requested_ = false;
}

void Renderer::draw_pause_menu() {
  const Rectangle panel = draw_modal_card(440.0F, abandon_armed_ ? 350.0F : 302.0F, "Paused");
  const float left = panel.x + 30.0F;
  const float wide = panel.width - 60.0F;
  draw_text("The colony is holding still.", static_cast<int>(left),
            static_cast<int>(panel.y) + 74, 17, kMutedInk);
  ui_.set_input_region(true);
  float line = panel.y + 110.0F;
  if (button(100, {left, line, wide, 44.0F}, "Return to colony", false, true, Icon::Play,
             "Resume the simulation (Esc)")) {
    scenes_.close();
    ui_.cancel_capture();
    world_click_.cancel();
  }
  line += 54.0F;
  if (button(101, {left, line, wide, 44.0F}, "Save colony", false, true, Icon::Save,
             "Write the colony to disk now")) {
    save_requested_ = true;
  }
  line += 54.0F;
  if (!abandon_armed_) {
    if (button(102, {left, line, wide, 44.0F}, "Abandon colony", false, true, Icon::Reset,
               "Ends this run with no Legacy and asks to confirm")) {
      abandon_armed_ = true;
    }
    return;
  }
  draw_text("This ends the run with no Legacy.", static_cast<int>(left), static_cast<int>(line),
            16, kProtein);
  line += 26.0F;
  if (button(103, {left, line, wide * 0.5F - 5.0F, 44.0F}, "Confirm", false, true, Icon::Reset,
             "Abandon this colony for good")) {
    abandon_requested_ = true;
    abandon_armed_ = false;
  }
  if (button(104, {left + wide * 0.5F + 5.0F, line, wide * 0.5F - 5.0F, 44.0F}, "Keep it", false,
             true, Icon::Close, "Leave the colony alone")) {
    abandon_armed_ = false;
  }
}

void Renderer::clear_requests() {
  toggle_pause_requested_ = false;
  speed_requested_.reset();
  upgrade_requested_.reset();
  focus_requested_.reset();
  save_requested_ = false;
  recover_requested_ = false;
  new_colony_requested_ = false;
  abandon_requested_ = false;
  flight_requested_ = false;
  new_run_requested_ = false;
  trait_requested_.reset();
}

} // namespace ant::presentation
