#include "persistence/profile_codec.hpp"

#include "game/session.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include "sim/rng.hpp"
#include "sim/terrain_generation.hpp"

#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ant::persistence {
namespace {
using json = nlohmann::json;

template <typename Enum>
int enum_value(const Enum value) { return static_cast<int>(value); }

template <typename Enum>
Enum read_enum(const json& value, const int maximum, const char* name) {
  const int raw = value.get<int>();
  if (raw < 0 || raw > maximum) throw std::invalid_argument(std::string(name) + " is outside its legal range");
  return static_cast<Enum>(raw);
}

json position_json(const sim::GridPos p) { return json::array({p.x, p.y}); }
sim::GridPos read_position(const json& value) {
  if (!value.is_array() || value.size() != 2) throw std::invalid_argument("grid position must have two coordinates");
  return {value[0].get<int>(), value[1].get<int>()};
}

json progression_json(const game::ProgressionConfig& config) {
  json upgrades = json::array();
  for (const auto& definition : config.upgrades) {
    upgrades.push_back({{"id", game::upgrade_id_name(definition.id)}, {"name", definition.name},
                        {"effect_percent_per_level", definition.effect_percent_per_level}, {"costs", definition.costs}});
  }
  return {{"content_version", config.content_version}, {"productive_ticks_per_work", config.productive_ticks_per_work},
          {"focus_cooldown_ticks", config.focus_cooldown_ticks}, {"upgrades", std::move(upgrades)}};
}

game::UpgradeId read_upgrade_id(const std::string& value) {
  if (value == "excavation") return game::UpgradeId::Excavation;
  if (value == "nursing") return game::UpgradeId::Nursing;
  if (value == "foraging") return game::UpgradeId::Foraging;
  if (value == "queen") return game::UpgradeId::Queen;
  throw std::invalid_argument("unknown embedded upgrade id");
}

game::ProgressionConfig read_progression(const json& value) {
  game::ProgressionConfig config;
  config.content_version = value.at("content_version").get<std::string>();
  config.productive_ticks_per_work = value.at("productive_ticks_per_work").get<std::uint64_t>();
  config.focus_cooldown_ticks = value.at("focus_cooldown_ticks").get<sim::Tick>();
  const auto& upgrades = value.at("upgrades");
  if (!upgrades.is_array() || upgrades.size() != 4) throw std::invalid_argument("embedded progression requires four upgrades");
  std::array<bool, 4> seen{};
  for (const auto& item : upgrades) {
    game::UpgradeDefinition definition;
    definition.id = read_upgrade_id(item.at("id").get<std::string>());
    const std::size_t index = game::upgrade_index(definition.id);
    if (seen[index]) throw std::invalid_argument("duplicate embedded upgrade");
    seen[index] = true;
    definition.name = item.at("name").get<std::string>();
    definition.effect_percent_per_level = item.at("effect_percent_per_level").get<int>();
    definition.costs = item.at("costs").get<std::array<std::int64_t, 10>>();
    config.upgrades[index] = std::move(definition);
  }
  std::string error;
  if (!game::validate_progression(config, error)) throw std::invalid_argument(error);
  return config;
}

json stats_json(const sim::WorldStats& s) {
  return {{"path_requests", s.path_requests}, {"path_expansions", s.path_expansions},
          {"navigation_replans", s.navigation_replans}, {"picked_up", s.picked_up},
          {"delivered", s.delivered}, {"external_refill", s.external_refill},
          {"completed_round_trips", s.completed_round_trips}, {"cells_excavated", s.cells_excavated},
          {"spoil_delivered", s.spoil_delivered}, {"eggs_laid", s.eggs_laid},
          {"workers_born", s.workers_born}, {"deaths", s.deaths}, {"corpses_cleaned", s.corpses_cleaned},
          {"consumed_carbohydrate", s.consumed_carbohydrate}, {"consumed_protein", s.consumed_protein},
          {"decayed_food", s.decayed_food}, {"productive_worker_ticks", s.productive_worker_ticks},
          {"gynes_born", s.gynes_born}};
}

sim::WorldStats read_stats(const json& j) {
  sim::WorldStats s;
  s.path_requests=j.at("path_requests").get<std::uint64_t>(); s.path_expansions=j.at("path_expansions").get<std::uint64_t>();
  s.navigation_replans=j.at("navigation_replans").get<std::uint64_t>(); s.picked_up=j.at("picked_up").get<std::int64_t>();
  s.delivered=j.at("delivered").get<std::int64_t>(); s.external_refill=j.at("external_refill").get<std::int64_t>();
  s.completed_round_trips=j.at("completed_round_trips").get<std::uint64_t>(); s.cells_excavated=j.at("cells_excavated").get<std::uint64_t>();
  s.spoil_delivered=j.at("spoil_delivered").get<std::uint64_t>(); s.eggs_laid=j.at("eggs_laid").get<std::uint64_t>();
  s.workers_born=j.at("workers_born").get<std::uint64_t>(); s.deaths=j.at("deaths").get<std::uint64_t>();
  s.corpses_cleaned=j.at("corpses_cleaned").get<std::uint64_t>(); s.consumed_carbohydrate=j.at("consumed_carbohydrate").get<std::int64_t>();
  s.consumed_protein=j.at("consumed_protein").get<std::int64_t>(); s.decayed_food=j.at("decayed_food").get<std::int64_t>();
  s.productive_worker_ticks=j.at("productive_worker_ticks").get<std::uint64_t>();
  s.gynes_born=j.at("gynes_born").get<std::uint64_t>();
  return s;
}

json source_json(const sim::FoodSource& s) {
  return {{"id",s.id},{"position",position_json(s.position)},{"nutrient",enum_value(s.nutrient)},
          {"amount",s.amount},{"capacity",s.capacity},{"reserved",s.reserved},{"known",s.known},{"appeared",s.appeared}};
}
sim::FoodSource read_source(const json& j) {
  return {j.at("id").get<sim::EntityId>(),read_position(j.at("position")),read_enum<sim::Nutrient>(j.at("nutrient"),1,"nutrient"),
          j.at("amount").get<std::int64_t>(),j.at("capacity").get<std::int64_t>(),j.at("reserved").get<std::int64_t>(),
          j.at("known").get<bool>(),j.at("appeared").get<sim::Tick>()};
}

json actor_json(const sim::ActorState& a) {
  json result={{"id",a.identity.id},{"kind",enum_value(a.ant.kind)},{"worker",a.worker},
    {"position",{{"x",a.position.x_subcells},{"y",a.position.y_subcells},{"previous_x",a.position.previous_x_subcells},{"previous_y",a.position.previous_y_subcells}}},
    {"cargo",{{"kind",enum_value(a.cargo.kind)},{"nutrient",enum_value(a.cargo.nutrient)},{"amount",a.cargo.amount},{"entity_id",a.cargo.entity_id}}}};
  if (a.worker || a.ant.kind == sim::AntKind::Queen) {
    json path=json::array(); for (const auto p:a.movement.path) path.push_back(position_json(p));
    result["movement"]={{"path",std::move(path)},{"next_cell",a.movement.next_cell},{"path_revision",a.movement.path_revision},{"speed_residual",a.movement.speed_residual},{"path_valid",a.path_valid}};
  }
  if (a.worker) {
    result["forager"]={{"state",enum_value(a.forager.state)},{"source_id",a.forager.source_id},{"reserved_amount",a.forager.reserved_amount},{"reservation_expiry",a.forager.reservation_expiry},{"retry_after",a.forager.retry_after},{"store_cell",position_json(a.forager.store_cell)},{"scout_target",position_json(a.forager.scout_target)}};
    result["mind"]={{"task",enum_value(a.mind.task)},{"committed_until",a.mind.committed_until},{"thresholds",a.mind.thresholds},{"target",position_json(a.mind.target)},{"has_target",a.mind.has_target},{"action_ticks",a.mind.action_ticks}};
    result["life"]={{"age",a.life.age},{"lifespan",a.life.lifespan},{"starvation",a.life.starvation}};
  } else if (a.ant.kind == sim::AntKind::WingedQueen) {
    result["life"]={{"age",a.life.age},{"lifespan",a.life.lifespan},{"starvation",a.life.starvation}};
  }
  return result;
}

sim::ActorState read_actor(const json& j) {
  sim::ActorState a;
  a.identity.id=j.at("id").get<sim::EntityId>(); a.ant.kind=read_enum<sim::AntKind>(j.at("kind"),2,"ant kind"); a.worker=j.at("worker").get<bool>();
  const auto& p=j.at("position"); a.position={p.at("x").get<std::int32_t>(),p.at("y").get<std::int32_t>(),p.at("previous_x").get<std::int32_t>(),p.at("previous_y").get<std::int32_t>()};
  const auto& c=j.at("cargo"); a.cargo={read_enum<sim::CargoKind>(c.at("kind"),3,"cargo kind"),read_enum<sim::Nutrient>(c.at("nutrient"),1,"cargo nutrient"),c.at("amount").get<std::int64_t>(),c.at("entity_id").get<sim::EntityId>()};
  if (j.contains("movement")) {
    const auto& m=j.at("movement"); for(const auto& item:m.at("path")) a.movement.path.push_back(read_position(item)); a.movement.next_cell=m.at("next_cell").get<std::size_t>(); a.movement.path_revision=m.at("path_revision").get<std::uint64_t>(); a.path_valid=m.value("path_valid",true); a.movement.speed_residual=m.at("speed_residual").get<int>();
  }
  if (a.worker) {
    const auto& f=j.at("forager"); a.forager={read_enum<sim::ForageState>(f.at("state"),5,"forage state"),f.at("source_id").get<sim::EntityId>(),f.at("reserved_amount").get<std::int64_t>(),f.at("reservation_expiry").get<sim::Tick>(),f.at("retry_after").get<sim::Tick>(),read_position(f.at("store_cell")),read_position(f.at("scout_target"))};
    const auto& mind=j.at("mind"); a.mind.task=read_enum<sim::Task>(mind.at("task"),4,"task"); a.mind.committed_until=mind.at("committed_until").get<sim::Tick>(); a.mind.thresholds=mind.at("thresholds").get<std::array<std::uint16_t,4>>(); a.mind.target=read_position(mind.at("target")); a.mind.has_target=mind.at("has_target").get<bool>(); a.mind.action_ticks=mind.at("action_ticks").get<int>();
    const auto& life=j.at("life"); a.life={life.at("age").get<sim::Tick>(),life.at("lifespan").get<sim::Tick>(),life.at("starvation").get<sim::Tick>()};
  } else if (a.ant.kind == sim::AntKind::WingedQueen) {
    const auto& life=j.at("life"); a.life={life.at("age").get<sim::Tick>(),life.at("lifespan").get<sim::Tick>(),life.at("starvation").get<sim::Tick>()};
  }
  return a;
}

json world_json(const sim::WorldSnapshot& w) {
  json terrain=json::array(); for(const auto value:w.terrain) terrain.push_back(enum_value(value));
  json sources=json::array(); for(const auto& source:w.sources) sources.push_back(source_json(source));
  json brood=json::array(); for(const auto& b:w.brood) brood.push_back({{"id",b.id},{"stage",enum_value(b.stage)},{"position",position_json(b.position)},{"progress",b.progress},{"target",b.target},{"care_remaining",b.care_remaining},{"starvation",b.starvation},{"role",enum_value(b.role)},{"carried_by",b.carried_by}});
  json corpses=json::array(); for(const auto& c:w.corpses) corpses.push_back({{"id",c.id},{"position",position_json(c.position)},{"age",c.age},{"cleanable",c.cleanable}});
  json dropped=json::array(); for(const auto& d:w.dropped_food) dropped.push_back({{"id",d.id},{"position",position_json(d.position)},{"nutrient",enum_value(d.nutrient)},{"amount",d.amount},{"age",d.age}});
  json actors=json::array(); for(const auto& a:w.actors) actors.push_back(actor_json(a));
  json granary=json::array(); for(const auto& g:w.granary) granary.push_back({{"position",position_json(g.position)},{"nutrient",enum_value(g.nutrient)},{"amount",g.amount}});
  json rooms=json::array(); for(const auto& r:w.rooms) rooms.push_back({{"centre",position_json(r.centre)},{"radius",r.radius},{"kind",enum_value(r.kind)},{"complete",r.complete}});
  json passages = json::array();
  for (const auto& passage : w.passages) {
    json route = json::array();
    for (const auto cell : passage.route) route.push_back(position_json(cell));
    passages.push_back({{"route", std::move(route)}, {"room", passage.room}, {"complete", passage.complete}});
  }
  return {{"seed",w.seed},{"tick",w.tick},{"next_id",w.next_id},{"home",position_json(w.home)},
    {"terrain",std::move(terrain)},{"sources",std::move(sources)},
    {"stores",{{"carbohydrate",w.stores.carbohydrate},{"protein",w.stores.protein},{"carbohydrate_capacity",w.stores.carbohydrate_capacity},{"protein_capacity",w.stores.protein_capacity}}},
    {"stats",stats_json(w.stats)},
    {"rng",{{"behavior_state",w.behavior_rng_state},{"behavior_increment",w.behavior_rng_increment},{"lifecycle_state",w.lifecycle_rng_state},{"lifecycle_increment",w.lifecycle_rng_increment},{"world_state",w.world_rng_state},{"world_increment",w.world_rng_increment}}},
    {"trails",w.trails},{"dig_work",w.dig_work},
    {"task_diagnostics",{{"stimuli",w.task_diagnostics.stimuli},{"workers_by_task",w.task_diagnostics.workers_by_task}}},
    {"rooms",std::move(rooms)},{"passages",std::move(passages)},{"next_source_spawn",w.next_source_spawn},{"recruiting_source",w.recruiting_source},{"recruit_until",w.recruit_until},{"brood",std::move(brood)},{"corpses",std::move(corpses)},{"dropped_food",std::move(dropped)},{"granary",std::move(granary)},{"actors",std::move(actors)},
    {"spoil_mound",w.spoil_mound},{"starting_nest_air",w.starting_nest_air},{"connected_nest_air",w.connected_nest_air},{"nursery_capacity",w.nursery_capacity},
    {"next_laying",w.next_laying},{"queen_settle",w.queen_settle},{"queen_starvation",w.queen_starvation},{"queen_alive",w.queen_alive},{"decline",w.decline},{"extinct",w.extinct},
    {"focus",enum_value(w.focus)},{"adaptation_levels",w.adaptation_levels},
    {"traits",{{"vigor_tier",w.traits.vigor_tier},{"industry_tier",w.traits.industry_tier}}},
    {"mature",w.mature},{"egg_assignment_counter",w.egg_assignment_counter}};
}

sim::WorldSnapshot read_world(const json& j) {
  sim::WorldSnapshot w; w.seed=j.at("seed").get<std::uint64_t>(); w.tick=j.at("tick").get<sim::Tick>(); w.next_id=j.at("next_id").get<sim::EntityId>(); w.home=read_position(j.at("home"));
  const auto& terrain=j.at("terrain"); if(!terrain.is_array()||terrain.size()!=static_cast<std::size_t>(sim::Grid::kWidth*sim::Grid::kHeight)) throw std::invalid_argument("terrain array has wrong size"); w.terrain.reserve(terrain.size()); for(const auto& item:terrain) w.terrain.push_back(read_enum<sim::Material>(item,6,"material"));
  const auto& sources=j.at("sources"); if(!sources.is_array()||sources.size()>sim::kMaxFoodSources) throw std::invalid_argument("too many food sources"); for(const auto& item:sources) w.sources.push_back(read_source(item));
  const auto& stores=j.at("stores"); w.stores={stores.at("carbohydrate").get<std::int64_t>(),stores.at("protein").get<std::int64_t>(),stores.at("carbohydrate_capacity").get<std::int64_t>(),stores.at("protein_capacity").get<std::int64_t>()};
  w.stats=read_stats(j.at("stats")); const auto& rng=j.at("rng"); w.behavior_rng_state=rng.at("behavior_state").get<std::uint64_t>(); w.behavior_rng_increment=rng.at("behavior_increment").get<std::uint64_t>(); w.lifecycle_rng_state=rng.at("lifecycle_state").get<std::uint64_t>(); w.lifecycle_rng_increment=rng.at("lifecycle_increment").get<std::uint64_t>(); w.world_rng_state=rng.at("world_state").get<std::uint64_t>(); w.world_rng_increment=rng.at("world_increment").get<std::uint64_t>();
  w.trails=j.at("trails").get<std::vector<std::uint16_t>>(); w.dig_work=j.at("dig_work").get<std::vector<std::uint16_t>>();
  const auto& td=j.at("task_diagnostics"); w.task_diagnostics.stimuli=td.at("stimuli").get<std::array<std::uint16_t,4>>(); w.task_diagnostics.workers_by_task=td.at("workers_by_task").get<std::array<std::uint32_t,5>>();
  if (!j.at("rooms").is_array() || j.at("rooms").size() > sim::kMaxRooms) throw std::invalid_argument("room limit exceeded");
  for(const auto& item:j.at("rooms")) { const int radius=item.at("radius").get<int>(); if(radius<1||radius>sim::kRoomMaxRadius) throw std::invalid_argument("room radius outside its range"); w.rooms.push_back({read_position(item.at("centre")),static_cast<std::uint8_t>(radius),read_enum<sim::RoomKind>(item.at("kind"),1,"room kind"),item.at("complete").get<bool>()}); }
  const auto& passages = j.at("passages");
  if (!passages.is_array() || passages.size() > sim::kMaxPassages)
    throw std::invalid_argument("passage limit exceeded");
  for (const auto& item : passages) {
    sim::Passage passage;
    passage.room = item.at("room").get<int>();
    passage.complete = item.at("complete").get<bool>();
    const auto& route = item.at("route");
    if (!route.is_array() || route.empty() || route.size() > sim::kMaxPassageLength)
      throw std::invalid_argument("invalid passage length");
    for (const auto& cell : route) passage.route.push_back(read_position(cell));
    w.passages.push_back(std::move(passage));
  }
  w.next_source_spawn=j.at("next_source_spawn").get<sim::Tick>(); w.recruiting_source=j.at("recruiting_source").get<sim::EntityId>(); w.recruit_until=j.at("recruit_until").get<sim::Tick>();
  for(const auto& item:j.at("brood")) w.brood.push_back({item.at("id").get<sim::EntityId>(),read_enum<sim::BroodStage>(item.at("stage"),2,"brood stage"),read_position(item.at("position")),item.at("progress").get<sim::Tick>(),item.at("target").get<sim::Tick>(),item.at("care_remaining").get<sim::Tick>(),item.at("starvation").get<sim::Tick>(),read_enum<sim::BroodRole>(item.at("role"),1,"brood role"),item.value("carried_by",sim::EntityId{0})});
  for(const auto& item:j.at("corpses")) w.corpses.push_back({item.at("id").get<sim::EntityId>(),read_position(item.at("position")),item.at("age").get<sim::Tick>(),item.at("cleanable").get<bool>()});
  for(const auto& item:j.at("dropped_food")) w.dropped_food.push_back({item.at("id").get<sim::EntityId>(),read_position(item.at("position")),read_enum<sim::Nutrient>(item.at("nutrient"),1,"nutrient"),item.at("amount").get<std::int64_t>(),item.at("age").get<sim::Tick>()});
  // Saves written before food had a place in the nest carry no granary; the world rebuilds one
  // from the stored totals on load, so nothing is lost and nothing is duplicated.
  if(j.contains("granary")) for(const auto& item:j.at("granary")) w.granary.push_back({read_position(item.at("position")),read_enum<sim::Nutrient>(item.at("nutrient"),1,"nutrient"),item.at("amount").get<std::int64_t>()});
  for(const auto& item:j.at("actors")) w.actors.push_back(read_actor(item));
  w.spoil_mound=j.at("spoil_mound").get<std::uint64_t>(); w.starting_nest_air=j.at("starting_nest_air").get<int>(); w.connected_nest_air=j.at("connected_nest_air").get<int>(); w.nursery_capacity=j.at("nursery_capacity").get<int>();
  w.next_laying=j.at("next_laying").get<sim::Tick>(); w.queen_settle=j.value("queen_settle",sim::Tick{0}); w.queen_starvation=j.at("queen_starvation").get<sim::Tick>(); w.queen_alive=j.at("queen_alive").get<bool>(); w.decline=j.at("decline").get<bool>(); w.extinct=j.at("extinct").get<bool>(); w.focus=read_enum<sim::Focus>(j.at("focus"),3,"focus"); w.adaptation_levels=j.at("adaptation_levels").get<std::array<std::uint8_t,4>>();
  const auto& t=j.at("traits"); w.traits={t.at("vigor_tier").get<std::uint8_t>(),t.at("industry_tier").get<std::uint8_t>()};
  w.mature=j.at("mature").get<bool>(); w.egg_assignment_counter=j.at("egg_assignment_counter").get<std::uint64_t>();
  return w;
}

void validate_world(const sim::WorldSnapshot& w) {
  constexpr std::size_t cells=static_cast<std::size_t>(sim::Grid::kWidth*sim::Grid::kHeight);
  if(w.trails.size()!=cells||w.dig_work.size()!=cells) throw std::invalid_argument("field array has wrong size");
  if(w.actors.size()+w.brood.size()+w.corpses.size()+w.dropped_food.size()>20'000||w.brood.size()>5'000) throw std::invalid_argument("entity limit exceeded");
  if(!sim::Grid().in_bounds(w.home)||w.starting_nest_air<=0||w.connected_nest_air<w.starting_nest_air||w.nursery_capacity<12) throw std::invalid_argument("invalid nest dimensions");
  std::set<sim::EntityId> ids; sim::EntityId maximum=0; auto add=[&](sim::EntityId id){if(id==0||!ids.insert(id).second) throw std::invalid_argument("duplicate or zero entity id"); maximum=std::max(maximum,id);};
  auto passable=[&](sim::GridPos p){return p.x>=0&&p.x<sim::Grid::kWidth&&p.y>=0&&p.y<sim::Grid::kHeight&&sim::is_passable(w.terrain[static_cast<std::size_t>(p.y*sim::Grid::kWidth+p.x)]);};
  std::map<sim::EntityId,std::int64_t> reservations;
  for(const auto& source:w.sources){add(source.id); reservations[source.id]=0; if(source.amount<0||source.capacity<=0||source.amount>source.capacity||source.reserved<0||source.reserved>source.amount||!passable(source.position)) throw std::invalid_argument("invalid food source");}
  bool queen=false; sim::EntityId previous=0;
  for(const auto& actor:w.actors){add(actor.identity.id); if(actor.identity.id<=previous) throw std::invalid_argument("actors are not in stable id order"); previous=actor.identity.id; if(!passable(actor.position.cell())||actor.cargo.amount<0||actor.worker!=(actor.ant.kind==sim::AntKind::Worker)) throw std::invalid_argument("invalid actor state"); if(actor.movement.next_cell>actor.movement.path.size()||actor.movement.path.size()>cells||actor.movement.speed_residual<0||actor.movement.speed_residual>=sim::kTicksPerSecond) throw std::invalid_argument("invalid movement state"); if(actor.path_valid) for(const auto p:actor.movement.path) if(!passable(p)) throw std::invalid_argument("current path crosses an impassable cell"); if(!actor.worker){if(actor.ant.kind==sim::AntKind::Queen){if(queen) throw std::invalid_argument("multiple queens");queen=true;}continue;} if(actor.forager.reserved_amount<0) throw std::invalid_argument("invalid reservation"); if(actor.forager.reserved_amount>0){const auto slot=reservations.find(actor.forager.source_id); if(slot==reservations.end()) throw std::invalid_argument("reservation names a site that is gone"); slot->second+=actor.forager.reserved_amount;}}
  if(queen!=w.queen_alive) throw std::invalid_argument("queen state mismatch");
  for(const auto& source:w.sources) if(reservations[source.id]!=source.reserved) throw std::invalid_argument("source reservation mismatch");
  for(const auto& room:w.rooms) if(!sim::within_envelope(room.centre,w.home)) throw std::invalid_argument("room lies outside the excavation envelope");
  std::set<int> passage_rooms;
  for (const auto& passage : w.passages) {
    if (passage.room < -1 || passage.room >= static_cast<int>(w.rooms.size()) ||
        (passage.room >= 0 && !passage_rooms.insert(passage.room).second))
      throw std::invalid_argument("invalid passage room reference");
    std::set<sim::GridPos> visited;
    for (std::size_t i = 0; i < passage.route.size(); ++i) {
      const auto cell = passage.route[i];
      if (!sim::within_envelope(cell, w.home) || !visited.insert(cell).second)
        throw std::invalid_argument("invalid passage cell");
      const auto material = w.terrain[static_cast<std::size_t>(cell.y * sim::Grid::kWidth + cell.x)];
      if (material != sim::Material::Air && !sim::is_diggable(material))
        throw std::invalid_argument("passage crosses permanent rock");
      if (passage.complete && material != sim::Material::Air)
        throw std::invalid_argument("completed passage is not open");
      if (i > 0 && std::abs(cell.x - passage.route[i - 1].x) +
                       std::abs(cell.y - passage.route[i - 1].y) != 1)
        throw std::invalid_argument("passage route is not contiguous");
    }
    if (passage.room >= 0 && !w.rooms[static_cast<std::size_t>(passage.room)].contains(passage.route.back()))
      throw std::invalid_argument("passage misses its room");
  }
  for(const auto& b:w.brood){add(b.id);if(!passable(b.position)||b.target==0||b.progress>b.target) throw std::invalid_argument("invalid brood state");}
  for(const auto& c:w.corpses){add(c.id);if(!passable(c.position)) throw std::invalid_argument("invalid corpse position");}
  for(const auto& d:w.dropped_food){add(d.id);if(!passable(d.position)||d.amount<=0) throw std::invalid_argument("invalid dropped cargo");}
  std::array<std::int64_t,2> stored{}; std::set<std::pair<int,int>> pile_cells;
  for(const auto& g:w.granary){if(!passable(g.position)||g.amount<0||g.amount>sim::kGrainsPerStoreCell) throw std::invalid_argument("invalid food pile");
    if(!pile_cells.insert({g.position.x,g.position.y}).second) throw std::invalid_argument("two food piles in one cell");
    stored[static_cast<std::size_t>(g.nutrient)]+=g.amount;}
  if(!w.granary.empty()&&(stored[0]!=w.stores.carbohydrate||stored[1]!=w.stores.protein)) throw std::invalid_argument("stored food does not match the heaps in the nest");
  if(w.traits.vigor_tier>4||w.traits.industry_tier>4) throw std::invalid_argument("trait tier above four");
  const int winged=static_cast<int>(std::count_if(w.actors.begin(),w.actors.end(),[](const sim::ActorState& a){return a.ant.kind==sim::AntKind::WingedQueen;}))+
                   static_cast<int>(std::count_if(w.brood.begin(),w.brood.end(),[](const sim::BroodSnapshot& b){return b.role==sim::BroodRole::Gyne;}));
  if(winged>10) throw std::invalid_argument("winged queens and winged brood exceed the cap of ten");
  if(!w.mature&&w.egg_assignment_counter>0) throw std::invalid_argument("egg assignment advanced before maturity");
  if(w.next_id<=maximum) throw std::invalid_argument("next entity id is not monotonic");
  static_cast<void>(sim::World(w));
}

// Schema 1 (M3) had no castes, traits, maturity latch or flight receipt. Fill those in memory so
// an existing profile keeps its colony; nothing is written until the caller commits.
void migrate_v1_to_v2(json& document) {
  document["schema_version"] = 2;
  if (!document.contains("last_flight_receipt")) document["last_flight_receipt"] = nullptr;
  if (document.at("run").is_null()) return;
  json& world = document.at("run").at("world");
  world["traits"] = {{"vigor_tier", 0}, {"industry_tier", 0}};
  world["mature"] = false;
  world["egg_assignment_counter"] = 0;
  world.at("stats")["gynes_born"] = 0;
  for (json& item : world.at("brood")) item["role"] = static_cast<int>(sim::BroodRole::Worker);
}

// Schema 2 stocked two forage sites that refilled forever, dug wandering faces, and had no rooms.
// Sites become finite and already known, the faces are dropped, and the colony is given the
// founding room set at its usual offsets — where the old nest already has the space the rooms open
// complete, and where it does not the colony digs them out, which is the same work it would do for
// any room it decided it needed.
void migrate_v2_to_v3(json& document) {
  document["schema_version"] = 3;
  if (document.at("run").is_null()) return;
  json& world = document.at("run").at("world");
  const sim::GridPos home = read_position(world.at("home"));
  const auto tick = world.at("tick").get<sim::Tick>();
  const std::uint64_t seed = world.at("seed").get<std::uint64_t>();

  std::vector<sim::EntityId> source_ids;
  for (json& source : world.at("sources")) {
    source["known"] = true;
    source["appeared"] = 0;
    source.erase("refill_amount");
    source.erase("refill_interval");
    source.erase("next_refill");
    source_ids.push_back(source.at("id").get<sim::EntityId>());
  }
  world.at("rng")["world_state"] = sim::mix_seed(seed ^ 0xF0DDEEULL);
  world.at("rng")["world_increment"] = sim::mix_seed(seed ^ 0x5EED1EULL) | 1ULL;
  world.erase("dig_faces");
  world.erase("frontiers_valid");
  world["next_source_spawn"] = tick + 60ULL * sim::kTicksPerSecond;
  world["recruiting_source"] = 0;
  world["recruit_until"] = 0;
  // Where the old nest already has the space, the room opens finished; where it does not, the
  // colony digs it out, which is the same work it would do for any room it decided it needed.
  const auto& terrain = world.at("terrain");
  const auto material_at = [&terrain](const sim::GridPos cell) {
    if (cell.x < 0 || cell.x >= sim::Grid::kWidth || cell.y < 0 || cell.y >= sim::Grid::kHeight) {
      return sim::Material::Bedrock;
    }
    return static_cast<sim::Material>(
        terrain[static_cast<std::size_t>(cell.y * sim::Grid::kWidth + cell.x)].get<int>());
  };
  // The founding room set for this very seed, so a migrated colony is given the same chambers a
  // colony of that seed is founded with rather than a guess that might land in solid ground.
  json rooms = json::array();
  for (const sim::Room& shape : sim::generate_terrain(seed).rooms) {
    bool carved = true;
    for (int y = -shape.reach(); y <= shape.reach() && carved; ++y) {
      for (int x = -shape.reach(); x <= shape.reach(); ++x) {
        const sim::GridPos cell{shape.centre.x + x, shape.centre.y + y};
        if (!shape.contains(cell) || !sim::within_envelope(cell, home)) continue;
        if (sim::is_diggable(material_at(cell))) { carved = false; break; }
      }
    }
    rooms.push_back({{"centre", json::array({shape.centre.x, shape.centre.y})},
                     {"radius", shape.radius},
                     {"kind", enum_value(shape.kind)},
                     {"complete", carved}});
  }
  world["rooms"] = std::move(rooms);

  for (json& actor : world.at("actors")) {
    if (!actor.at("worker").get<bool>()) continue;
    json& forager = actor.at("forager");
    const int index = forager.value("source_index", -1);
    forager.erase("source_index");
    forager["source_id"] =
        index >= 0 && static_cast<std::size_t>(index) < source_ids.size() ? source_ids[static_cast<std::size_t>(index)] : 0;
    if (!forager.contains("store_cell")) forager["store_cell"] = json::array({0, 0});
    forager["scout_target"] = json::array({0, 0});
  }
}

json profile_json(const game::ProfileSnapshot& p) {
  json run=nullptr;
  if(p.run){const auto& r=*p.run;run={{"run_id",r.run_id},{"embedded_progression",progression_json(r.embedded_progression)},{"work",r.work},{"productive_tick_remainder",r.productive_tick_remainder},{"accounted_productive_ticks",r.accounted_productive_ticks},{"upgrade_levels",r.upgrade_levels},{"next_focus_change_tick",r.next_focus_change_tick},{"assisted",r.assisted},{"world",world_json(r.world)}};}
  return {{"schema_version",p.schema_version},{"revision",p.revision},{"content_version",p.content_version},{"profile_id",p.profile_id},{"phase",p.phase==game::ProfilePhase::ActiveRun?"ActiveRun":"BetweenRuns"},
    {"meta",{{"legacy_wallet",p.meta.legacy_wallet},{"legacy_earned_total",p.meta.legacy_earned_total},{"legacy_spent_total",p.meta.legacy_spent_total},{"vigor_tier",p.meta.vigor_tier},{"industry_tier",p.meta.industry_tier},{"generation",p.meta.generation},{"successful_flights",p.meta.successful_flights}}},
    {"settings",{{"ui_scale_percent",p.settings.ui_scale_percent},{"reduced_motion",p.settings.reduced_motion},{"preferred_speed",p.settings.preferred_speed}}},
    {"last_flight_receipt",p.last_flight_receipt?json{{"run_id",p.last_flight_receipt->run_id},{"earned_legacy",p.last_flight_receipt->earned_legacy},{"births",p.last_flight_receipt->births},{"winged_queens",p.last_flight_receipt->winged_queens}}:json(nullptr)},
    {"run",std::move(run)}};
}

game::ProfileSnapshot read_profile(const json& j) {
  game::ProfileSnapshot p; p.schema_version=j.at("schema_version").get<std::uint32_t>(); if(p.schema_version!=game::kCurrentSchemaVersion) throw std::invalid_argument("unsupported schema_version " + std::to_string(p.schema_version)); p.revision=j.at("revision").get<std::uint64_t>(); p.content_version=j.at("content_version").get<std::string>(); p.profile_id=j.at("profile_id").get<std::string>(); const std::string phase=j.at("phase").get<std::string>(); if(phase=="ActiveRun")p.phase=game::ProfilePhase::ActiveRun;else if(phase=="BetweenRuns")p.phase=game::ProfilePhase::BetweenRuns;else throw std::invalid_argument("invalid profile phase");
  const auto& m=j.at("meta");p.meta={m.at("legacy_wallet").get<std::int64_t>(),m.at("legacy_earned_total").get<std::int64_t>(),m.at("legacy_spent_total").get<std::int64_t>(),m.at("vigor_tier").get<std::uint8_t>(),m.at("industry_tier").get<std::uint8_t>(),m.at("generation").get<std::uint64_t>(),m.at("successful_flights").get<std::uint64_t>()};
  const auto& s=j.at("settings");p.settings={s.at("ui_scale_percent").get<int>(),s.at("reduced_motion").get<bool>(),s.at("preferred_speed").get<int>()};
  if(!j.at("last_flight_receipt").is_null()){const auto& r=j.at("last_flight_receipt");game::FlightReceipt receipt;receipt.run_id=r.at("run_id").get<std::string>();receipt.earned_legacy=r.at("earned_legacy").get<std::int64_t>();receipt.births=r.at("births").get<std::uint64_t>();receipt.winged_queens=r.at("winged_queens").get<int>();if(receipt.run_id.empty()||receipt.run_id.size()>128||receipt.earned_legacy<0||receipt.winged_queens<0)throw std::invalid_argument("invalid flight receipt");p.last_flight_receipt=std::move(receipt);}
  if(!j.at("run").is_null()){const auto& r=j.at("run");game::RunSnapshot run;run.run_id=r.at("run_id").get<std::string>();run.embedded_progression=read_progression(r.at("embedded_progression"));run.work=r.at("work").get<std::int64_t>();run.productive_tick_remainder=r.at("productive_tick_remainder").get<std::uint64_t>();run.accounted_productive_ticks=r.at("accounted_productive_ticks").get<std::uint64_t>();run.upgrade_levels=r.at("upgrade_levels").get<std::array<std::uint8_t,4>>();run.next_focus_change_tick=r.at("next_focus_change_tick").get<sim::Tick>();run.assisted=r.at("assisted").get<bool>();run.world=read_world(r.at("world"));p.run=std::move(run);}
  if(p.profile_id.empty()||p.profile_id.size()>128||p.content_version.empty()||p.content_version.size()>64) throw std::invalid_argument("invalid profile identity");
  if(p.meta.legacy_wallet<0||p.meta.legacy_earned_total<0||p.meta.legacy_spent_total<0||p.meta.legacy_wallet!=p.meta.legacy_earned_total-p.meta.legacy_spent_total||p.meta.vigor_tier>4||p.meta.industry_tier>4||p.meta.generation==0) throw std::invalid_argument("invalid meta accounting");
  if(p.settings.ui_scale_percent<75||p.settings.ui_scale_percent>200||(p.settings.preferred_speed!=1&&p.settings.preferred_speed!=5&&p.settings.preferred_speed!=20)) throw std::invalid_argument("invalid settings");
  if((p.phase==game::ProfilePhase::ActiveRun)!=p.run.has_value()) throw std::invalid_argument("profile phase and run disagree");
  if(p.run){if(p.run->run_id.empty()||p.run->run_id.size()>128||p.run->work<0) throw std::invalid_argument("invalid run fields");validate_world(p.run->world);static_cast<void>(game::Session::restore(*p.run));}
  return p;
}
} // namespace

std::string encode_profile(const game::ProfileSnapshot& profile) { return profile_json(profile).dump(); }

DecodeResult decode_profile(const std::string_view document) {
  if(document.size()>kMaximumProfileBytes) return {std::nullopt,"profile exceeds 64 MiB"};
  try {
    json parsed=json::parse(document.begin(),document.end());
    const std::uint32_t version=parsed.at("schema_version").get<std::uint32_t>();
    if(version>game::kCurrentSchemaVersion) throw std::invalid_argument("unsupported schema_version " + std::to_string(version));
    if(version==1) migrate_v1_to_v2(parsed);
    if(version<=2) migrate_v2_to_v3(parsed);
    if(version >= 1 && version <= 3) {
      parsed["schema_version"] = 4;
      if (!parsed.at("run").is_null()) parsed["run"]["world"]["passages"] = json::array();
    }
    return {read_profile(parsed),{}};
  }
  catch(const std::exception& error){return {std::nullopt,error.what()};}
}

game::ProfileSnapshot make_new_profile(const game::Session& session, const std::uint64_t revision) {
  std::ostringstream id; id<<"profile-"<<std::hex<<std::setw(16)<<std::setfill('0')<<session.world().seed();
  game::ProfileSnapshot profile; profile.revision=revision; profile.content_version=session.progression().content_version; profile.profile_id=id.str(); profile.run=session.snapshot(); return profile;
}

} // namespace ant::persistence
