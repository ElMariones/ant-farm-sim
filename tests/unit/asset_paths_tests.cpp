#include "app/asset_paths.hpp"

#include <catch2/catch_test_macros.hpp>
#include <set>

namespace {

const std::filesystem::path kExe{"/Applications/Ant Farm.app/Contents/MacOS"};
const std::filesystem::path kCwd{"/Users/someone/work"};
const std::filesystem::path kSource{"/Users/someone/ant-farm-sim"};

ant::app::PathPredicate only(std::set<std::filesystem::path> present) {
  return [present = std::move(present)](const std::filesystem::path& path) {
    return present.count(path) > 0;
  };
}

} // namespace

TEST_CASE("a bundled file is found next to the executable first", "[app][assets]") {
  // A packaged app is launched with an arbitrary working directory, so this is the only anchor
  // that can be relied on.
  const auto resolved = ant::app::resolve_bundled_file(
      "config/progression.json", kExe, kCwd, kSource,
      only({kExe / "config/progression.json", kCwd / "config/progression.json",
            kSource / "config/progression.json"}));
  CHECK(resolved == kExe / "config/progression.json");
}

TEST_CASE("the working directory is used when the executable has no copy", "[app][assets]") {
  const auto resolved =
      ant::app::resolve_bundled_file("config/progression.json", kExe, kCwd, kSource,
                                     only({kCwd / "config/progression.json",
                                           kSource / "config/progression.json"}));
  CHECK(resolved == kCwd / "config/progression.json");
}

TEST_CASE("the source tree is the last resort", "[app][assets]") {
  const auto resolved = ant::app::resolve_bundled_file(
      "config/progression.json", kExe, kCwd, kSource, only({kSource / "config/progression.json"}));
  CHECK(resolved == kSource / "config/progression.json");
}

TEST_CASE("a missing file reports the packaged location", "[app][assets]") {
  // The error a player sees should name where a shipped build looked, not a source path that does
  // not exist on their machine.
  const auto resolved =
      ant::app::resolve_bundled_file("config/progression.json", kExe, kCwd, kSource, only({}));
  CHECK(resolved == kExe / "config/progression.json");
}

TEST_CASE("an empty candidate directory is skipped", "[app][assets]") {
  const auto resolved = ant::app::resolve_bundled_file(
      "config/progression.json", {}, kCwd, kSource, only({kCwd / "config/progression.json"}));
  CHECK(resolved == kCwd / "config/progression.json");
}
