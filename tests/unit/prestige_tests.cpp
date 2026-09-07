#include "game/prestige.hpp"
#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "persistence/profile_codec.hpp"
#include "persistence/save_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

using ant::game::FlightBlock;
using ant::game::ProfilePhase;
using ant::game::ProfileSnapshot;
using ant::game::Session;
using ant::game::TraitBranch;

constexpr ant::sim::Tick kSecond = ant::sim::kTicksPerSecond;

// A colony that satisfies every flight condition, reached cheaply: the maturity latch and the
// caste rules are covered in their own tests.
Session flight_ready_session(const int winged_queens = 3, const std::uint64_t births = 150) {
  Session session(42);
  session.debug_world().run_ticks(200);
  session.debug_world().debug_set_workers_born(births);
  session.debug_world().debug_set_mature();
  for (int i = 0; i < winged_queens; ++i) {
    session.debug_world().debug_spawn_brood(ant::sim::BroodStage::Pupa, 1'790, 0,
                                            ant::sim::BroodRole::Gyne);
  }
  session.step_ticks(3 * kSecond);
  return session;
}

ProfileSnapshot active_profile(const Session& session, const std::uint64_t revision = 1) {
  ProfileSnapshot profile = ant::persistence::make_new_profile(session, revision);
  return profile;
}

ProfileSnapshot between_runs_profile(const std::int64_t wallet, const std::uint64_t revision = 1) {
  ProfileSnapshot profile;
  profile.revision = revision;
  profile.profile_id = "profile-test";
  profile.phase = ProfilePhase::BetweenRuns;
  profile.meta.legacy_wallet = wallet;
  profile.meta.legacy_earned_total = wallet;
  return profile;
}

class ScopedSaveDirectory {
public:
  ScopedSaveDirectory() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("ant-prestige-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~ScopedSaveDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  ScopedSaveDirectory(const ScopedSaveDirectory&) = delete;
  ScopedSaveDirectory& operator=(const ScopedSaveDirectory&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

TEST_CASE("integer square root floors exactly across its domain", "[game][prestige]") {
  CHECK(ant::game::integer_sqrt(0) == 0);
  CHECK(ant::game::integer_sqrt(1) == 1);
  CHECK(ant::game::integer_sqrt(3) == 1);
  CHECK(ant::game::integer_sqrt(4) == 2);
  CHECK(ant::game::integer_sqrt(8) == 2);
  CHECK(ant::game::integer_sqrt(9) == 3);
  for (std::uint64_t root = 0; root < 3'000; ++root) {
    const std::uint64_t square = root * root;
    CHECK(ant::game::integer_sqrt(square) == root);
    CHECK(ant::game::integer_sqrt(square + root) == root);
    if (root > 0) CHECK(ant::game::integer_sqrt(square - 1) == root - 1);
  }
  // The full validated domain, where a double would already have lost precision.
  const std::uint64_t big = std::uint64_t{1} << 62;
  CHECK(ant::game::integer_sqrt(big) == (std::uint64_t{1} << 31));
  CHECK(ant::game::integer_sqrt(std::numeric_limits<std::uint64_t>::max()) == 4'294'967'295ULL);
}

TEST_CASE("flight payout matches the documented examples", "[game][prestige]") {
  // ECONOMY: eligibility 4, 600 births with 6 queens 6, 1350 births with 10 queens 8.
  CHECK(ant::game::flight_payout(150, 3).total == 4);
  CHECK(ant::game::flight_payout(600, 6).total == 6);
  CHECK(ant::game::flight_payout(1'350, 10).total == 8);

  // Square-root steps at 150, 600, 1350, 2400; queen steps at 3, 6 and 9.
  CHECK(ant::game::flight_payout(149, 3).birth_bonus == 0);
  CHECK(ant::game::flight_payout(150, 3).birth_bonus == 1);
  CHECK(ant::game::flight_payout(599, 3).birth_bonus == 1);
  CHECK(ant::game::flight_payout(600, 3).birth_bonus == 2);
  CHECK(ant::game::flight_payout(1'350, 3).birth_bonus == 3);
  CHECK(ant::game::flight_payout(2'400, 3).birth_bonus == 4);
  CHECK(ant::game::flight_payout(150, 2).queen_bonus == 0);
  CHECK(ant::game::flight_payout(150, 5).queen_bonus == 1);
  CHECK(ant::game::flight_payout(150, 6).queen_bonus == 2);
  CHECK(ant::game::flight_payout(150, 9).queen_bonus == 3);
  // More than ten live queens never counts for more than ten.
  CHECK(ant::game::flight_payout(150, 40).queen_bonus == 3);
}

TEST_CASE("flight eligibility names the actual blocking condition", "[game][prestige]") {
  SECTION("an immature colony cannot fly") {
    Session session(42);
    session.step_ticks(400);
    const auto preview = ant::game::preview_flight(session);
    CHECK_FALSE(preview.eligible);
    CHECK(preview.block == FlightBlock::NotMature);
  }
  SECTION("two winged queens are not enough") {
    Session session = flight_ready_session(2);
    const auto preview = ant::game::preview_flight(session);
    CHECK_FALSE(preview.eligible);
    CHECK(preview.block == FlightBlock::TooFewWingedQueens);
  }
  SECTION("a dead queen revokes flight") {
    Session session = flight_ready_session();
    REQUIRE(ant::game::preview_flight(session).eligible);
    session.debug_world().debug_kill_queen();
    const auto preview = ant::game::preview_flight(session);
    CHECK_FALSE(preview.eligible);
    CHECK(preview.block == FlightBlock::QueenDead);
  }
  SECTION("an assisted run cannot fly") {
    Session session = flight_ready_session();
    REQUIRE(ant::game::preview_flight(session).eligible);
    session.debug_grant_work(1);
    const auto preview = ant::game::preview_flight(session);
    CHECK_FALSE(preview.eligible);
    CHECK(preview.block == FlightBlock::AssistedRun);
  }
  SECTION("an eligible colony previews its payout and next thresholds") {
    Session session = flight_ready_session();
    const auto preview = ant::game::preview_flight(session);
    CHECK(preview.eligible);
    CHECK(preview.payout.total == 4);
    CHECK(preview.live_winged_queens == 3);
    CHECK(preview.next_birth_threshold == 600);
    CHECK(preview.next_queen_threshold == 6);
  }
}

TEST_CASE("a flight credits Legacy exactly once", "[game][prestige]") {
  Session session = flight_ready_session();
  const ProfileSnapshot current = active_profile(session);
  const std::string run_id = current.run->run_id;

  const auto flight = ant::game::prepare_flight(current, session, run_id, current.revision);
  REQUIRE(flight.accepted);
  const ProfileSnapshot& candidate = *flight.candidate;
  CHECK(candidate.meta.legacy_wallet == 4);
  CHECK(candidate.meta.legacy_earned_total == 4);
  CHECK(candidate.meta.legacy_spent_total == 0);
  CHECK(candidate.meta.successful_flights == 1);
  CHECK(candidate.phase == ProfilePhase::BetweenRuns);
  CHECK_FALSE(candidate.run.has_value());
  REQUIRE(candidate.last_flight_receipt.has_value());
  CHECK(candidate.last_flight_receipt->run_id == run_id);
  CHECK(candidate.last_flight_receipt->earned_legacy == 4);
  CHECK(candidate.last_flight_receipt->winged_queens == 3);

  // Replaying the same command against the committed profile cannot pay again.
  ProfileSnapshot committed = candidate;
  committed.revision = current.revision + 1;
  const auto replay = ant::game::prepare_flight(committed, session, run_id, committed.revision);
  CHECK_FALSE(replay.accepted);
  CHECK_FALSE(replay.error.empty());
}

TEST_CASE("a stale or mismatched flight command is refused", "[game][prestige]") {
  Session session = flight_ready_session();
  const ProfileSnapshot current = active_profile(session);

  SECTION("a revision the caller has not seen") {
    const auto result =
        ant::game::prepare_flight(current, session, current.run->run_id, current.revision + 1);
    CHECK_FALSE(result.accepted);
    CHECK(result.error.find("revision") != std::string::npos);
  }
  SECTION("a different run") {
    const auto result =
        ant::game::prepare_flight(current, session, "run-999-1", current.revision);
    CHECK_FALSE(result.accepted);
    CHECK(result.error.find("different run") != std::string::npos);
  }
  SECTION("a profile that already flew") {
    ProfileSnapshot between = between_runs_profile(4, current.revision);
    const auto result =
        ant::game::prepare_flight(between, session, current.run->run_id, between.revision);
    CHECK_FALSE(result.accepted);
  }
}

TEST_CASE("a failed commit leaves the live colony and profile untouched",
          "[game][prestige][persistence]") {
  const ScopedSaveDirectory directory;
  Session session = flight_ready_session();
  ant::persistence::SaveService service(directory.path());

  const auto first = service.commit(ant::persistence::make_new_profile(session));
  REQUIRE(first.committed);
  const ProfileSnapshot current = *first.profile;
  const std::uint64_t hash_before = session.world().canonical_hash();

  // Corrupt the current file so the next commit must refuse.
  {
    std::ofstream output(directory.path() / "profile.json", std::ios::trunc);
    output << "{ broken";
  }
  const auto flight =
      ant::game::prepare_flight(current, session, current.run->run_id, current.revision);
  REQUIRE(flight.accepted);
  const auto save = service.commit(*flight.candidate);
  CHECK_FALSE(save.committed);

  // The candidate was never accepted, so the colony is still running and still owns its run.
  CHECK(session.world().canonical_hash() == hash_before);
  CHECK(current.phase == ProfilePhase::ActiveRun);
  CHECK(current.meta.legacy_wallet == 0);
}

TEST_CASE("the whole flight, shop and new colony flow commits one step at a time",
          "[game][prestige][persistence]") {
  const ScopedSaveDirectory directory;
  Session session = flight_ready_session();
  ant::persistence::SaveService service(directory.path());

  ProfileSnapshot current = *service.commit(ant::persistence::make_new_profile(session)).profile;
  REQUIRE(current.phase == ProfilePhase::ActiveRun);

  // 1. Fly.
  const auto flight =
      ant::game::prepare_flight(current, session, current.run->run_id, current.revision);
  REQUIRE(flight.accepted);
  const auto flown = service.commit(*flight.candidate);
  REQUIRE(flown.committed);
  current = *flown.profile;
  CHECK(current.phase == ProfilePhase::BetweenRuns);
  CHECK(current.meta.legacy_wallet == 4);

  // 2. Buy a tier-1 trait; four Legacy affords one tier 1 with one left over.
  const auto purchase =
      ant::game::prepare_trait_purchase(current, TraitBranch::Vigor, current.revision);
  REQUIRE(purchase.accepted);
  const auto bought = service.commit(*purchase.candidate);
  REQUIRE(bought.committed);
  current = *bought.profile;
  CHECK(current.meta.vigor_tier == 1);
  CHECK(current.meta.legacy_wallet == 1);
  CHECK(current.meta.legacy_spent_total == 3);
  CHECK(current.meta.legacy_earned_total == 4);

  // It cannot afford the other branch's tier 1 as well.
  const auto unaffordable =
      ant::game::prepare_trait_purchase(current, TraitBranch::Industry, current.revision);
  CHECK_FALSE(unaffordable.accepted);

  // 3. Found the next colony, which persists before play resumes.
  const auto next = ant::game::prepare_new_run(current, 77, ant::game::canonical_progression(),
                                               current.revision);
  REQUIRE(next.accepted);
  const auto started = service.commit(*next.candidate);
  REQUIRE(started.committed);
  current = *started.profile;
  CHECK(current.phase == ProfilePhase::ActiveRun);
  CHECK(current.meta.generation == 2);
  CHECK(current.run->run_id == "run-77-2");
  CHECK(current.meta.successful_flights == 1);

  // The new colony was founded with the owned trait applied exactly once.
  const Session second = Session::restore(*current.run);
  CHECK(second.world().traits() == ant::sim::TraitModifiers{1, 0});
  CHECK(second.world().living_workers() == 6);
  CHECK(second.work() == 0);
  CHECK(second.upgrade_levels() == std::array<std::uint8_t, 4>{});
  CHECK(second.world().tick() == 0);

  // Reloading from disk gives the same second colony.
  const auto reloaded = service.load();
  REQUIRE(reloaded.state == ant::persistence::LoadState::Loaded);
  CHECK(reloaded.profile->meta.vigor_tier == 1);
  CHECK(ant::game::Session::restore(*reloaded.profile->run).world().canonical_hash() ==
        second.world().canonical_hash());
}

TEST_CASE("a repeated trait purchase cannot overspend the wallet", "[game][prestige]") {
  const ProfileSnapshot current = between_runs_profile(3);

  const auto first = ant::game::prepare_trait_purchase(current, TraitBranch::Vigor, current.revision);
  REQUIRE(first.accepted);
  CHECK(first.candidate->meta.legacy_wallet == 0);

  // A second click built from the same view is stale once the first has committed.
  ProfileSnapshot committed = *first.candidate;
  committed.revision = current.revision + 1;
  const auto stale =
      ant::game::prepare_trait_purchase(committed, TraitBranch::Vigor, current.revision);
  CHECK_FALSE(stale.accepted);
  CHECK(stale.error.find("revision") != std::string::npos);

  // Even with a fresh view there is nothing left to spend.
  const auto broke =
      ant::game::prepare_trait_purchase(committed, TraitBranch::Vigor, committed.revision);
  CHECK_FALSE(broke.accepted);
  CHECK(committed.meta.legacy_wallet == 0);
}

TEST_CASE("trait branches follow their cost table and linear prerequisites", "[game][prestige]") {
  ProfileSnapshot profile = between_runs_profile(57);
  const std::array<std::int64_t, 4> expected{3, 7, 15, 32};
  std::int64_t spent = 0;

  for (std::uint8_t tier = 0; tier < 4; ++tier) {
    CHECK(ant::game::next_trait_cost(profile.meta, TraitBranch::Industry) == expected[tier]);
    const auto purchase =
        ant::game::prepare_trait_purchase(profile, TraitBranch::Industry, profile.revision);
    REQUIRE(purchase.accepted);
    profile = *purchase.candidate;
    profile.revision += 1;
    spent += expected[tier];
    CHECK(profile.meta.industry_tier == tier + 1);
    CHECK(profile.meta.legacy_spent_total == spent);
  }
  // 57 Legacy completes exactly one branch.
  CHECK(spent == 57);
  CHECK(profile.meta.legacy_wallet == 0);
  CHECK(ant::game::next_trait_cost(profile.meta, TraitBranch::Industry) == -1);

  const auto beyond =
      ant::game::prepare_trait_purchase(profile, TraitBranch::Industry, profile.revision);
  CHECK_FALSE(beyond.accepted);
  CHECK(beyond.error.find("already complete") != std::string::npos);
}

TEST_CASE("traits cannot be bought while a run is active", "[game][prestige]") {
  Session session = flight_ready_session();
  ProfileSnapshot current = active_profile(session);
  current.meta.legacy_wallet = 100;
  current.meta.legacy_earned_total = 100;

  const auto purchase =
      ant::game::prepare_trait_purchase(current, TraitBranch::Vigor, current.revision);
  CHECK_FALSE(purchase.accepted);
  CHECK(purchase.error.find("between runs") != std::string::npos);
}

TEST_CASE("Industry IV lowers the Work threshold exactly once", "[game][prestige][traits]") {
  const Session plain(42, ant::game::canonical_progression(), {0, 3});
  const Session industrious(42, ant::game::canonical_progression(), {0, 4});
  CHECK(plain.effective_ticks_per_work() == 1'200);
  CHECK(industrious.effective_ticks_per_work() == 960);

  Session earning(42, ant::game::canonical_progression(), {0, 4});
  earning.step_ticks(3'000);
  const std::uint64_t productive = earning.world().stats().productive_worker_ticks;
  CHECK(earning.work() == static_cast<std::int64_t>(productive / 960));
  CHECK(earning.productive_tick_remainder() == productive % 960);
}

TEST_CASE("abandoning a run ends it without any reward", "[game][prestige]") {
  Session session = flight_ready_session();
  const ProfileSnapshot current = active_profile(session);

  const auto abandon = ant::game::prepare_abandon_run(current, current.revision);
  REQUIRE(abandon.accepted);
  CHECK(abandon.candidate->phase == ProfilePhase::BetweenRuns);
  CHECK_FALSE(abandon.candidate->run.has_value());
  CHECK(abandon.candidate->meta.legacy_wallet == 0);
  CHECK(abandon.candidate->meta.legacy_earned_total == 0);
  CHECK(abandon.candidate->meta.successful_flights == 0);
  CHECK_FALSE(abandon.candidate->last_flight_receipt.has_value());

  // And there is nothing left to abandon.
  ProfileSnapshot committed = *abandon.candidate;
  committed.revision += 1;
  CHECK_FALSE(ant::game::prepare_abandon_run(committed, committed.revision).accepted);
}

TEST_CASE("a crash before or after replacement leaves one valid phase and payout",
          "[game][prestige][persistence]") {
  SECTION("interrupted before the replacement, the run is still eligible") {
    const ScopedSaveDirectory directory;
    Session session = flight_ready_session();
    ant::persistence::SaveService service(directory.path());
    const ProfileSnapshot current =
        *service.commit(ant::persistence::make_new_profile(session)).profile;

    // The candidate was built but never committed.
    const auto flight =
        ant::game::prepare_flight(current, session, current.run->run_id, current.revision);
    REQUIRE(flight.accepted);

    const auto reloaded = service.load();
    REQUIRE(reloaded.state == ant::persistence::LoadState::Loaded);
    CHECK(reloaded.profile->phase == ProfilePhase::ActiveRun);
    CHECK(reloaded.profile->meta.legacy_wallet == 0);
    CHECK_FALSE(reloaded.profile->last_flight_receipt.has_value());
    // The reloaded run can still fly, and pays exactly once.
    const Session resumed = Session::restore(*reloaded.profile->run);
    CHECK(ant::game::preview_flight(resumed).payout.total == 4);
  }

  SECTION("after the replacement, the reward is credited once and cannot repeat") {
    const ScopedSaveDirectory directory;
    Session session = flight_ready_session();
    ant::persistence::SaveService service(directory.path());
    const ProfileSnapshot current =
        *service.commit(ant::persistence::make_new_profile(session)).profile;
    const std::string run_id = current.run->run_id;

    const auto flight =
        ant::game::prepare_flight(current, session, run_id, current.revision);
    REQUIRE(service.commit(*flight.candidate).committed);

    const auto reloaded = service.load();
    REQUIRE(reloaded.state == ant::persistence::LoadState::Loaded);
    CHECK(reloaded.profile->phase == ProfilePhase::BetweenRuns);
    CHECK(reloaded.profile->meta.legacy_wallet == 4);
    CHECK(reloaded.profile->meta.successful_flights == 1);
    REQUIRE(reloaded.profile->last_flight_receipt.has_value());
    CHECK(reloaded.profile->last_flight_receipt->run_id == run_id);
    // Loading a between-runs profile runs no payout logic of its own.
    CHECK_FALSE(ant::game::prepare_flight(*reloaded.profile, session, run_id,
                                          reloaded.profile->revision)
                    .accepted);
  }
}

TEST_CASE("loading a mature save does not fly on its own", "[game][prestige][persistence]") {
  Session session = flight_ready_session();
  REQUIRE(ant::game::preview_flight(session).eligible);

  const auto decoded = ant::persistence::decode_profile(
      ant::persistence::encode_profile(ant::persistence::make_new_profile(session)));
  REQUIRE(decoded.profile.has_value());
  CHECK(decoded.profile->phase == ProfilePhase::ActiveRun);
  CHECK(decoded.profile->meta.legacy_wallet == 0);
  CHECK(decoded.profile->meta.successful_flights == 0);
  CHECK_FALSE(decoded.profile->last_flight_receipt.has_value());

  const Session resumed = Session::restore(*decoded.profile->run);
  CHECK(resumed.world().mature());
  CHECK(ant::game::preview_flight(resumed).eligible);
}
