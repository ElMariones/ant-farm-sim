#pragma once

#include "game/prestige.hpp"
#include "game/session.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ant::game {

enum class Bottleneck : std::uint8_t { None, Nutrition, Nursing, NurserySpace, QueenOutput, Labor };

// Profile-level progression the session itself does not own. The app fills this in from the
// committed profile after building the view.
struct LegacyView {
  bool between_runs{};
  std::int64_t wallet{};
  std::uint8_t vigor_tier{};
  std::uint8_t industry_tier{};
  std::int64_t vigor_cost{-1};
  std::int64_t industry_cost{-1};
  std::uint64_t generation{1};
  std::uint64_t successful_flights{};
  FlightPreview flight;
};

struct GameView {
  // The counters the interface watches from frame to frame to notice that something happened.
  struct Watched {
    std::uint64_t workers_born{};
    std::uint64_t gynes_born{};
    std::uint64_t sources_found{};
    std::uint64_t sources_exhausted{};
    std::uint64_t rooms_built{};
    std::size_t complete_rooms{};
    std::size_t complete_passages{};
    std::array<std::uint8_t, 4> upgrade_levels{};
  };
  [[nodiscard]] Watched watched() const;

  std::uint64_t seed{};
  sim::Tick tick{};
  int grid_width{};
  int grid_height{};
  std::vector<sim::Material> terrain;
  sim::GridPos home{};
  std::vector<sim::FoodSource> sources;
  std::vector<sim::Room> rooms;
  sim::Construction construction;
  // The site the colony is currently calling everyone to, while the alert lasts.
  sim::EntityId recruiting_source{};
  bool knows_any_food{};
  sim::FoodStore stores{};
  sim::WorldStats stats{};
  std::vector<sim::ActorSnapshot> actors;
  std::vector<sim::BroodSnapshot> brood;
  std::vector<sim::CorpseSnapshot> corpses;
  std::vector<sim::DroppedCargoSnapshot> dropped_food;
  std::vector<sim::FoodPile> granary;
  std::vector<std::uint16_t> trails;
  sim::TaskDiagnostics tasks{};
  int nursery_capacity{};
  int connected_nest_air{};
  std::uint64_t spoil_mound{};
  bool queen_alive{};
  bool decline{};
  bool extinct{};
  sim::Focus focus{sim::Focus::Balanced};
  bool focus_available{};
  sim::Tick focus_cooldown_remaining{};
  std::int64_t work{};
  std::uint64_t productive_tick_remainder{};
  std::array<std::uint8_t, 4> upgrade_levels{};
  std::array<std::int64_t, 4> upgrade_costs{};
  Bottleneck bottleneck{Bottleneck::None};
  LegacyView legacy;
  // Changes only when a cell is dug, so the renderer can cache the baked ground.
  std::uint64_t terrain_revision{};
};

[[nodiscard]] GameView make_view(const Session& session);
[[nodiscard]] const char* bottleneck_name(Bottleneck bottleneck);

} // namespace ant::game
