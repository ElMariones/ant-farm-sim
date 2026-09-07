#include "presentation/renderer.hpp"

#include "sim/grid.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace ant::presentation {
namespace {

constexpr Color kDeepSoil{48, 41, 35, 255};
constexpr Color kSandySoil{141, 105, 70, 255};
constexpr Color kClay{102, 74, 58, 255};
constexpr Color kTunnel{23, 27, 25, 255};
constexpr Color kRoot{67, 52, 40, 255};
constexpr Color kBedrock{37, 33, 30, 255};
constexpr Color kFoliage{112, 132, 87, 255};
constexpr Color kSky{201, 213, 195, 255};
constexpr Color kPaper{238, 228, 204, 255};
constexpr Color kInk{37, 45, 40, 255};
constexpr Color kCarbohydrate{221, 189, 103, 255};
constexpr Color kProtein{199, 127, 101, 255};
constexpr Color kBrood{234, 219, 192, 255};
constexpr Color kSelection{152, 203, 195, 255};
constexpr Color kPaperSoft{246, 239, 220, 255};
constexpr Color kMutedInk{85, 91, 84, 255};
constexpr Color kWarmLine{193, 176, 143, 255};

Color material_color(const sim::Material material, const int x, const int y,
                     const std::uint64_t seed) {
  if (material == sim::Material::Sky) {
    return kSky;
  }
  if (material == sim::Material::Air) {
    return kTunnel;
  }
  if (material == sim::Material::Root) {
    return kRoot;
  }
  if (material == sim::Material::Bedrock) {
    return kBedrock;
  }
  const std::uint64_t fleck = (static_cast<std::uint64_t>(x) * 73856093ULL) ^
                              (static_cast<std::uint64_t>(y) * 19349663ULL) ^ (seed * 83492791ULL);
  const int variation = static_cast<int>((fleck >> 4U) % 13ULL) - 6;
  const Color base = material == sim::Material::Clay ? kClay : kSandySoil;
  return {static_cast<unsigned char>(std::clamp(static_cast<int>(base.r) + variation, 0, 255)),
          static_cast<unsigned char>(std::clamp(static_cast<int>(base.g) + variation, 0, 255)),
          static_cast<unsigned char>(std::clamp(static_cast<int>(base.b) + variation, 0, 255)),
          255};
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
}

void Renderer::draw_text(const char* text, const int x, const int y, const int size,
                         const Color color) const {
  DrawTextEx(font_, text, {static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(size),
             0.0F, color);
}

void Renderer::draw_button(const Rectangle bounds, const char* label, const bool active) const {
  DrawRectangleRounded(bounds, 0.22F, 8, active ? kSelection : Color{229, 219, 195, 255});
  DrawRectangleRoundedLinesEx(bounds, 0.22F, 8, 1.0F, active ? Color{69, 111, 103, 255} : kWarmLine);
  const Vector2 measured = MeasureTextEx(font_, label, 19.0F, 0.0F);
  DrawTextEx(font_, label, {bounds.x + (bounds.width - measured.x) * 0.5F, bounds.y + 8.0F}, 19.0F,
             0.0F, kInk);
}

namespace {

constexpr float kUpgradeCardTop = 436.0F;
constexpr float kUpgradeCardHeight = 30.0F;
constexpr float kUpgradeCardStride = 34.0F;
constexpr float kFocusButtonTop = 380.0F;

Rectangle upgrade_card_bounds(const int panel_x, const int panel_width, const int index) {
  return {static_cast<float>(panel_x + 18), kUpgradeCardTop + kUpgradeCardStride * index,
          static_cast<float>(panel_width - 36), kUpgradeCardHeight};
}

Rectangle focus_button_bounds(const int panel_x, const int index) {
  return {static_cast<float>(panel_x + 18 + index * 70), kFocusButtonTop, 64.0F, 29.0F};
}

} // namespace

void Renderer::update_input(const float delta_seconds) {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  camera_.layout(width, height, inspector_open_);
  camera_.update(delta_seconds, true);

  if (IsKeyPressed(KEY_SPACE)) {
    toggle_pause_requested_ = true;
  }
  if (IsKeyPressed(KEY_ONE)) {
    speed_requested_ = 1;
  } else if (IsKeyPressed(KEY_TWO)) {
    speed_requested_ = 5;
  } else if (IsKeyPressed(KEY_THREE)) {
    speed_requested_ = 20;
  }
  if (IsKeyPressed(KEY_I)) {
    inspector_open_ = !inspector_open_;
    camera_.layout(width, height, inspector_open_);
  }
  if (IsKeyPressed(KEY_F1)) upgrade_requested_ = game::UpgradeId::Excavation;
  else if (IsKeyPressed(KEY_F2)) upgrade_requested_ = game::UpgradeId::Nursing;
  else if (IsKeyPressed(KEY_F3)) upgrade_requested_ = game::UpgradeId::Foraging;
  else if (IsKeyPressed(KEY_F4)) upgrade_requested_ = game::UpgradeId::Queen;
  if (IsKeyPressed(KEY_B)) focus_requested_ = sim::Focus::Balanced;
  else if (IsKeyPressed(KEY_G)) focus_requested_ = sim::Focus::Growth;
  else if (IsKeyPressed(KEY_X)) focus_requested_ = sim::Focus::Expansion;
  else if (IsKeyPressed(KEY_F)) focus_requested_ = sim::Focus::Foraging;
  if (IsKeyPressed(KEY_S)) save_requested_ = true;
  if (!recovery_prompt_) {
    if (IsKeyPressed(KEY_L)) legacy_panel_open_ = !legacy_panel_open_;
    if (legacy_panel_open_) {
      if (IsKeyPressed(KEY_ENTER)) flight_requested_ = true;
      if (IsKeyPressed(KEY_V)) trait_requested_ = game::TraitBranch::Vigor;
      if (IsKeyPressed(KEY_Y)) trait_requested_ = game::TraitBranch::Industry;
      if (IsKeyPressed(KEY_C)) new_run_requested_ = true;
      if (IsKeyPressed(KEY_ESCAPE)) legacy_panel_open_ = false;
    }
  }
  if (recovery_prompt_) {
    if (IsKeyPressed(KEY_R)) recover_requested_ = true;
    if (IsKeyPressed(KEY_N)) new_colony_requested_ = true;
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    const Vector2 mouse = GetMousePosition();
    const float footer_y = static_cast<float>(height - 51);
    if (CheckCollisionPointRec(mouse, {18.0F, footer_y, 94.0F, 42.0F})) {
      toggle_pause_requested_ = true;
    } else if (CheckCollisionPointRec(mouse, {130.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 1;
    } else if (CheckCollisionPointRec(mouse, {194.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 5;
    } else if (CheckCollisionPointRec(mouse, {258.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 20;
    } else if (CheckCollisionPointRec(mouse, {330.0F, footer_y, 78.0F, 42.0F})) {
      save_requested_ = true;
    } else if (inspector_open_) {
      const int panel_width = width >= 1120 ? 320 : 300;
      const int panel_x = width - panel_width;
      for (int index = 0; index < 4; ++index) {
        if (CheckCollisionPointRec(mouse, upgrade_card_bounds(panel_x, panel_width, index))) {
          upgrade_requested_ = static_cast<game::UpgradeId>(index);
        }
        if (CheckCollisionPointRec(mouse, focus_button_bounds(panel_x, index))) {
          focus_requested_ = static_cast<sim::Focus>(index);
        }
      }
    }
  }
}

void Renderer::draw(const game::GameView& view, const double interpolation_alpha, const bool paused,
                    const int speed, const bool simulation_limited) {
  update_selection(view);
  ClearBackground(kDeepSoil);
  draw_world(view, interpolation_alpha);
  draw_interface(view, paused, speed, simulation_limited);
  if (legacy_panel_open_) draw_legacy_panel(view);
  if (recovery_prompt_) draw_recovery_prompt();
}

void Renderer::draw_legacy_panel(const game::GameView& view) const {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{24, 28, 25, 180});
  const float panel_width = static_cast<float>(std::min(width - 80, 640));
  const Rectangle panel{(static_cast<float>(width) - panel_width) * 0.5F,
                        static_cast<float>(height) * 0.5F - 210.0F, panel_width, 420.0F};
  DrawRectangleRounded(panel, 0.04F, 8, kPaper);
  DrawRectangleRoundedLinesEx(panel, 0.04F, 8, 1.0F, kWarmLine);
  const int left = static_cast<int>(panel.x) + 28;
  int line = static_cast<int>(panel.y) + 26;
  const game::LegacyView& legacy = view.legacy;

  draw_text(legacy.between_runs ? "Between colonies" : "Nuptial flight", left, line, 23);
  line += 34;
  draw_text(TextFormat("Generation %llu   |   Flights %llu   |   Legacy %lld",
                       static_cast<unsigned long long>(legacy.generation),
                       static_cast<unsigned long long>(legacy.successful_flights),
                       static_cast<long long>(legacy.wallet)),
            left, line, 17, kMutedInk);
  line += 34;

  if (!legacy.between_runs) {
    const game::FlightPreview& flight = legacy.flight;
    draw_text("READINESS", left, line, 14, kMutedInk);
    line += 24;
    const auto tick = [](const bool done) { return done ? "[x]" : "[ ]"; };
    draw_text(TextFormat("%s  Mature colony", tick(flight.mature)), left, line, 17);
    line += 25;
    draw_text(TextFormat("%s  Living workers  %d / 100", tick(flight.living_workers >= 100),
                         flight.living_workers),
              left, line, 17);
    line += 25;
    draw_text(TextFormat("%s  Workers born  %llu / 150", tick(flight.births >= 150),
                         static_cast<unsigned long long>(flight.births)),
              left, line, 17);
    line += 25;
    draw_text(TextFormat("%s  Run time  %llu / 720 s",
                         tick(flight.run_ticks >= 720 * static_cast<unsigned>(sim::kTicksPerSecond)),
                         static_cast<unsigned long long>(flight.run_ticks / sim::kTicksPerSecond)),
              left, line, 17);
    line += 25;
    draw_text(TextFormat("%s  Winged queens  %d / 3", tick(flight.live_winged_queens >= 3),
                         flight.live_winged_queens),
              left, line, 17);
    line += 34;
    draw_text(TextFormat("Legacy on flight: %lld   (2 base + %lld births + %lld queens)",
                         static_cast<long long>(flight.payout.total),
                         static_cast<long long>(flight.payout.birth_bonus),
                         static_cast<long long>(flight.payout.queen_bonus)),
              left, line, 18);
    line += 26;
    if (flight.next_birth_threshold > 0) {
      draw_text(TextFormat("Next step at %llu births%s",
                           static_cast<unsigned long long>(flight.next_birth_threshold),
                           flight.next_queen_threshold > 0
                               ? TextFormat(" or %d winged queens", flight.next_queen_threshold)
                               : ""),
                left, line, 15, kMutedInk);
    }
    line += 34;
    draw_text(flight.eligible ? "Enter  -  send the flight and end this colony"
                              : game::flight_block_reason(flight.block),
              left, line, 18, flight.eligible ? kInk : kMutedInk);
  } else {
    draw_text("PERMANENT TRAITS", left, line, 14, kMutedInk);
    line += 26;
    const auto cost_label = [](const std::int64_t cost) {
      return cost < 0 ? "complete" : TextFormat("%lld Legacy", static_cast<long long>(cost));
    };
    draw_text(TextFormat("V   Vigor  tier %d/4   %s", legacy.vigor_tier,
                         cost_label(legacy.vigor_cost)),
              left, line, 18,
              legacy.vigor_cost >= 0 && legacy.wallet >= legacy.vigor_cost ? kInk : kMutedInk);
    line += 27;
    draw_text("     Faster eggs, more founders, faster brood, thriftier queen", left, line, 14,
              kMutedInk);
    line += 30;
    draw_text(TextFormat("Y   Industry  tier %d/4   %s", legacy.industry_tier,
                         cost_label(legacy.industry_cost)),
              left, line, 18,
              legacy.industry_cost >= 0 && legacy.wallet >= legacy.industry_cost ? kInk : kMutedInk);
    line += 27;
    draw_text("     Faster digging, faster movement, bigger loads, cheaper Work", left, line, 14,
              kMutedInk);
    line += 40;
    draw_text("C   -  found the next colony", left, line, 18);
  }

  draw_text("L or Escape closes this panel", left,
            static_cast<int>(panel.y + panel.height) - 34, 15, kMutedInk);
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

void Renderer::draw_recovery_prompt() const {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  DrawRectangle(0, 0, width, height, Color{24, 28, 25, 190});
  const float panel_width = static_cast<float>(std::min(width - 80, 620));
  const Rectangle panel{(static_cast<float>(width) - panel_width) * 0.5F,
                        static_cast<float>(height) * 0.5F - 110.0F, panel_width, 220.0F};
  DrawRectangleRounded(panel, 0.06F, 8, kPaper);
  DrawRectangleRoundedLinesEx(panel, 0.06F, 8, 1.0F, kWarmLine);
  const int left = static_cast<int>(panel.x) + 28;
  int line = static_cast<int>(panel.y) + 28;
  draw_text("Saved colony could not be read", left, line, 22);
  line += 40;
  draw_text("The current save file is damaged. It has been left untouched", left, line, 17,
            kMutedInk);
  line += 24;
  draw_text("for diagnosis, and nothing is being saved right now.", left, line, 17, kMutedInk);
  line += 38;
  draw_text("R  -  restore the previous saved revision", left, line, 18);
  line += 28;
  draw_text("N  -  start a new colony instead", left, line, 18);
  if (!status_line_.empty()) {
    const std::string fitted = truncate_to_width(status_line_, panel.width - 56.0F, 15.0F);
    draw_text(fitted.c_str(), left, static_cast<int>(panel.y + panel.height) - 34, 15, kMutedInk);
  }
}

void Renderer::draw_world(const game::GameView& view, const double interpolation_alpha) {
  BeginMode2D(camera_.camera());

  for (int y = 0; y < view.grid_height; ++y) {
    for (int x = 0; x < view.grid_width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y * view.grid_width + x);
      DrawRectangle(x, y, 1, 1, material_color(view.terrain[index], x, y, view.seed));
    }
  }

  for (int x = 0; x < view.grid_width; x += 3) {
    const float height =
        0.8F + static_cast<float>((x * 17 + static_cast<int>(view.seed)) % 4) * 0.25F;
    DrawLineEx({static_cast<float>(x), 32.0F}, {static_cast<float>(x) + 0.3F, 32.0F - height},
               0.35F, kFoliage);
  }

  DrawCircleV({static_cast<float>(view.home.x) + 0.5F, static_cast<float>(view.home.y) + 0.5F},
              3.5F, Color{74, 62, 48, 255});
  DrawCircleLinesV({static_cast<float>(view.home.x) + 0.5F, static_cast<float>(view.home.y) + 0.5F},
                   4.2F, kBrood);

  for (const sim::FoodSource& source : view.sources) {
    const Color color = source.nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein;
    const Vector2 center{static_cast<float>(source.position.x) + 0.5F,
                         static_cast<float>(source.position.y) + 0.5F};
    if (source.nutrient == sim::Nutrient::Carbohydrate) {
      DrawCircleV(center, 2.7F, color);
      DrawCircleLinesV(center, 3.2F, kInk);
    } else {
      DrawPoly(center, 5, 3.1F, -18.0F, color);
      DrawPolyLines(center, 5, 3.2F, -18.0F, kInk);
    }
  }

  for (const sim::BroodSnapshot& brood : view.brood) {
    const Vector2 center{static_cast<float>(brood.position.x) + 0.5F,
                         static_cast<float>(brood.position.y) + 0.5F};
    if (brood.stage == sim::BroodStage::Egg) {
      DrawEllipse(static_cast<int>(center.x), static_cast<int>(center.y), 0.45F, 0.7F, kBrood);
    } else if (brood.stage == sim::BroodStage::Larva) {
      DrawCircleV(center, 0.75F, kBrood);
      DrawCircleV({center.x + 0.65F, center.y + 0.1F}, 0.52F, kBrood);
    } else {
      DrawEllipse(static_cast<int>(center.x), static_cast<int>(center.y), 0.75F, 1.15F, kBrood);
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

  if (view.spoil_mound > 0) {
    const float mound = std::min(8.0F, 1.5F + static_cast<float>(view.spoil_mound) * 0.3F);
    DrawEllipse(view.home.x + 10, 31, mound, mound * 0.35F, Color{126, 91, 58, 255});
  }

  for (const sim::ActorSnapshot& actor : view.actors) {
    if (actor.kind == sim::AntKind::Queen && !view.queen_alive) {
      continue;
    }
    draw_ant(actor, interpolation_alpha, camera_.zoom(), selected_id_ == actor.id);
  }

  EndMode2D();
}

void Renderer::draw_ant(const sim::ActorSnapshot& actor, const double interpolation_alpha,
                        const float zoom, const bool selected) {
  const float alpha = static_cast<float>(std::clamp(interpolation_alpha, 0.0, 1.0));
  float x = static_cast<float>(actor.previous_x + (actor.x - actor.previous_x) * alpha);
  float y = static_cast<float>(actor.previous_y + (actor.y - actor.previous_y) * alpha);
  x += static_cast<float>(static_cast<int>((actor.id * 17ULL) % 7ULL) - 3) * 0.09F;
  y += static_cast<float>(static_cast<int>((actor.id * 11ULL) % 5ULL) - 2) * 0.08F;
  const float scale = actor.kind == sim::AntKind::Queen ? 1.85F : 1.0F;

  if (selected) {
    DrawCircleLinesV({x, y}, 2.7F * scale, kSelection);
  }
  if (zoom < 2.6F) {
    DrawCircleV({x, y}, 0.85F * scale, kInk);
  } else {
    DrawCircleV({x - 0.8F * scale, y}, 0.55F * scale, kInk);
    DrawCircleV({x, y}, 0.48F * scale, kInk);
    DrawEllipse(static_cast<int>(x + 0.9F * scale), static_cast<int>(y), 0.85F * scale,
                0.62F * scale, kInk);
    if (zoom >= 5.5F && actor.kind == sim::AntKind::Worker) {
      for (int leg = -1; leg <= 1; ++leg) {
        const float leg_x = x + static_cast<float>(leg) * 0.35F;
        DrawLineEx({leg_x, y}, {leg_x - 0.8F, y - 1.0F}, 0.12F, kInk);
        DrawLineEx({leg_x, y}, {leg_x + 0.8F, y + 1.0F}, 0.12F, kInk);
      }
    }
  }

  if (actor.cargo_amount > 0) {
    const Color cargo_color =
        actor.cargo_nutrient == sim::Nutrient::Carbohydrate ? kCarbohydrate : kProtein;
    if (actor.cargo_nutrient == sim::Nutrient::Carbohydrate) {
      DrawCircleV({x, y - 1.45F * scale}, 0.55F, cargo_color);
    } else {
      DrawPoly({x, y - 1.45F * scale}, 4, 0.65F, 45.0F, cargo_color);
    }
  }
}

void Renderer::draw_interface(const game::GameView& view, const bool paused, const int speed,
                              const bool simulation_limited) {
  const int width = GetScreenWidth();
  const int height = GetScreenHeight();
  const int worker_count = static_cast<int>(std::count_if(
      view.actors.begin(), view.actors.end(), [](const sim::ActorSnapshot& actor) {
        return actor.kind == sim::AntKind::Worker;
      }));
  DrawRectangle(0, 0, width, 72, kPaperSoft);
  DrawRectangle(0, 68, width, 4, Color{112, 132, 87, 255});
  draw_text("ANT FARM", 22, 15, 27);
  draw_text("LIVING COLONY", 23, 44, 13, kMutedInk);
  const int metric_start = width >= 1180 ? 190 : 170;
  const int metric_width = width >= 1180 ? 145 : 125;
  const std::array<std::pair<const char*, std::string>, 4> metrics{{
      {"WORKERS", std::to_string(worker_count)}, {"BROOD", std::to_string(view.brood.size())},
      {"CARBS", TextFormat("%lld", static_cast<long long>(view.stores.carbohydrate / 1000))},
      {"PROTEIN", TextFormat("%lld", static_cast<long long>(view.stores.protein / 1000))}}};
  for (std::size_t index = 0; index < metrics.size(); ++index) {
    const int x = metric_start + static_cast<int>(index) * metric_width;
    draw_text(metrics[index].first, x, 14, 12, kMutedInk);
    draw_text(metrics[index].second.c_str(), x, 32, 23);
  }

  const float footer_y = static_cast<float>(height - 60);
  DrawRectangle(0, height - 60, width, 60, kPaperSoft);
  DrawLine(0, height - 60, width, height - 60, kWarmLine);
  draw_button({18.0F, footer_y + 9.0F, 94.0F, 42.0F}, paused ? "Resume" : "Pause", paused);
  draw_button({130.0F, footer_y + 9.0F, 58.0F, 42.0F}, "1x", speed == 1);
  draw_button({194.0F, footer_y + 9.0F, 58.0F, 42.0F}, "5x", speed == 5);
  draw_button({258.0F, footer_y + 9.0F, 58.0F, 42.0F}, "20x", speed == 20);
  draw_button({330.0F, footer_y + 9.0F, 78.0F, 42.0F}, "Save", false);
  draw_text("Generation 1", 430, height - 39, 18);
  if (!status_line_.empty() && width >= 1100) {
    // Stop a long failure message from running into the control hints on the right.
    const float available = static_cast<float>((width >= 1200 ? width - 540 : width - 20) - 560);
    draw_text(truncate_to_width(status_line_, available, 16.0F).c_str(), 560, height - 39, 16,
              kMutedInk);
  }
  if (width >= 1200) {
    draw_text("Drag to pan  |  Scroll to zoom  |  Click an ant", width - 530, height - 39, 17,
              kMutedInk);
  }

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
  DrawRectangle(panel_x - 7, 72, 7, height - 132, Color{37, 45, 40, 35});
  DrawRectangle(panel_x, 72, panel_width, height - 132, Color{246, 239, 220, 248});
  draw_text("COLONY", panel_x + 22, 92, 15, kMutedInk);
  const char* condition = view.extinct ? "The colony is still" : view.decline ? "The queen is gone" :
                          view.brood.size() >= static_cast<std::size_t>(view.nursery_capacity) ? "Nursery is full" :
                          view.stores.protein < 10'000 ? "Protein is running low" : "Growing steadily";
  draw_text(condition, panel_x + 22, 116, 22, view.decline ? kProtein : kInk);
  draw_text(TextFormat("%d workers tending %zu brood", worker_count, view.brood.size()),
            panel_x + 22, 148, 16, kMutedInk);

  const Rectangle nursery_card{static_cast<float>(panel_x + 16), 180.0F,
                                static_cast<float>(panel_width - 32), 88.0F};
  DrawRectangleRounded(nursery_card, 0.12F, 6, kPaper);
  DrawRectangleRoundedLinesEx(nursery_card, 0.12F, 6, 1.0F, kWarmLine);
  draw_text("NURSERY", panel_x + 30, 195, 13, kMutedInk);
  draw_text(TextFormat("%zu of %d spaces", view.brood.size(), view.nursery_capacity),
            panel_x + 30, 216, 19);
  const float ratio = view.nursery_capacity == 0 ? 0.0F : std::min(1.0F, static_cast<float>(view.brood.size()) / static_cast<float>(view.nursery_capacity));
  DrawRectangle(panel_x + 30, 247, panel_width - 60, 7, Color{214, 200, 170, 255});
  DrawRectangle(panel_x + 30, 247, static_cast<int>(static_cast<float>(panel_width - 60) * ratio), 7, kFoliage);

  draw_text("COLONY ACTIVITY", panel_x + 22, 292, 14, kMutedInk);
  draw_text(TextFormat("Forage %u   Dig %u   Nurse %u", view.tasks.workers_by_task[0],
                       view.tasks.workers_by_task[1], view.tasks.workers_by_task[2]),
            panel_x + 22, 317, 16);
  draw_text(TextFormat("%llu new cells  |  %llu births",
                       static_cast<unsigned long long>(view.stats.cells_excavated),
                       static_cast<unsigned long long>(view.stats.workers_born)),
            panel_x + 22, 338, 16, kMutedInk);
  draw_text(TextFormat("Focus: %s%s", focus_name(view.focus), view.focus_cooldown_remaining > 0 ? " (cooldown)" : ""), panel_x + 22, 359, 16, kMutedInk);

  const std::array<const char*,4> focus_labels{{"Bal","Grow","Expand","Food"}};
  for(int index=0;index<4;++index) draw_button(focus_button_bounds(panel_x,index),focus_labels[static_cast<std::size_t>(index)],static_cast<int>(view.focus)==index);
  DrawLine(panel_x + 20, 415, width - 20, 415, kWarmLine);
  draw_text(TextFormat("WORK  %lld",static_cast<long long>(view.work)),panel_x+22,419,16,kMutedInk);
  const std::array<const char*,4> upgrades{{"Mandibles","Nursery","Trails","Queen"}};
  for(int index=0;index<4;++index){
    const Rectangle card=upgrade_card_bounds(panel_x,panel_width,index);
    const std::int64_t cost=view.upgrade_costs[static_cast<std::size_t>(index)];
    const bool affordable=cost>=0&&view.work>=cost;
    DrawRectangleRounded(card,0.15F,5,affordable?Color{224,231,207,255}:kPaper);
    const char* label=cost<0?TextFormat("F%d  %s  L%d  max",index+1,upgrades[static_cast<std::size_t>(index)],view.upgrade_levels[static_cast<std::size_t>(index)])
                            :TextFormat("F%d  %s  L%d  %lld",index+1,upgrades[static_cast<std::size_t>(index)],view.upgrade_levels[static_cast<std::size_t>(index)],static_cast<long long>(cost));
    draw_text(label,panel_x+28,static_cast<int>(card.y)+6,14,affordable?kInk:kMutedInk);
  }
  draw_text(game::bottleneck_name(view.bottleneck),panel_x+22,576,14,kMutedInk);
  /* Selection stays available in the world; temporary M3 cards occupy the inspector detail area. */

  if (height >= 720) {
    const int note_y = height - 178;
    DrawRectangleRounded({static_cast<float>(panel_x + 16), static_cast<float>(note_y),
                          static_cast<float>(panel_width - 32), 82.0F}, 0.12F, 6,
                         Color{224, 231, 207, 255});
    draw_text("A LIVING SYSTEM", panel_x + 30, note_y + 14, 13, Color{65, 92, 68, 255});
    draw_text("Ants choose work from the", panel_x + 30, note_y + 35, 16);
    draw_text("colony's changing needs.", panel_x + 30, note_y + 57, 16);
  }
}

void Renderer::update_selection(const game::GameView& view) {
  // Accept release as well as press so very short native clicks are not lost between render frames.
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    return;
  }
  const Vector2 mouse = GetMousePosition();
  if (!camera_.contains(mouse)) {
    return;
  }
  const int width = GetScreenWidth();
  if (inspector_open_ && width < 1120 && mouse.x >= static_cast<float>(width - 300)) {
    return;
  }
  const Vector2 world = GetScreenToWorld2D(mouse, camera_.camera());
  const double pick_radius = std::max(1.8, 8.0 / static_cast<double>(camera_.zoom()));
  const sim::ActorSnapshot* best = nullptr;
  double best_distance = pick_radius * pick_radius;
  for (const sim::ActorSnapshot& actor : view.actors) {
    const double dx = actor.x - static_cast<double>(world.x);
    const double dy = actor.y - static_cast<double>(world.y);
    const double distance = dx * dx + dy * dy;
    if (distance <= best_distance) {
      best_distance = distance;
      best = &actor;
    }
  }
  if (best != nullptr) {
    selected_id_ = best->id;
  } else {
    selected_id_.reset();
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
  flight_requested_ = false;
  new_run_requested_ = false;
  trait_requested_.reset();
}

} // namespace ant::presentation
