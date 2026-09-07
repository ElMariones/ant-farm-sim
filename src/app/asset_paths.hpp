#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace ant::app {

using PathPredicate = std::function<bool(const std::filesystem::path&)>;

// Resolves a bundled data file such as `config/progression.json`.
//
// A packaged application is launched from Finder with an arbitrary working directory, so the only
// dependable anchor is the executable's own directory. The working directory is tried next so a
// developer can run from the repository root, and the source tree last as a development fallback
// that does not exist on a player's machine.
//
// Returns the first candidate that exists. When none do, returns the executable-relative path, so
// the resulting error names the location a shipped build would actually have used.
[[nodiscard]] std::filesystem::path resolve_bundled_file(
    std::string_view relative_path, const std::filesystem::path& executable_directory,
    const std::filesystem::path& working_directory, const std::filesystem::path& source_directory,
    const PathPredicate& exists);

} // namespace ant::app
