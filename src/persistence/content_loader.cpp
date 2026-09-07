#include "persistence/content_loader.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace ant::persistence {
namespace {

game::UpgradeId parse_upgrade_id(const std::string& id) {
  if (id == "excavation") return game::UpgradeId::Excavation;
  if (id == "nursing") return game::UpgradeId::Nursing;
  if (id == "foraging") return game::UpgradeId::Foraging;
  if (id == "queen") return game::UpgradeId::Queen;
  throw std::invalid_argument("unknown upgrade id: " + id);
}

} // namespace

game::ProgressionConfig load_progression(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open progression config: " + path.string());
  nlohmann::json document;
  try { input >> document; }
  catch (const nlohmann::json::exception& error) { throw std::invalid_argument(std::string("invalid progression JSON: ") + error.what()); }
  game::ProgressionConfig config;
  try {
    config.content_version = document.at("content_version").get<std::string>();
    config.productive_ticks_per_work = document.at("productive_ticks_per_work").get<std::uint64_t>();
    config.focus_cooldown_ticks = document.at("focus_cooldown_ticks").get<sim::Tick>();
    const auto& upgrades = document.at("upgrades");
    if (!upgrades.is_array() || upgrades.size() != 4) throw std::invalid_argument("progression must define four upgrades");
    for (const auto& item : upgrades) {
      game::UpgradeDefinition definition;
      definition.id = parse_upgrade_id(item.at("id").get<std::string>());
      definition.name = item.at("name").get<std::string>();
      definition.effect_percent_per_level = item.at("effect_percent_per_level").get<int>();
      definition.costs = item.at("costs").get<std::array<std::int64_t, 10>>();
      config.upgrades[game::upgrade_index(definition.id)] = std::move(definition);
    }
  } catch (const nlohmann::json::exception& error) {
    throw std::invalid_argument(std::string("invalid progression fields: ") + error.what());
  }
  std::string error;
  if (!game::validate_progression(config, error)) throw std::invalid_argument("invalid progression config: " + error);
  return config;
}

} // namespace ant::persistence
