#pragma once

#include "game/snapshot.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ant::persistence {

class FileOps {
public:
  virtual ~FileOps() = default;
  virtual void create_directories(const std::filesystem::path& path) = 0;
  [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;
  [[nodiscard]] virtual std::string read(const std::filesystem::path& path) const = 0;
  virtual void write_and_flush(const std::filesystem::path& path, std::string_view contents) = 0;
  virtual void replace(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
  virtual void sync_directory(const std::filesystem::path& path) = 0;
  virtual void remove_if_exists(const std::filesystem::path& path) noexcept = 0;
};

[[nodiscard]] std::shared_ptr<FileOps> standard_file_ops();

enum class LoadState { NewProfile, Loaded, RecoveryAvailable, Invalid };
struct LoadResult {
  LoadState state{LoadState::NewProfile};
  std::optional<game::ProfileSnapshot> profile;
  std::optional<game::ProfileSnapshot> backup;
  std::string error;
};

struct SaveResult {
  bool committed{};
  bool durability_uncertain{};
  std::optional<game::ProfileSnapshot> profile;
  std::string error;
};

class SaveService {
public:
  explicit SaveService(std::filesystem::path directory,
                       std::shared_ptr<FileOps> file_ops = standard_file_ops());
  ~SaveService();
  SaveService(const SaveService&) = delete;
  SaveService& operator=(const SaveService&) = delete;

  [[nodiscard]] LoadResult load() const;
  [[nodiscard]] SaveResult commit(game::ProfileSnapshot candidate);
  [[nodiscard]] SaveResult recover_backup(const game::ProfileSnapshot& backup);
  // Moves an unreadable current profile aside for diagnosis, then commits `replacement`. Used for
  // both backup recovery and starting over; never called automatically.
  [[nodiscard]] SaveResult replace_unreadable(const game::ProfileSnapshot& replacement);
  [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

private:
  void acquire_lock();
  void release_lock() noexcept;
  [[nodiscard]] std::filesystem::path unique_temporary(std::string_view stem);

  std::filesystem::path directory_;
  std::shared_ptr<FileOps> file_ops_;
  std::uint64_t temporary_counter_{};
  int lock_handle_{-1};
};

} // namespace ant::persistence
