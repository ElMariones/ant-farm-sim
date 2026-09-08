#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "persistence/profile_codec.hpp"
#include "sim/snapshot.hpp"

#include "sim/terrain_generation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>

namespace {

using json = nlohmann::json;

// Advances a colony to a state that has cargo in transit, brood in development, live paths,
// deposited trails and both RNG streams well past their seeded start.
ant::game::Session busy_session(const std::uint64_t seed = 42, const ant::sim::Tick ticks = 6'000) {
  ant::game::Session session(seed);
  session.step_ticks(ticks);
  return session;
}

bool has_cargo(const ant::sim::World& world) {
  for (const auto& actor : world.actors()) {
    if (actor.cargo_amount > 0) return true;
  }
  return false;
}

// Steps on from a warmed colony until at least one worker is actually carrying something, so the
// round-trip cases exercise cargo in transit rather than whatever a fixed tick happened to hold.
ant::game::Session session_carrying_cargo(const std::uint64_t seed = 42) {
  ant::game::Session session = [seed] {
    ant::game::Session warmed(seed);
    warmed.step_ticks(6'000);
    return warmed;
  }();
  for (int tick = 0; tick < 2'000 && !has_cargo(session.world()); ++tick) session.step();
  return session;
}

// The first grid index whose material cannot be stood on.
std::size_t first_solid_cell(const json& terrain) {
  for (std::size_t index = 0; index < terrain.size(); ++index) {
    if (!ant::sim::is_passable(static_cast<ant::sim::Material>(terrain[index].get<int>()))) {
      return index;
    }
  }
  return 0;
}

json decode_to_json(const ant::game::ProfileSnapshot& profile) {
  return json::parse(ant::persistence::encode_profile(profile));
}

} // namespace

TEST_CASE("a busy colony round-trips through the snapshot codec", "[persistence][snapshot]") {
  ant::game::Session session = session_carrying_cargo();
  const ant::sim::World& world = session.world();
  REQUIRE(has_cargo(world));
  REQUIRE_FALSE(world.brood().empty());
  REQUIRE(world.trails().mass() > 0);
  REQUIRE(world.stats().path_requests > 0);

  const ant::game::ProfileSnapshot profile = ant::persistence::make_new_profile(session);
  const ant::persistence::DecodeResult decoded =
      ant::persistence::decode_profile(ant::persistence::encode_profile(profile));
  REQUIRE(decoded.profile.has_value());
  REQUIRE(decoded.profile->run.has_value());

  ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);
  CHECK(restored.world().tick() == world.tick());
  CHECK(restored.world().canonical_hash() == world.canonical_hash());
  CHECK(restored.work() == session.work());
  CHECK(restored.productive_tick_remainder() == session.productive_tick_remainder());
  CHECK(restored.upgrade_levels() == session.upgrade_levels());
  CHECK(restored.world().trails().mass() == world.trails().mass());
  CHECK(restored.world().brood().size() == world.brood().size());
  CHECK(restored.world().stores().carbohydrate == world.stores().carbohydrate);
  CHECK(restored.world().invariant_holds());
}

TEST_CASE("saving and loading does not change the future of a run",
          "[persistence][snapshot][determinism]") {
  ant::game::Session uninterrupted = busy_session();
  ant::game::Session to_save = busy_session();
  REQUIRE(uninterrupted.world().canonical_hash() == to_save.world().canonical_hash());

  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(to_save)));
  REQUIRE(decoded.profile.has_value());
  ant::game::Session resumed = ant::game::Session::restore(*decoded.profile->run);

  // Continue both for long enough that any dropped path, reservation or RNG bit diverges.
  uninterrupted.step_ticks(4'000);
  resumed.step_ticks(4'000);

  CHECK(resumed.world().tick() == uninterrupted.world().tick());
  CHECK(resumed.world().canonical_hash() == uninterrupted.world().canonical_hash());
  CHECK(resumed.work() == uninterrupted.work());
  CHECK(resumed.world().stats().delivered == uninterrupted.world().stats().delivered);
  CHECK(resumed.world().stats().workers_born == uninterrupted.world().stats().workers_born);
  CHECK(resumed.world().stats().cells_excavated == uninterrupted.world().stats().cells_excavated);
  CHECK(resumed.world().invariant_holds());
}

TEST_CASE("purchased adaptations and focus survive a round trip", "[persistence][snapshot]") {
  ant::game::Session session = busy_session();
  session.debug_grant_work(500);
  REQUIRE(session.buy_upgrade(ant::game::UpgradeId::Excavation).accepted);
  REQUIRE(session.buy_upgrade(ant::game::UpgradeId::Foraging).accepted);
  // Run on until focus unlocks rather than assuming a particular tick, so a change in growth
  // pacing does not look like a persistence failure.
  for (int tick = 0; tick < 8'000 && !session.focus_available(); ++tick) session.step();
  REQUIRE(session.focus_available());
  REQUIRE(session.set_focus_command(ant::sim::Focus::Expansion).accepted);
  session.step_ticks(50);

  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  REQUIRE(decoded.profile.has_value());
  ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);

  CHECK(restored.upgrade_levels()[0] == 1);
  CHECK(restored.upgrade_levels()[2] == 1);
  CHECK(restored.world().adaptation_levels() == session.world().adaptation_levels());
  CHECK(restored.world().focus() == ant::sim::Focus::Expansion);
  CHECK(restored.focus_cooldown_remaining() == session.focus_cooldown_remaining());
  CHECK(restored.assisted());
  CHECK(restored.world().canonical_hash() == session.world().canonical_hash());
}

TEST_CASE("malformed, oversized and unknown-schema input is rejected without touching live state",
          "[persistence][snapshot]") {
  ant::game::Session live = busy_session(7, 2'000);
  const std::uint64_t live_hash = live.world().canonical_hash();
  const ant::game::ProfileSnapshot profile = ant::persistence::make_new_profile(live);

  SECTION("not JSON at all") {
    const auto result = ant::persistence::decode_profile("{ this is not json");
    CHECK_FALSE(result.profile.has_value());
    CHECK_FALSE(result.error.empty());
  }
  SECTION("empty document") {
    CHECK_FALSE(ant::persistence::decode_profile("").profile.has_value());
  }
  SECTION("JSON that is not a profile object") {
    CHECK_FALSE(ant::persistence::decode_profile("[1, 2, 3]").profile.has_value());
  }
  SECTION("oversized document") {
    const std::string oversized(ant::persistence::kMaximumProfileBytes + 1, 'x');
    const auto result = ant::persistence::decode_profile(oversized);
    CHECK_FALSE(result.profile.has_value());
    CHECK(result.error.find("64 MiB") != std::string::npos);
  }
  SECTION("newer schema version") {
    json document = decode_to_json(profile);
    document["schema_version"] = ant::game::kCurrentSchemaVersion + 1;
    const auto result = ant::persistence::decode_profile(document.dump());
    CHECK_FALSE(result.profile.has_value());
    CHECK(result.error.find("schema_version") != std::string::npos);
  }
  SECTION("missing required field") {
    json document = decode_to_json(profile);
    document.erase("meta");
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }

  // Every rejection above decodes into a throwaway DTO; the running colony is untouched.
  CHECK(live.world().canonical_hash() == live_hash);
  CHECK(live.world().invariant_holds());
}

TEST_CASE("duplicate identifiers and broken references are rejected",
          "[persistence][snapshot]") {
  ant::game::Session session = busy_session(7, 3'000);
  const ant::game::ProfileSnapshot profile = ant::persistence::make_new_profile(session);
  REQUIRE(decode_to_json(profile)["run"]["world"]["actors"].size() > 2);

  SECTION("duplicate actor id") {
    json document = decode_to_json(profile);
    auto& actors = document["run"]["world"]["actors"];
    actors[1]["id"] = actors[0]["id"];
    const auto result = ant::persistence::decode_profile(document.dump());
    CHECK_FALSE(result.profile.has_value());
    CHECK_FALSE(result.error.empty());
  }
  SECTION("next entity id is not monotonic") {
    json document = decode_to_json(profile);
    document["run"]["world"]["next_id"] = 1;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("reservation does not match its source") {
    json document = decode_to_json(profile);
    document["run"]["world"]["sources"][0]["reserved"] =
        document["run"]["world"]["sources"][0]["reserved"].get<std::int64_t>() + 1'000;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("terrain array has the wrong size") {
    json document = decode_to_json(profile);
    document["run"]["world"]["terrain"] = json::array({0, 1, 2});
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("trail field has the wrong size") {
    json document = decode_to_json(profile);
    document["run"]["world"]["trails"] = json::array({0, 0});
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("a current path crossing solid ground") {
    json document = decode_to_json(profile);
    for (json& item : document["run"]["world"]["actors"]) {
      if (!item.at("worker").get<bool>() || item.at("movement").at("path").empty()) continue;
      item["movement"]["path_valid"] = true;
      const std::size_t solid = first_solid_cell(document["run"]["world"]["terrain"]);
      item["movement"]["path"][0] = json::array({static_cast<int>(solid % ant::sim::Grid::kWidth),
                                                 static_cast<int>(solid / ant::sim::Grid::kWidth)});
      break;
    }
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("an actor stands inside solid rock") {
    json document = decode_to_json(profile);
    const std::size_t solid = first_solid_cell(document["run"]["world"]["terrain"]);
    const int x = static_cast<int>(solid % ant::sim::Grid::kWidth);
    const int y = static_cast<int>(solid / ant::sim::Grid::kWidth);
    auto& position = document["run"]["world"]["actors"][0]["position"];
    position["x"] = x * ant::sim::kSubcellsPerCell + ant::sim::kSubcellsPerCell / 2;
    position["y"] = y * ant::sim::kSubcellsPerCell + ant::sim::kSubcellsPerCell / 2;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("upgrade level exceeds its maximum") {
    json document = decode_to_json(profile);
    document["run"]["upgrade_levels"] = json::array({11, 0, 0, 0});
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("Work counters disagree with the world") {
    json document = decode_to_json(profile);
    document["run"]["accounted_productive_ticks"] =
        document["run"]["accounted_productive_ticks"].get<std::uint64_t>() + 7;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("wallet does not equal earned minus spent") {
    json document = decode_to_json(profile);
    document["meta"]["legacy_wallet"] = 5;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("phase and run disagree") {
    json document = decode_to_json(profile);
    document["phase"] = "BetweenRuns";
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
  SECTION("settings are out of range") {
    json document = decode_to_json(profile);
    document["settings"]["preferred_speed"] = 3;
    CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
  }
}

TEST_CASE("an active run keeps its embedded content when the shipped balance changes",
          "[persistence][snapshot][content]") {
  ant::game::ProgressionConfig shipped = ant::game::canonical_progression();
  shipped.content_version = "test-original";
  ant::game::Session session(42, shipped);
  session.step_ticks(1'500);

  const ant::game::ProfileSnapshot profile = ant::persistence::make_new_profile(session);
  const std::string document = ant::persistence::encode_profile(profile);

  // A later build ships different numbers; the saved run must not silently adopt them.
  ant::game::ProgressionConfig updated = ant::game::canonical_progression();
  updated.content_version = "test-updated";
  updated.productive_ticks_per_work = 50;
  for (auto& upgrade : updated.upgrades) {
    for (std::int64_t& cost : upgrade.costs) cost *= 10;
  }
  std::string error;
  REQUIRE(ant::game::validate_progression(updated, error));

  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(document);
  REQUIRE(decoded.profile.has_value());
  ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);

  CHECK(restored.progression().content_version == "test-original");
  CHECK(restored.progression().productive_ticks_per_work == 1'200);
  CHECK(restored.next_upgrade_cost(ant::game::UpgradeId::Excavation) == 15);
  CHECK(restored.world().canonical_hash() == session.world().canonical_hash());

  // A brand-new run does use the current shipped content.
  const ant::game::Session fresh(42, updated);
  CHECK(fresh.progression().content_version == "test-updated");
  CHECK(fresh.next_upgrade_cost(ant::game::UpgradeId::Excavation) == 150);
}

TEST_CASE("an embedded progression that is itself invalid is rejected",
          "[persistence][snapshot][content]") {
  ant::game::Session session = busy_session(42, 500);
  json document = decode_to_json(ant::persistence::make_new_profile(session));

  document["run"]["embedded_progression"]["upgrades"][0]["costs"][4] = 1;
  CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());

  document = decode_to_json(ant::persistence::make_new_profile(session));
  document["run"]["embedded_progression"]["upgrades"][1]["id"] = "excavation";
  CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());

  document = decode_to_json(ant::persistence::make_new_profile(session));
  document["run"]["embedded_progression"]["upgrades"][2]["id"] = "telepathy";
  CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile.has_value());
}

TEST_CASE("a resumed run stays paused at its saved tick", "[persistence][snapshot]") {
  ant::game::Session session = busy_session(101, 2'000);
  const ant::sim::Tick saved_tick = session.world().tick();
  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  REQUIRE(decoded.profile.has_value());

  const ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);
  // Restoring advances nothing on its own: no offline progress.
  CHECK(restored.world().tick() == saved_tick);
  CHECK(restored.world().stats().delivered == session.world().stats().delivered);
  CHECK(restored.world().canonical_hash() == session.world().canonical_hash());
}


namespace {

// Rewrites a current profile document into the schema 1 shape M3 shipped, so the migration is
// exercised against a real legacy document rather than a hand-written stub. Everything the newer
// schemas added has to go, including the fields schema 3 introduced.
json downgrade_to_schema_one(json document) {
  document["schema_version"] = 1;
  document.erase("last_flight_receipt");
  json& world = document.at("run").at("world");
  world.erase("traits");
  world.erase("mature");
  world.erase("egg_assignment_counter");
  world.at("stats").erase("gynes_born");
  for (json& item : world.at("brood")) item.erase("role");

  // Schema 3 gave sites an end and rooms a purpose; schema 1 had neither.
  world.erase("rooms");
  world.erase("next_source_spawn");
  world.erase("recruiting_source");
  world.erase("recruit_until");
  world.at("rng").erase("world_state");
  world.at("rng").erase("world_increment");
  std::vector<ant::sim::EntityId> source_ids;
  for (json& source : world.at("sources")) {
    source_ids.push_back(source.at("id").get<ant::sim::EntityId>());
    source.erase("known");
    source.erase("appeared");
    source["refill_amount"] = 800;
    source["refill_interval"] = 20;
    source["next_refill"] = 20;
  }
  for (json& actor : world.at("actors")) {
    if (!actor.at("worker").get<bool>()) continue;
    json& forager = actor.at("forager");
    const auto id = forager.at("source_id").get<ant::sim::EntityId>();
    const auto slot = std::find(source_ids.begin(), source_ids.end(), id);
    forager["source_index"] =
        slot == source_ids.end() ? -1 : static_cast<int>(slot - source_ids.begin());
    forager.erase("source_id");
    forager.erase("scout_target");
  }
  return document;
}

} // namespace

TEST_CASE("a schema 1 profile migrates forward and keeps its colony",
          "[persistence][snapshot][migration]") {
  ant::game::Session session = busy_session(42, 4'000);
  REQUIRE_FALSE(session.world().brood().empty());
  const json current = decode_to_json(ant::persistence::make_new_profile(session));
  const json legacy = downgrade_to_schema_one(current);

  // The old document really is missing the schema 2 fields.
  REQUIRE_FALSE(legacy.at("run").at("world").contains("traits"));
  REQUIRE_FALSE(legacy.contains("last_flight_receipt"));

  const ant::persistence::DecodeResult decoded =
      ant::persistence::decode_profile(legacy.dump());
  REQUIRE(decoded.profile.has_value());
  CHECK(decoded.profile->schema_version == ant::game::kCurrentSchemaVersion);
  CHECK_FALSE(decoded.profile->last_flight_receipt.has_value());
  REQUIRE(decoded.profile->run.has_value());

  const ant::sim::WorldSnapshot& world = decoded.profile->run->world;
  CHECK(world.traits == ant::sim::TraitModifiers{});
  CHECK_FALSE(world.mature);
  CHECK(world.egg_assignment_counter == 0);
  CHECK(world.stats.gynes_born == 0);
  for (const auto& item : world.brood) CHECK(item.role == ant::sim::BroodRole::Worker);

  // Schema 3 fields the old document never had are filled in rather than guessed at: the sites the
  // colony had are the ones it knows, and it is given the founding rooms, opened where the ground
  // it already dug has the space for them.
  CHECK_FALSE(world.sources.empty());
  for (const auto& source : world.sources) CHECK(source.known);
  // The founding room set for the profile's own seed, opened where the ground it already dug has
  // the space for them.
  REQUIRE(world.rooms.size() ==
          ant::sim::generate_terrain(world.seed).rooms.size());
  CHECK(world.rooms.front().kind == ant::sim::RoomKind::Nursery);
  CHECK(std::any_of(world.rooms.begin(), world.rooms.end(), [](const ant::sim::Room& room) {
    return room.kind == ant::sim::RoomKind::Granary;
  }));
  for (const auto& room : world.rooms) CHECK(room.complete);

  // The migrated colony is still the same colony: same moment, same ground, same ants, same food.
  const ant::game::Session restored = ant::game::Session::restore(*decoded.profile->run);
  CHECK(restored.world().tick() == session.world().tick());
  CHECK(restored.world().grid().material_hash() == session.world().grid().material_hash());
  CHECK(restored.world().living_workers() == session.world().living_workers());
  CHECK(restored.world().brood().size() == session.world().brood().size());
  CHECK(restored.world().stores().carbohydrate == session.world().stores().carbohydrate);
  CHECK(restored.world().stores().protein == session.world().stores().protein);
  CHECK(restored.world().invariant_holds());
}

TEST_CASE("a schema 1 profile that is still invalid is rejected after migration",
          "[persistence][snapshot][migration]") {
  ant::game::Session session = busy_session(42, 800);
  json legacy = downgrade_to_schema_one(decode_to_json(ant::persistence::make_new_profile(session)));
  legacy["run"]["world"]["next_id"] = 1;
  CHECK_FALSE(ant::persistence::decode_profile(legacy.dump()).profile.has_value());
}


TEST_CASE("stale paths and frontier caches keep their staleness across a save",
          "[persistence][snapshot][determinism]") {
  // Regression: restoring used to mark every saved path and the frontier cache as current, so a
  // colony that owed itself a replan resumed without one and diverged from an uninterrupted run.
  ant::game::Session live = session_carrying_cargo(7);
  // Change the terrain so the live world now holds stale navigation state.
  live.debug_world().debug_grid().set({live.world().home().x + 2, 40}, ant::sim::Material::Soil);

  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(live)));
  REQUIRE(decoded.profile.has_value());
  ant::game::Session resumed = ant::game::Session::restore(*decoded.profile->run);
  REQUIRE(resumed.world().canonical_hash() == live.world().canonical_hash());

  live.step_ticks(2'000);
  resumed.step_ticks(2'000);
  CHECK(resumed.world().canonical_hash() == live.world().canonical_hash());
  CHECK(resumed.world().stats().navigation_replans == live.world().stats().navigation_replans);
  CHECK(resumed.world().stats().path_requests == live.world().stats().path_requests);
}

TEST_CASE("clearing a route resets its cursor", "[world][navigation][persistence]") {
  // A path and its cursor must be cleared together, or the snapshot records a cursor past the end
  // of an empty path and the profile is rejected as invalid.
  ant::game::Session session(42);
  for (int tick = 0; tick < 40'000; ++tick) {
    session.step();
    if (session.world().stats().spoil_delivered == 0) continue;
    // Just after spoil is delivered the carrier's route is cleared.
    for (const auto& actor : session.world().snapshot().actors) {
      if (!actor.worker) continue;
      CHECK(actor.movement.next_cell <= actor.movement.path.size());
    }
    break;
  }
  REQUIRE(session.world().stats().spoil_delivered > 0);
  const ant::persistence::DecodeResult decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  CHECK(decoded.profile.has_value());
  CHECK(decoded.error.empty());
}

TEST_CASE("passage intent survives saving midway through excavation", "[nest-network][snapshot]") {
  auto session = busy_session(42, 1200);
  REQUIRE_FALSE(session.world().passages().empty());
  const auto document = ant::persistence::encode_profile(ant::persistence::make_new_profile(session));
  const auto decoded = ant::persistence::decode_profile(document);
  REQUIRE(decoded.profile);
  auto restored = ant::game::Session::restore(*decoded.profile->run);
  CHECK(restored.world().passages() == session.world().passages());
  session.step_ticks(1200);
  restored.step_ticks(1200);
  CHECK(restored.world().canonical_hash() == session.world().canonical_hash());
}

TEST_CASE("schema three migrates its terrain without inventing excavated passages", "[nest-network][snapshot]") {
  auto session = busy_session(7, 400);
  json old = json::parse(ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  old["schema_version"] = 3;
  old["run"]["world"].erase("passages");
  auto decoded = ant::persistence::decode_profile(old.dump());
  REQUIRE(decoded.profile);
  CHECK(decoded.profile->schema_version == 4);
  CHECK(decoded.profile->run->world.terrain == session.world().snapshot().terrain);
  CHECK(decoded.profile->run->world.passages.empty());
  auto migrated = ant::game::Session::restore(*decoded.profile->run);
  migrated.step_ticks(40);
  CHECK(migrated.world().invariant_holds());
  const auto saved = ant::persistence::decode_profile(ant::persistence::encode_profile(
      ant::persistence::make_new_profile(migrated)));
  REQUIRE(saved.profile);
  auto resumed = ant::game::Session::restore(*saved.profile->run);
  migrated.step_ticks(400);
  resumed.step_ticks(400);
  CHECK(migrated.world().canonical_hash() == resumed.world().canonical_hash());
}

TEST_CASE("invalid passage geometry is rejected before adopting a profile", "[nest-network][snapshot]") {
  auto session = busy_session(42, 400);
  json document = json::parse(ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  auto& passages = document["run"]["world"]["passages"];
  REQUIRE_FALSE(passages.empty());
  SECTION("disconnected route") { passages[0]["route"] = json::array({json::array({192, 58}), json::array({200, 58})}); }
  SECTION("unknown room") { passages[0]["room"] = 200; }
  SECTION("empty route") { passages[0]["route"] = json::array(); }
  SECTION("oversized route") { passages[0]["route"] = std::vector<std::array<int, 2>>(257, {192, 58}); }
  SECTION("duplicate room connection") { passages.push_back(passages[0]); }
  SECTION("oversized room list") {
    auto& rooms = document["run"]["world"]["rooms"];
    const auto room = rooms[0];
    while (rooms.size() <= ant::sim::kMaxRooms) rooms.push_back(room);
  }
  CHECK_FALSE(ant::persistence::decode_profile(document.dump()).profile);
}
