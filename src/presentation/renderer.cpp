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

const char* state_name(const sim::ForageState state) {
  switch (state) {
  case sim::ForageState::AtHome:
    return "Choosing a route";
  case sim::ForageState::ToSource:
    return "Foraging";
  case sim::ForageState::Returning:
    return "Returning to storage";
  case sim::ForageState::WaitingForStorage:
    return "Waiting for storage";
  }
  return "Unknown";
}

} // namespace

Renderer::Renderer() {
  const std::filesystem::path application_directory = GetApplicationDirectory();
  const std::filesystem::path bundled =
      application_directory / "assets" / "fonts" / "Nunito-SemiBold.ttf";
  const std::filesystem::path source =
      std::filesystem::path(ANT_SOURCE_ASSET_DIR) / "fonts" / "Nunito-SemiBold.ttf";
  const std::filesystem::path selected = std::filesystem::exists(bundled) ? bundled : source;
  font_ = LoadFontEx(selected.string().c_str(), 64, nullptr, 0);
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
  DrawRectangleRec(bounds, active ? kSelection : Color{221, 211, 187, 255});
  DrawRectangleLinesEx(bounds, 1.0F, kInk);
  const Vector2 measured = MeasureTextEx(font_, label, 19.0F, 0.0F);
  DrawTextEx(font_, label, {bounds.x + (bounds.width - measured.x) * 0.5F, bounds.y + 8.0F}, 19.0F,
             0.0F, kInk);
}

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
    speed_requested_ = 2;
  } else if (IsKeyPressed(KEY_THREE)) {
    speed_requested_ = 5;
  }
  if (IsKeyPressed(KEY_I)) {
    inspector_open_ = !inspector_open_;
    camera_.layout(width, height, inspector_open_);
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    const Vector2 mouse = GetMousePosition();
    const float footer_y = static_cast<float>(height - 51);
    if (CheckCollisionPointRec(mouse, {18.0F, footer_y, 94.0F, 42.0F})) {
      toggle_pause_requested_ = true;
    } else if (CheckCollisionPointRec(mouse, {130.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 1;
    } else if (CheckCollisionPointRec(mouse, {194.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 2;
    } else if (CheckCollisionPointRec(mouse, {258.0F, footer_y, 58.0F, 42.0F})) {
      speed_requested_ = 5;
    }
  }
}

void Renderer::draw(const game::GameView& view, const double interpolation_alpha, const bool paused,
                    const int speed, const bool simulation_limited) {
  update_selection(view);
  ClearBackground(kDeepSoil);
  draw_world(view, interpolation_alpha);
  draw_interface(view, paused, speed, simulation_limited);
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
    const float font_size = 16.0F / camera_.zoom();
    DrawTextEx(font_, TextFormat("%lld", static_cast<long long>(source.amount / 1000)),
               {center.x - 2.0F, center.y - 7.0F}, font_size, 0.5F / camera_.zoom(), kInk);
  }

  for (const sim::ActorSnapshot& actor : view.actors) {
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
  DrawRectangle(0, 0, width, 64, kPaper);
  DrawLine(0, 63, width, 63, kInk);
  draw_text("ANT FARM", 20, 14, 27);
  draw_text(TextFormat("Workers  %d", static_cast<int>(view.actors.size()) - 1), 170, 20, 20);
  draw_text(TextFormat("Carbs  %lld", static_cast<long long>(view.stores.carbohydrate / 1000)), 318,
            20, 20);
  draw_text(TextFormat("Protein  %lld", static_cast<long long>(view.stores.protein / 1000)), 444,
            20, 20);
  draw_text(
      TextFormat("Trips  %llu", static_cast<unsigned long long>(view.stats.completed_round_trips)),
      594, 20, 20);

  const float footer_y = static_cast<float>(height - 60);
  DrawRectangle(0, height - 60, width, 60, kPaper);
  DrawLine(0, height - 60, width, height - 60, kInk);
  draw_button({18.0F, footer_y + 9.0F, 94.0F, 42.0F}, paused ? "Resume" : "Pause", paused);
  draw_button({130.0F, footer_y + 9.0F, 58.0F, 42.0F}, "1x", speed == 1);
  draw_button({194.0F, footer_y + 9.0F, 58.0F, 42.0F}, "2x", speed == 2);
  draw_button({258.0F, footer_y + 9.0F, 58.0F, 42.0F}, "5x", speed == 5);
  draw_text(TextFormat("Seed %llu  |  Tick %llu  |  Zoom %.1fx",
                       static_cast<unsigned long long>(view.seed),
                       static_cast<unsigned long long>(view.tick), camera_.zoom()),
            350, height - 39, 18);
  if (width >= 1200) {
    draw_text("Drag: pan  |  Wheel: zoom  |  Click: inspect  |  I: panel", width - 590, height - 39,
              18, Color{71, 78, 72, 255});
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
  DrawRectangle(panel_x, 64, panel_width, height - 124, Color{238, 228, 204, 245});
  DrawLine(panel_x, 64, panel_x, height - 60, kInk);
  draw_text("COLONY", panel_x + 22, 84, 22);
  draw_text("A steady first forage", panel_x + 22, 122, 20);
  draw_text("Guided behavior (M1)", panel_x + 22, 153, 18, Color{85, 91, 84, 255});

  draw_text("STORES", panel_x + 22, 203, 20);
  draw_text(TextFormat("Carbohydrate   %lld / %lld",
                       static_cast<long long>(view.stores.carbohydrate / 1000),
                       static_cast<long long>(view.stores.capacity_per_nutrient / 1000)),
            panel_x + 22, 235, 18);
  draw_text(TextFormat("Protein          %lld / %lld",
                       static_cast<long long>(view.stores.protein / 1000),
                       static_cast<long long>(view.stores.capacity_per_nutrient / 1000)),
            panel_x + 22, 264, 18);

  draw_text("SELECTED", panel_x + 22, 321, 20);
  const sim::ActorSnapshot* selected = nullptr;
  for (const sim::ActorSnapshot& actor : view.actors) {
    if (selected_id_ == actor.id) {
      selected = &actor;
      break;
    }
  }
  if (selected == nullptr) {
    draw_text("Click an ant to inspect it.", panel_x + 22, 354, 18, Color{85, 91, 84, 255});
  } else {
    draw_text(TextFormat("%s  #%llu", selected->kind == sim::AntKind::Queen ? "Queen" : "Worker",
                         static_cast<unsigned long long>(selected->id)),
              panel_x + 22, 354, 20);
    if (selected->kind == sim::AntKind::Worker) {
      draw_text(state_name(selected->forage_state), panel_x + 22, 387, 18);
      if (selected->cargo_amount > 0) {
        draw_text(TextFormat("Carrying %lld %s",
                             static_cast<long long>(selected->cargo_amount / 1000),
                             nutrient_name(selected->cargo_nutrient)),
                  panel_x + 22, 417, 18);
      } else {
        draw_text("Cargo: empty", panel_x + 22, 417, 18, Color{85, 91, 84, 255});
      }
    } else {
      draw_text("Founding queen", panel_x + 22, 387, 18);
    }
  }

  draw_text("OBJECTIVE", panel_x + 22, 484, 20);
  if (view.stats.completed_round_trips == 0) {
    draw_text("Watch a worker bring food", panel_x + 22, 518, 18);
    draw_text("home to the nursery.", panel_x + 22, 546, 18);
  } else {
    draw_text("Food delivered to the colony", panel_x + 22, 518, 18);
    draw_text(TextFormat("%llu complete round trips",
                         static_cast<unsigned long long>(view.stats.completed_round_trips)),
              panel_x + 22, 546, 18, Color{65, 112, 91, 255});
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
}

} // namespace ant::presentation
