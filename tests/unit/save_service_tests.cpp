#include "game/session.hpp"
#include "game/snapshot.hpp"
#include "persistence/profile_codec.hpp"
#include "persistence/save_service.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>

namespace {

using ant::persistence::FileOps;
using ant::persistence::LoadState;
using ant::persistence::SaveService;

// Every test owns a private directory so no case can observe another's profile or lock.
class ScopedSaveDirectory {
public:
  ScopedSaveDirectory() {
    static std::atomic<unsigned> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("ant-save-test-" + std::to_string(::getpid()) + "-" +
             std::to_string(counter.fetch_add(1)));
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
  [[nodiscard]] std::filesystem::path current() const { return path_ / "profile.json"; }
  [[nodiscard]] std::filesystem::path backup() const { return path_ / "profile.backup.json"; }

private:
  std::filesystem::path path_;
};

// Delegates to the real filesystem so post-failure disk state is genuine, and fails on demand at
// a chosen stage of the save protocol.
class FaultInjectingFileOps final : public FileOps {
public:
  explicit FaultInjectingFileOps(std::shared_ptr<FileOps> inner) : inner_(std::move(inner)) {}

  std::string fail_write_containing;
  std::string fail_replace_to_containing;
  std::string tear_after_replace_to_containing;
  bool fail_sync{false};

  void create_directories(const std::filesystem::path& path) override {
    inner_->create_directories(path);
  }
  bool exists(const std::filesystem::path& path) const override { return inner_->exists(path); }
  std::string read(const std::filesystem::path& path) const override { return inner_->read(path); }
  void write_and_flush(const std::filesystem::path& path,
                       const std::string_view contents) override {
    if (matches(path, fail_write_containing)) throw std::runtime_error("injected write failure");
    inner_->write_and_flush(path, contents);
  }
  void replace(const std::filesystem::path& from, const std::filesystem::path& to) override {
    if (matches(to, fail_replace_to_containing)) throw std::runtime_error("injected replace failure");
    inner_->replace(from, to);
    // The rename committed but the process could not confirm it: the exact uncertain outcome the
    // save protocol has to resolve by re-reading the authoritative file.
    if (matches(to, tear_after_replace_to_containing)) {
      throw std::runtime_error("injected failure after replacement");
    }
  }
  void sync_directory(const std::filesystem::path& path) override {
    if (fail_sync) throw std::runtime_error("injected directory sync failure");
    inner_->sync_directory(path);
  }
  void remove_if_exists(const std::filesystem::path& path) noexcept override {
    inner_->remove_if_exists(path);
  }

private:
  static bool matches(const std::filesystem::path& path, const std::string& needle) {
    return !needle.empty() && path.filename().string().find(needle) != std::string::npos;
  }
  std::shared_ptr<FileOps> inner_;
};

ant::game::ProfileSnapshot profile_for(const std::uint64_t seed, const ant::sim::Tick ticks) {
  ant::game::Session session(seed);
  session.step_ticks(ticks);
  return ant::persistence::make_new_profile(session);
}

void write_raw(const std::filesystem::path& path, const std::string_view contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
}

std::string read_raw(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("an empty save directory reports a new profile", "[persistence][save]") {
  const ScopedSaveDirectory directory;
  const SaveService service(directory.path());
  const auto result = service.load();
  CHECK(result.state == LoadState::NewProfile);
  CHECK_FALSE(result.profile.has_value());
}

TEST_CASE("a committed profile reloads with the colony intact", "[persistence][save]") {
  const ScopedSaveDirectory directory;
  const ant::game::ProfileSnapshot candidate = profile_for(42, 3'000);
  const std::uint64_t saved_hash =
      ant::game::Session::restore(*candidate.run).world().canonical_hash();

  {
    SaveService service(directory.path());
    const auto saved = service.commit(candidate);
    REQUIRE(saved.committed);
    CHECK_FALSE(saved.durability_uncertain);
    CHECK(saved.error.empty());
    CHECK(saved.profile->revision == 1);
  }

  // A second process-lifetime opens the same directory, as a relaunch would.
  const SaveService reopened(directory.path());
  const auto loaded = reopened.load();
  REQUIRE(loaded.state == LoadState::Loaded);
  REQUIRE(loaded.profile->run.has_value());
  CHECK(loaded.profile->revision == 1);
  CHECK(loaded.profile->settings.ui_scale_percent == candidate.settings.ui_scale_percent);
  CHECK(loaded.profile->settings.preferred_speed == candidate.settings.preferred_speed);

  const ant::game::Session resumed = ant::game::Session::restore(*loaded.profile->run);
  CHECK(resumed.world().canonical_hash() == saved_hash);
  CHECK(resumed.world().tick() == candidate.run->world.tick);
  CHECK(resumed.world().invariant_holds());
}

TEST_CASE("each commit advances the revision and preserves the previous one as backup",
          "[persistence][save]") {
  const ScopedSaveDirectory directory;
  SaveService service(directory.path());

  REQUIRE(service.commit(profile_for(42, 400)).committed);
  const auto second = service.commit(profile_for(42, 800));
  REQUIRE(second.committed);
  CHECK(second.profile->revision == 2);

  REQUIRE(std::filesystem::exists(directory.backup()));
  const auto backup = ant::persistence::decode_profile(read_raw(directory.backup()));
  REQUIRE(backup.profile.has_value());
  CHECK(backup.profile->revision == 1);
  const auto current = ant::persistence::decode_profile(read_raw(directory.current()));
  REQUIRE(current.profile.has_value());
  CHECK(current.profile->revision == 2);
}

TEST_CASE("a second writer for the same profile is refused", "[persistence][save]") {
  const ScopedSaveDirectory directory;
  const SaveService first(directory.path());
  CHECK_THROWS(SaveService(directory.path()));

  // Different profile locations remain independently writable.
  const ScopedSaveDirectory other;
  CHECK_NOTHROW(SaveService(other.path()));
}

TEST_CASE("a failure at any save stage leaves a recoverable profile on disk",
          "[persistence][save][faults]") {
  const ant::game::ProfileSnapshot first = profile_for(42, 400);
  const ant::game::ProfileSnapshot second = profile_for(42, 900);

  SECTION("the candidate temporary cannot be written") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(first).committed);
    const std::string committed = read_raw(directory.current());

    faults->fail_write_containing = "profile.";
    const auto failed = service.commit(second);
    CHECK_FALSE(failed.committed);
    CHECK_FALSE(failed.error.empty());
    // The previously committed revision is byte-for-byte untouched.
    CHECK(read_raw(directory.current()) == committed);
    const auto reloaded = ant::persistence::decode_profile(read_raw(directory.current()));
    REQUIRE(reloaded.profile.has_value());
    CHECK(reloaded.profile->revision == 1);
  }

  SECTION("the backup copy cannot be written") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(first).committed);
    const std::string committed = read_raw(directory.current());

    faults->fail_write_containing = "backup.";
    const auto failed = service.commit(second);
    CHECK_FALSE(failed.committed);
    // Backup creation failing must keep the current profile rather than replace it.
    CHECK(read_raw(directory.current()) == committed);
  }

  SECTION("the backup replacement fails") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(first).committed);
    const std::string committed = read_raw(directory.current());

    faults->fail_replace_to_containing = "profile.backup.json";
    const auto failed = service.commit(second);
    CHECK_FALSE(failed.committed);
    CHECK(read_raw(directory.current()) == committed);
  }

  SECTION("the final replacement fails") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(first).committed);

    faults->fail_replace_to_containing = "profile.json";
    const auto failed = service.commit(second);
    CHECK_FALSE(failed.committed);
    const auto reloaded = ant::persistence::decode_profile(read_raw(directory.current()));
    REQUIRE(reloaded.profile.has_value());
    CHECK(reloaded.profile->revision == 1);
  }

  SECTION("no temporary files are left behind after a failure") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(first).committed);

    faults->fail_replace_to_containing = "profile.json";
    CHECK_FALSE(service.commit(second).committed);
    for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
      CHECK(entry.path().extension() != ".tmp");
    }
  }
}

TEST_CASE("an uncertain outcome after replacement is resolved by re-reading the file",
          "[persistence][save][faults]") {
  SECTION("the replacement itself could not be confirmed") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    REQUIRE(service.commit(profile_for(42, 400)).committed);

    faults->tear_after_replace_to_containing = "profile.json";
    const auto uncertain = service.commit(profile_for(42, 900));
    CHECK(uncertain.committed);
    CHECK(uncertain.durability_uncertain);
    CHECK_FALSE(uncertain.error.empty());
    REQUIRE(uncertain.profile.has_value());
    CHECK(uncertain.profile->revision == 2);
    // The reported revision is the one actually on disk, not the one that was hoped for.
    const auto on_disk = ant::persistence::decode_profile(read_raw(directory.current()));
    REQUIRE(on_disk.profile.has_value());
    CHECK(on_disk.profile->revision == uncertain.profile->revision);
  }

  SECTION("only the directory sync failed") {
    const ScopedSaveDirectory directory;
    auto faults = std::make_shared<FaultInjectingFileOps>(ant::persistence::standard_file_ops());
    SaveService service(directory.path(), faults);
    faults->fail_sync = true;

    const auto uncertain = service.commit(profile_for(7, 400));
    CHECK(uncertain.committed);
    CHECK(uncertain.durability_uncertain);
    REQUIRE(uncertain.profile.has_value());
    CHECK(uncertain.profile->revision == 1);
    const auto on_disk = ant::persistence::decode_profile(read_raw(directory.current()));
    REQUIRE(on_disk.profile.has_value());
    CHECK(on_disk.profile->revision == 1);
  }
}

TEST_CASE("a corrupt current profile is never overwritten automatically",
          "[persistence][save][recovery]") {
  const ScopedSaveDirectory directory;
  SaveService service(directory.path());
  REQUIRE(service.commit(profile_for(42, 400)).committed);
  REQUIRE(service.commit(profile_for(42, 900)).committed);
  const std::string good_backup = read_raw(directory.backup());

  write_raw(directory.current(), "{\"schema_version\": 1, \"truncated\":");
  const std::string corrupt = read_raw(directory.current());

  const auto loaded = service.load();
  REQUIRE(loaded.state == LoadState::RecoveryAvailable);
  CHECK_FALSE(loaded.profile.has_value());
  REQUIRE(loaded.backup.has_value());
  CHECK(loaded.backup->revision == 1);
  CHECK_FALSE(loaded.error.empty());

  // An ordinary save (an autosave, say) must refuse rather than destroy the corrupt original.
  const auto refused = service.commit(profile_for(42, 1'200));
  CHECK_FALSE(refused.committed);
  CHECK(refused.error.find("refusing to overwrite") != std::string::npos);
  CHECK(read_raw(directory.current()) == corrupt);
  CHECK(read_raw(directory.backup()) == good_backup);
}

TEST_CASE("explicit recovery restores the backup and retains the corrupt file for diagnosis",
          "[persistence][save][recovery]") {
  const ScopedSaveDirectory directory;
  SaveService service(directory.path());
  REQUIRE(service.commit(profile_for(42, 400)).committed);
  REQUIRE(service.commit(profile_for(42, 900)).committed);

  write_raw(directory.current(), "not a profile at all");
  const auto loaded = service.load();
  REQUIRE(loaded.state == LoadState::RecoveryAvailable);
  REQUIRE(loaded.backup.has_value());

  const auto recovered = service.recover_backup(*loaded.backup);
  REQUIRE(recovered.committed);
  const auto now = ant::persistence::decode_profile(read_raw(directory.current()));
  REQUIRE(now.profile.has_value());
  CHECK(now.profile->run.has_value());

  bool retained = false;
  for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
    retained = retained || entry.path().filename().string().find("profile.corrupt.") == 0;
  }
  CHECK(retained);

  const auto reloaded = service.load();
  CHECK(reloaded.state == LoadState::Loaded);
}

TEST_CASE("both files being unreadable is reported rather than repaired",
          "[persistence][save][recovery]") {
  const ScopedSaveDirectory directory;
  SaveService service(directory.path());
  REQUIRE(service.commit(profile_for(42, 400)).committed);
  REQUIRE(service.commit(profile_for(42, 900)).committed);

  write_raw(directory.current(), "broken");
  write_raw(directory.backup(), "also broken");

  const auto loaded = service.load();
  CHECK(loaded.state == LoadState::Invalid);
  CHECK_FALSE(loaded.profile.has_value());
  CHECK_FALSE(loaded.backup.has_value());
  CHECK_FALSE(loaded.error.empty());
}

TEST_CASE("loading never advances the simulation", "[persistence][save]") {
  const ScopedSaveDirectory directory;
  const ant::game::ProfileSnapshot candidate = profile_for(101, 2'000);
  const ant::sim::Tick saved_tick = candidate.run->world.tick;

  SaveService service(directory.path());
  REQUIRE(service.commit(candidate).committed);
  const auto loaded = service.load();
  REQUIRE(loaded.state == LoadState::Loaded);

  const ant::game::Session resumed = ant::game::Session::restore(*loaded.profile->run);
  CHECK(resumed.world().tick() == saved_tick);
}
