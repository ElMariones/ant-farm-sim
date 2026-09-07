#pragma once

#include "game/progression.hpp"

#include <filesystem>

namespace ant::persistence {

[[nodiscard]] game::ProgressionConfig load_progression(const std::filesystem::path& path);

} // namespace ant::persistence
