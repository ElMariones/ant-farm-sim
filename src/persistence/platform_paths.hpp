#pragma once

#include <filesystem>

namespace ant::persistence {

[[nodiscard]] std::filesystem::path default_save_directory();

} // namespace ant::persistence
