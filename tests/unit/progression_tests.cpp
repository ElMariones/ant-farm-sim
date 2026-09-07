#include "game/progression.hpp"
#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "game/view.hpp"
#include "persistence/content_loader.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace {

using ant::game::CommandRejection;
using ant::game::UpgradeId;

constexpr std::array<UpgradeId, 4> kUpgrades{UpgradeId::Excavation, UpgradeId::Nursing,
                                             UpgradeId::Foraging, UpgradeId::Queen};

// ECONOMY: cost(level) = ceil(15 * 8^level / 5^level), in checked integer arithmetic.
std::int64_t economy_cost(const int level) {
  std::int64_t numerator = 15;
  std::int64_t denominator = 1;
  for (int step = 0; step < level; ++step) {
    numerator *= 8;
    denominator *= 5;
  }
  return (numerator + denominator - 1) / denominator;
}

std::filesystem::path repository_config() {
  return std::filesystem::path(ANT_SOURCE_CONFIG_DIR) / "progression.json";
}

} // namespace

TEST_CASE("run adaptation costs and effects match ECONOMY", "[game][progression]") {
  const ant::game::ProgressionConfig config = ant::game::canonical_progression();
  std::string error;
  REQUIRE(ant::game::validate_progression(config, error));

  // 1 Work per 60 productive worker-seconds at 20 Hz, measured in T008 (see ECONOMY).
  CHECK(config.productive_ticks_per_work == 1'200);
  for (const ant::game::UpgradeDefinition& upgrade : config.upgrades) {
    for (int level = 0; level < 10; ++level) {
      CHECK(upgrade.costs[static_cast<std::size_t>(level)] == economy_cost(level));
    }
  }
  CHECK(config.upgrades[0].costs[0] == 15);
  CHECK(config.upgrades[0].costs[1] == 24);
  CHECK(config.upgrades[0].costs[2] == 39);
  CHECK(config.upgrades[0].costs[3] == 62);
  CHECK(config.upgrades[0].costs[4] == 99);

  CHECK(config.upgrades[ant::game::upgrade_index(UpgradeId::Excavation)].effect_percent_per_level == 25);
  CHECK(config.upgrades[ant::game::upgrade_index(UpgradeId::Nursing)].effect_percent_per_level == 10);
  CHECK(config.upgrades[ant::game::upgrade_index(UpgradeId::Foraging)].effect_percent_per_level == 20);
  CHECK(config.upgrades[ant::game::upgrade_index(UpgradeId::Queen)].effect_percent_per_level == 15);
}

TEST_CASE("shipped progression config matches the canonical balance", "[game][progression]") {
  const ant::game::ProgressionConfig loaded = ant::persistence::load_progression(repository_config());
  const ant::game::ProgressionConfig canonical = ant::game::canonical_progression();

  CHECK(loaded.content_version == canonical.content_version);
  CHECK(loaded.productive_ticks_per_work == canonical.productive_ticks_per_work);
  CHECK(loaded.focus_cooldown_ticks == canonical.focus_cooldown_ticks);
  for (std::size_t index = 0; index < loaded.upgrades.size(); ++index) {
    CHECK(loaded.upgrades[index].id == canonical.upgrades[index].id);
    CHECK(loaded.upgrades[index].name == canonical.upgrades[index].name);
    CHECK(loaded.upgrades[index].costs == canonical.upgrades[index].costs);
    CHECK(loaded.upgrades[index].effect_percent_per_level ==
          canonical.upgrades[index].effect_percent_per_level);
  }
}

TEST_CASE("invalid progression content is rejected", "[game][progression]") {
  std::string error;
  ant::game::ProgressionConfig config = ant::game::canonical_progression();

  config.upgrades[0].costs[3] = 1; // no longer monotonic
  CHECK_FALSE(ant::game::validate_progression(config, error));
  CHECK_FALSE(error.empty());

  config = ant::game::canonical_progression();
  config.productive_ticks_per_work = 0;
  CHECK_FALSE(ant::game::validate_progression(config, error));

  config = ant::game::canonical_progression();
  config.upgrades[2].id = UpgradeId::Excavation; // duplicate id
  CHECK_FALSE(ant::game::validate_progression(config, error));

  config = ant::game::canonical_progression();
  config.upgrades[1].effect_percent_per_level = 0;
  CHECK_FALSE(ant::game::validate_progression(config, error));
}

TEST_CASE("Work is earned only from productive worker ticks", "[game][progression]") {
  ant::game::Session session(42);
  CHECK(session.work() == 0);
  CHECK(session.world().stats().productive_worker_ticks == 0);

  bool observed_idle_tick = false;
  for (int tick = 0; tick < 3'000; ++tick) {
    session.step();
    const std::uint64_t productive = session.world().stats().productive_worker_ticks;
    // The accounting identity: Work is exactly the whole number of full blocks worked.
    const std::uint64_t block = session.progression().productive_ticks_per_work;
    REQUIRE(session.work() == static_cast<std::int64_t>(productive / block));
    REQUIRE(session.productive_tick_remainder() == productive % block);
    if (productive == 0) {
      observed_idle_tick = true;
      REQUIRE(session.work() == 0);
    }
  }
  // A colony that has not worked yet must have earned nothing, so worker count alone grants none.
  CHECK(observed_idle_tick);
  CHECK(session.work() > 0);
}

TEST_CASE("purchasing an adaptation spends Work exactly once", "[game][progression]") {
  ant::game::Session session(42);
  session.debug_grant_work(100);
  const std::int64_t before = session.work();
  const std::int64_t cost = session.next_upgrade_cost(UpgradeId::Excavation);
  REQUIRE(cost == 15);

  const ant::game::CommandResult result = session.buy_upgrade(UpgradeId::Excavation);
  CHECK(result.accepted);
  CHECK(result.rejection == CommandRejection::None);
  CHECK(result.spent == cost);
  CHECK(session.work() == before - cost);
  CHECK(session.upgrade_levels()[0] == 1);
  CHECK(session.world().adaptation_levels()[0] == 1);
  CHECK(session.next_upgrade_cost(UpgradeId::Excavation) == 24);
}

TEST_CASE("purchases are rejected without Work and at maximum level", "[game][progression]") {
  ant::game::Session session(42);

  const ant::game::CommandResult poor = session.buy_upgrade(UpgradeId::Nursing);
  CHECK_FALSE(poor.accepted);
  CHECK(poor.rejection == CommandRejection::InsufficientWork);
  CHECK(poor.spent == 0);
  CHECK(session.work() == 0);
  CHECK(session.upgrade_levels()[1] == 0);

  session.debug_grant_work(1'000'000);
  for (int level = 0; level < 10; ++level) {
    const ant::game::CommandResult bought = session.buy_upgrade(UpgradeId::Nursing);
    REQUIRE(bought.accepted);
    REQUIRE(bought.spent == economy_cost(level));
  }
  CHECK(session.upgrade_levels()[1] == 10);
  CHECK(session.next_upgrade_cost(UpgradeId::Nursing) == -1);

  const std::int64_t work_at_cap = session.work();
  const ant::game::CommandResult capped = session.buy_upgrade(UpgradeId::Nursing);
  CHECK_FALSE(capped.accepted);
  CHECK(capped.rejection == CommandRejection::MaxLevel);
  CHECK(capped.spent == 0);
  CHECK(session.work() == work_at_cap);
  CHECK(session.upgrade_levels()[1] == 10);
}

TEST_CASE("effective adaptation levels do not compound when the view is rebuilt",
          "[game][progression]") {
  ant::game::Session session(42);
  session.debug_grant_work(1'000);
  for (const UpgradeId id : kUpgrades) REQUIRE(session.buy_upgrade(id).accepted);
  session.step_ticks(100);

  const std::uint64_t hash_before = session.world().canonical_hash();
  const std::array<std::uint8_t, 4> expected{1, 1, 1, 1};
  for (int reopen = 0; reopen < 50; ++reopen) {
    const ant::game::GameView view = ant::game::make_view(session);
    REQUIRE(view.upgrade_levels == expected);
    REQUIRE(session.world().adaptation_levels() == expected);
  }
  // Reading the shop never advances or re-applies anything.
  CHECK(session.world().canonical_hash() == hash_before);
}

TEST_CASE("adaptations change simulated rates rather than task eligibility",
          "[game][progression]") {
  ant::game::Session baseline(7);
  ant::game::Session upgraded(7);
  upgraded.debug_grant_work(1'000'000);
  for (int level = 0; level < 10; ++level) {
    REQUIRE(upgraded.buy_upgrade(UpgradeId::Excavation).accepted);
  }

  // Buying a dig-rate adaptation diverges the two colonies' random draws, so a few minutes of
  // excavation is noise. Measure over a window long enough for the rate itself to dominate.
  baseline.step_ticks(12'000);
  upgraded.step_ticks(12'000);

  CHECK(upgraded.world().stats().cells_excavated > baseline.world().stats().cells_excavated);
  // The adaptation changes how fast digging goes, not who is allowed to dig.
  CHECK(baseline.world().stats().cells_excavated > 0);
  CHECK(baseline.world().invariant_holds());
  CHECK(upgraded.world().invariant_holds());
}

TEST_CASE("colony focus never removes an essential job", "[game][progression][tasks]") {
  for (const ant::sim::Focus focus :
       {ant::sim::Focus::Balanced, ant::sim::Focus::Growth, ant::sim::Focus::Expansion,
        ant::sim::Focus::Foraging}) {
    ant::game::Session session(42);
    session.step_ticks(8'000);
    REQUIRE(session.focus_available());
    REQUIRE(session.set_focus_command(focus).accepted);

    std::array<bool, 5> ever_assigned{};
    for (int tick = 0; tick < 4'000; ++tick) {
      session.step();
      const auto& counts = session.world().task_diagnostics().workers_by_task;
      for (std::size_t task = 0; task < counts.size(); ++task) {
        ever_assigned[task] = ever_assigned[task] || counts[task] > 0;
      }
    }
    // Forage, excavate and nurse all stay reachable under every focus.
    CHECK(ever_assigned[0]);
    CHECK(ever_assigned[1]);
    CHECK(ever_assigned[2]);
    CHECK(session.world().focus() == focus);
  }
}

TEST_CASE("focus is locked before twelve workers and rate limited afterwards",
          "[game][progression]") {
  ant::game::Session session(42);
  const ant::game::CommandResult locked = session.set_focus_command(ant::sim::Focus::Growth);
  CHECK_FALSE(locked.accepted);
  CHECK(locked.rejection == CommandRejection::FocusLocked);

  session.step_ticks(8'000);
  REQUIRE(session.focus_available());
  CHECK(session.focus_cooldown_remaining() == 0);

  REQUIRE(session.set_focus_command(ant::sim::Focus::Growth).accepted);
  CHECK(session.focus_cooldown_remaining() == session.progression().focus_cooldown_ticks);

  const ant::game::CommandResult too_soon = session.set_focus_command(ant::sim::Focus::Foraging);
  CHECK_FALSE(too_soon.accepted);
  CHECK(too_soon.rejection == CommandRejection::FocusCooldown);
  CHECK(session.world().focus() == ant::sim::Focus::Growth);

  session.step_ticks(session.progression().focus_cooldown_ticks);
  CHECK(session.focus_cooldown_remaining() == 0);
  CHECK(session.set_focus_command(ant::sim::Focus::Foraging).accepted);
  CHECK(session.world().focus() == ant::sim::Focus::Foraging);
}

TEST_CASE("granting Work without labour marks the run assisted", "[game][progression]") {
  ant::game::Session session(42);
  CHECK_FALSE(session.assisted());
  session.step_ticks(500);
  CHECK_FALSE(session.assisted());
  session.debug_grant_work(10);
  CHECK(session.assisted());
  CHECK(session.snapshot().assisted);
}

TEST_CASE("the view reports a bottleneck reason", "[game][progression]") {
  ant::game::Session session(42);
  session.step_ticks(2'000);
  const ant::game::GameView view = ant::game::make_view(session);
  CHECK(std::string(ant::game::bottleneck_name(view.bottleneck)).size() > 0);
  CHECK(view.work == session.work());
  CHECK(view.upgrade_costs[0] == session.next_upgrade_cost(UpgradeId::Excavation));
}
