#include "app/asset_paths.hpp"

#include <array>

namespace ant::app {

std::filesystem::path resolve_bundled_file(const std::string_view relative_path,
                                           const std::filesystem::path& executable_directory,
                                           const std::filesystem::path& working_directory,
                                           const std::filesystem::path& source_directory,
                                           const PathPredicate& exists) {
  const std::filesystem::path relative{relative_path};
  const std::array<std::filesystem::path, 3> candidates{
      executable_directory / relative, working_directory / relative, source_directory / relative};
  for (const std::filesystem::path& candidate : candidates) {
    if (!candidate.empty() && exists && exists(candidate)) return candidate;
  }
  return candidates.front();
}

} // namespace ant::app
