#include "persistence/save_service.hpp"

#include "persistence/profile_codec.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#if !defined(_WIN32)
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ant::persistence {
namespace {
class StandardFileOps final : public FileOps {
public:
  void create_directories(const std::filesystem::path& path) override { std::filesystem::create_directories(path); }
  bool exists(const std::filesystem::path& path) const override { return std::filesystem::exists(path); }
  std::string read(const std::filesystem::path& path) const override {
    const auto size=std::filesystem::file_size(path); if(size>kMaximumProfileBytes) throw std::runtime_error("profile exceeds 64 MiB");
    std::ifstream input(path,std::ios::binary); if(!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  }
  void write_and_flush(const std::filesystem::path& path, const std::string_view contents) override {
#if defined(_WIN32)
    std::ofstream output(path,std::ios::binary|std::ios::trunc); output.write(contents.data(),static_cast<std::streamsize>(contents.size())); output.flush(); if(!output) throw std::runtime_error("failed to write " + path.string());
#else
    const int handle=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(handle<0) throw std::system_error(errno,std::generic_category(),"open temporary save");
    std::size_t offset=0; while(offset<contents.size()){const ssize_t count=::write(handle,contents.data()+offset,contents.size()-offset);if(count<0){const int error=errno;::close(handle);throw std::system_error(error,std::generic_category(),"write temporary save");}offset+=static_cast<std::size_t>(count);} if(::fsync(handle)!=0){const int error=errno;::close(handle);throw std::system_error(error,std::generic_category(),"flush temporary save");} if(::close(handle)!=0) throw std::system_error(errno,std::generic_category(),"close temporary save");
#endif
  }
  void replace(const std::filesystem::path& from,const std::filesystem::path& to) override {
#if defined(_WIN32)
    std::error_code error; std::filesystem::remove(to,error); std::filesystem::rename(from,to);
#else
    if(::rename(from.c_str(),to.c_str())!=0) throw std::system_error(errno,std::generic_category(),"replace save");
#endif
  }
  void sync_directory(const std::filesystem::path& path) override {
#if !defined(_WIN32)
    const int handle=::open(path.c_str(),O_RDONLY|O_DIRECTORY); if(handle<0) throw std::system_error(errno,std::generic_category(),"open save directory"); if(::fsync(handle)!=0){const int error=errno;::close(handle);throw std::system_error(error,std::generic_category(),"sync save directory");}::close(handle);
#else
    static_cast<void>(path);
#endif
  }
  void remove_if_exists(const std::filesystem::path& path) noexcept override { std::error_code error;std::filesystem::remove(path,error); }
};
}

std::shared_ptr<FileOps> standard_file_ops(){return std::make_shared<StandardFileOps>();}

SaveService::SaveService(std::filesystem::path directory,std::shared_ptr<FileOps> file_ops)
    :directory_(std::move(directory)),file_ops_(std::move(file_ops)){if(!file_ops_)throw std::invalid_argument("FileOps is required");file_ops_->create_directories(directory_);acquire_lock();}
SaveService::~SaveService(){release_lock();}

void SaveService::acquire_lock(){
#if defined(_WIN32)
  const auto lock=directory_/"profile.lock"; const int handle=::_open(lock.string().c_str(),_O_CREAT|_O_EXCL|_O_WRONLY,_S_IREAD|_S_IWRITE); if(handle<0)throw std::runtime_error("profile is already locked");lock_handle_=handle;
#else
  const auto lock=directory_/"profile.lock";lock_handle_=::open(lock.c_str(),O_RDWR|O_CREAT,0600);if(lock_handle_<0)throw std::system_error(errno,std::generic_category(),"open profile lock");if(::flock(lock_handle_,LOCK_EX|LOCK_NB)!=0){::close(lock_handle_);lock_handle_=-1;throw std::runtime_error("profile is already open by another process");}
#endif
}
void SaveService::release_lock() noexcept {if(lock_handle_<0)return;
#if defined(_WIN32)
  ::_close(lock_handle_);std::error_code error;std::filesystem::remove(directory_/"profile.lock",error);
#else
  ::flock(lock_handle_,LOCK_UN);::close(lock_handle_);
#endif
  lock_handle_=-1;}

std::filesystem::path SaveService::unique_temporary(const std::string_view stem){++temporary_counter_;return directory_/(std::string(stem)+"."+std::to_string(temporary_counter_)+".tmp");}

LoadResult SaveService::load() const {
  const auto current=directory_/"profile.json";const auto backup=directory_/"profile.backup.json";
  if(!file_ops_->exists(current))return {};
  DecodeResult decoded;
  try{decoded=decode_profile(file_ops_->read(current));}catch(const std::exception& error){decoded.error=error.what();}
  if(decoded.profile)return {LoadState::Loaded,std::move(decoded.profile),std::nullopt,{}};
  std::optional<game::ProfileSnapshot> valid_backup;
  if(file_ops_->exists(backup)){try{DecodeResult candidate=decode_profile(file_ops_->read(backup));valid_backup=std::move(candidate.profile);}catch(const std::exception&) {}}
  return {valid_backup?LoadState::RecoveryAvailable:LoadState::Invalid,std::nullopt,std::move(valid_backup),"current profile is invalid: "+decoded.error};
}

SaveResult SaveService::commit(game::ProfileSnapshot candidate) {
  const auto current=directory_/"profile.json";const auto backup=directory_/"profile.backup.json";
  std::uint64_t current_revision=0;std::string current_contents;
  if(file_ops_->exists(current)){
    try{current_contents=file_ops_->read(current);const DecodeResult decoded=decode_profile(current_contents);if(!decoded.profile)return {false,false,std::nullopt,"refusing to overwrite invalid current profile: "+decoded.error};current_revision=decoded.profile->revision;}catch(const std::exception& error){return {false,false,std::nullopt,error.what()};}
  }
  if(current_revision==std::numeric_limits<std::uint64_t>::max())return {false,false,std::nullopt,"profile revision overflow"};
  candidate.revision=current_revision+1;const std::string encoded=encode_profile(candidate);const DecodeResult verified=decode_profile(encoded);if(!verified.profile)return {false,false,std::nullopt,"candidate validation failed: "+verified.error};
  const auto candidate_temp=unique_temporary("profile");const auto backup_temp=unique_temporary("backup");
  try{
    file_ops_->write_and_flush(candidate_temp,encoded);
    const DecodeResult disk_candidate=decode_profile(file_ops_->read(candidate_temp));if(!disk_candidate.profile)throw std::runtime_error("temporary candidate failed verification: "+disk_candidate.error);
    if(!current_contents.empty()){
      file_ops_->write_and_flush(backup_temp,current_contents);const DecodeResult disk_backup=decode_profile(file_ops_->read(backup_temp));if(!disk_backup.profile)throw std::runtime_error("temporary backup failed verification: "+disk_backup.error);file_ops_->replace(backup_temp,backup);
    }
    try{file_ops_->replace(candidate_temp,current);}catch(const std::exception& replace_error){try{const DecodeResult authoritative=decode_profile(file_ops_->read(current));if(authoritative.profile&&authoritative.profile->revision==candidate.revision)return {true,true,std::move(authoritative.profile),std::string("replacement outcome recovered after error: ")+replace_error.what()};}catch(const std::exception&){}throw;}
    try{file_ops_->sync_directory(directory_);}catch(const std::exception& sync_error){const DecodeResult authoritative=decode_profile(file_ops_->read(current));if(authoritative.profile&&authoritative.profile->revision==candidate.revision)return {true,true,std::move(authoritative.profile),std::string("save committed but directory sync failed: ")+sync_error.what()};throw;}
    return {true,false,std::move(candidate),{}};
  }catch(const std::exception& error){file_ops_->remove_if_exists(candidate_temp);file_ops_->remove_if_exists(backup_temp);return {false,false,std::nullopt,error.what()};}
}

SaveResult SaveService::recover_backup(const game::ProfileSnapshot& backup) {
  return replace_unreadable(backup);
}

SaveResult SaveService::replace_unreadable(const game::ProfileSnapshot& replacement) {
  const auto current = directory_ / "profile.json";
  if (file_ops_->exists(current)) {
    const auto retained =
        directory_ / ("profile.corrupt." + std::to_string(++temporary_counter_) + ".json");
    try {
      file_ops_->replace(current, retained);
    } catch (const std::exception& error) {
      return {false, false, std::nullopt,
              std::string("could not retain corrupt profile: ") + error.what()};
    }
  }
  return commit(replacement);
}

} // namespace ant::persistence
