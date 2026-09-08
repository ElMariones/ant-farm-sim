#include "presentation/terrain_damage.hpp"

#include <algorithm>
#include <stdexcept>

namespace ant::presentation {
TerrainDamage terrain_damage(std::span<const sim::Material> before,
    std::span<const sim::Material> after, std::span<const sim::Room> old_rooms,
    std::span<const sim::Room> new_rooms, bool force_full) {
  constexpr std::size_t cells = sim::Grid::kWidth * sim::Grid::kHeight;
  if (after.size() != cells) throw std::invalid_argument("terrain damage requires the fixed world grid");
  TerrainDamage damage;
  if (force_full || before.size() != after.size()) {
    damage.chunks.fill(true);
    return damage;
  }
  for (std::size_t i = 0; i < cells; ++i) {
    if (before[i] == after[i]) continue;
    const int x = static_cast<int>(i % sim::Grid::kWidth), y = static_cast<int>(i / sim::Grid::kWidth);
    damage.include(x, y);
    damage.include(x - 1, y); damage.include(x + 1, y);
    damage.include(x, y - 1); damage.include(x, y + 1);
  }
  const auto include_room = [&](const sim::Room& room) {
    for (int dy = -room.reach(); dy <= room.reach(); ++dy)
      for (int dx = -room.reach(); dx <= room.reach(); ++dx)
        damage.include(room.centre.x + dx, room.centre.y + dy);
  };
  for (std::size_t i = 0; i < std::max(old_rooms.size(), new_rooms.size()); ++i) {
    if (i < old_rooms.size() && i < new_rooms.size() && old_rooms[i] == new_rooms[i]) continue;
    if (i < old_rooms.size()) include_room(old_rooms[i]);
    if (i < new_rooms.size()) include_room(new_rooms[i]);
  }
  return damage;
}
} // namespace ant::presentation
