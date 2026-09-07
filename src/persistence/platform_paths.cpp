#include "persistence/platform_paths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace ant::persistence {

std::filesystem::path default_save_directory() {
#if defined(_WIN32)
  const char* root = std::getenv("LOCALAPPDATA");
  if (root == nullptr) throw std::runtime_error("LOCALAPPDATA is unavailable");
  return std::filesystem::path(root) / "AntFarmSim";
#elif defined(__APPLE__)
  const char* root = std::getenv("HOME");
  if (root == nullptr) throw std::runtime_error("HOME is unavailable");
  return std::filesystem::path(root) / "Library" / "Application Support" / "AntFarmSim";
#else
  if (const char* root = std::getenv("XDG_DATA_HOME")) return std::filesystem::path(root) / "AntFarmSim";
  const char* root = std::getenv("HOME");
  if (root == nullptr) throw std::runtime_error("HOME is unavailable");
  return std::filesystem::path(root) / ".local" / "share" / "AntFarmSim";
#endif
}

} // namespace ant::persistence
