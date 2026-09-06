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
  draw_button({194.0F, footer_y + 9.0F, 58.0F, 42.0F}, "2x", speed == 2);
  draw_button({258.0F, footer_y + 9.0F, 58.0F, 42.0F}, "5x", speed == 5);
  draw_text("Generation 1", 350, height - 39, 18);
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
  draw_text(TextFormat("Focus: %s", focus_name(view.focus)), panel_x + 22, 344, 16, kMutedInk);
  draw_text(TextFormat("%llu new cells  |  %llu births",
                       static_cast<unsigned long long>(view.stats.cells_excavated),
                       static_cast<unsigned long long>(view.stats.workers_born)),
            panel_x + 22, 367, 16, kMutedInk);

  DrawLine(panel_x + 20, 397, width - 20, 397, kWarmLine);
  draw_text("SELECTED", panel_x + 22, 416, 14, kMutedInk);
  const sim::ActorSnapshot* selected = nullptr;
  for (const sim::ActorSnapshot& actor : view.actors) {
    if (selected_id_ == actor.id) {
      selected = &actor;
      break;
    }
  }
  if (selected == nullptr) {
    draw_text("Click an ant to follow its work.", panel_x + 22, 444, 17, kMutedInk);
  } else {
    draw_text(TextFormat("%s  #%llu", selected->kind == sim::AntKind::Queen ? "Queen" : "Worker",
                         static_cast<unsigned long long>(selected->id)),
              panel_x + 22, 444, 20);
    if (selected->kind == sim::AntKind::Worker) {
      draw_text(sim::task_name(selected->task), panel_x + 22, 475, 18);
      if (selected->cargo_amount > 0) {
        const char* carried = selected->cargo_kind == sim::CargoKind::Spoil ? "Carrying spoil" :
                              selected->cargo_kind == sim::CargoKind::Corpse ? "Carrying a corpse" :
                              TextFormat("Carrying %lld %s", static_cast<long long>(selected->cargo_amount / 1000), nutrient_name(selected->cargo_nutrient));
        draw_text(carried, panel_x + 22, 504, 17);
      } else {
        draw_text("Cargo is empty", panel_x + 22, 504, 17, kMutedInk);
      }
    } else {
      draw_text(view.queen_alive ? "Founding queen | well tended" : "Queen deceased", panel_x + 22, 475, 17);
    }
  }

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
}

} // namespace ant::presentation
