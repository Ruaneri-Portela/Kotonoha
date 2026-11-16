#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <Kotonoha/Filesystem/Adapters/StreamAdapter.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cerrno>
#include <exception>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Kotonoha {
namespace {

bool Fail(std::string *error, const char *text) {
  if (error != nullptr)
    *error = text != nullptr ? text : "School Days save storage error";
  return false;
}

constexpr size_t kMaxGlobalFlagBytes = 32u * 1024u * 1024u;
std::atomic<uint64_t> gGlobalFlagTempSequence{0};

struct RemovePending {
  std::filesystem::path path;
  bool keep = false;
  ~RemovePending() {
    if (!keep) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  }
};

// A crashed writer can leave a uniquely named pending file. Never inspect it
// as save data. Clean only old regular files belonging to a process that is
// demonstrably gone; a stalled or active writer retains its own temp file.
void PruneStaleGlobalFlagTemps(const std::filesystem::path &target) {
  namespace fs = std::filesystem;
  const auto prefix = target.filename().string() + ".pending.";
  const auto now = fs::file_time_type::clock::now();
  std::error_code ec;
  fs::directory_iterator it(target.parent_path(), ec);
  const fs::directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto candidate = it->path();
    const auto name = candidate.filename().string();
    if (name.compare(0, prefix.size(), prefix) != 0)
      continue;
    const auto delimiter = name.find('.', prefix.size());
    if (delimiter == std::string::npos)
      continue;
    unsigned long long pid = 0;
    try {
      const auto token = name.substr(prefix.size(), delimiter - prefix.size());
      if (token.empty() || token.find_first_not_of("0123456789") !=
                               std::string::npos)
        continue;
      pid = std::stoull(token);
    } catch (const std::exception &) {
      continue;
    }
    if (pid == 0 || pid > static_cast<unsigned long long>(INT_MAX))
      continue;
#ifdef _WIN32
    HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                    static_cast<DWORD>(pid));
    if (process != nullptr) {
      CloseHandle(process);
      continue;
    }
    if (GetLastError() != ERROR_INVALID_PARAMETER)
      continue;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH)
      continue;
#endif
    std::error_code check;
    const auto status = fs::symlink_status(candidate, check);
    if (check || !fs::is_regular_file(status))
      continue;
    const auto age = now - fs::last_write_time(candidate, check);
    if (!check && age > std::chrono::hours(24)) {
      std::error_code ignored;
      fs::remove(candidate, ignored);
    }
  }
}

std::filesystem::path UniqueGlobalFlagTemp(const std::filesystem::path &target) {
  auto temp = target;
  const auto ticks = std::chrono::steady_clock::now()
                         .time_since_epoch().count();
  const auto sequence = gGlobalFlagTempSequence.fetch_add(1);
#ifdef _WIN32
  const auto pid = GetCurrentProcessId();
#else
  const auto pid = ::getpid();
#endif
  temp += ".pending." + std::to_string(pid) + "." +
          std::to_string(ticks) + "." + std::to_string(sequence);
  return temp;
}

} // namespace

bool SchoolDaysSaveStorage::Initialize(
    const std::filesystem::path &executableDirectory,
    const SchoolDaysSaveProfile &profile, std::string *error) {
  if (error != nullptr)
    error->clear();
  initialized_ = false;
  baseDirectory_.clear();
  profile_ = {};
  if (executableDirectory.empty() || profile.saveFolder.empty()) {
    return Fail(error, "School Days save storage init arguments are invalid");
  }
  baseDirectory_ =
      std::filesystem::absolute(executableDirectory).lexically_normal();
  profile_ = profile;

  std::filesystem::path folder;
  if (!ResolveRelative(profile_.saveFolder, &folder, error))
    return false;
  // Native init ignores CreateDirectoryA's return. Mirror that ownership:
  // attempt exactly once here and let later slot open report any real failure.
  std::error_code ignored;
  (void)std::filesystem::create_directories(folder, ignored);
  initialized_ = true;
  return true;
}

bool SchoolDaysSaveStorage::ResolveRelative(const std::string &configured,
                                            std::filesystem::path *out,
                                            std::string *error) const {
  if (out == nullptr || configured.empty() || baseDirectory_.empty()) {
    return Fail(error,
                "School Days save path resolution arguments are invalid");
  }
  std::filesystem::path relative(configured);
  // The authenticated v1.02 profile is relative to the executable.  Refuse
  // absolute/traversal paths here rather than letting extracted config escape
  // the Kotonoha runtime directory.
  if (relative.is_absolute() || relative.has_root_name() ||
      relative.has_root_directory()) {
    return Fail(error, "School Days save path must be executable-relative");
  }
  const auto normalized = relative.lexically_normal();
  for (const auto &part : normalized) {
    if (part == "..")
      return Fail(error, "School Days save path escapes executable directory");
  }
  *out = (baseDirectory_ / normalized).lexically_normal();
  return true;
}

bool SchoolDaysSaveStorage::ReadFile(const std::filesystem::path &path,
                                     std::vector<uint8_t> *bytes,
                                     std::string *error) {
  if (bytes == nullptr)
    return Fail(error, "School Days read output is null");
  bytes->clear();
  Kotonoha::Filesystem::InputFile file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return Fail(error, "School Days save file could not be opened for read");
  const auto end = file.tellg();
  if (end < 0 ||
      static_cast<uintmax_t>(end) >
          static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
    return Fail(error, "School Days save file size is invalid");
  }
  bytes->resize(static_cast<size_t>(end));
  file.seekg(0, std::ios::beg);
  if (!bytes->empty() &&
      !file.read(reinterpret_cast<char *>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()))) {
    bytes->clear();
    return Fail(error, "School Days save file read failed");
  }
  return true;
}

bool SchoolDaysSaveStorage::WriteFileTruncate(const std::filesystem::path &path,
                                              const std::vector<uint8_t> &bytes,
                                              std::string *error) {
  if (bytes.empty())
    return Fail(error, "School Days save write payload is empty");
  Kotonoha::Filesystem::OutputFile file(path, std::ios::binary | std::ios::trunc);
  if (!file)
    return Fail(error, "School Days save file could not be opened for write");
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file)
    return Fail(error, "School Days save file write failed");
  file.close();
  if (!file)
    return Fail(error, "School Days save file close failed");
  return true;
}

bool SchoolDaysSaveStorage::WriteGlobalFlagAtomic(
    const std::filesystem::path &path, const std::vector<uint8_t> &bytes,
    std::string *error) const {
  if (bytes.empty() || bytes.size() > kMaxGlobalFlagBytes)
    return Fail(error, "School Days GlobalFlag payload is empty or too large");
  PruneStaleGlobalFlagTemps(path);
  const auto temp = UniqueGlobalFlagTemp(path);
  RemovePending cleanup{temp};
  using Fault = GlobalFlagWriteFault;

#ifdef _WIN32
  HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return Fail(error, ("School Days GlobalFlag temp create failed at " +
                        temp.u8string() + " (Win32 " +
                        std::to_string(GetLastError()) + ")").c_str());
  if (globalFlagWriteFaultForTesting_ == Fault::Write) {
    DWORD written = 0;
    (void)WriteFile(handle, bytes.data(), 1, &written, nullptr);
    CloseHandle(handle);
    return Fail(error, "School Days GlobalFlag simulated write failure");
  }
  size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD chunk = static_cast<DWORD>(
        std::min<size_t>(bytes.size() - offset, 1024u * 1024u));
    DWORD written = 0;
    if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) ||
        written == 0) {
      const auto code = GetLastError();
      CloseHandle(handle);
      return Fail(error, ("School Days GlobalFlag temp write failed (Win32 " +
                          std::to_string(code) + ")").c_str());
    }
    offset += written;
  }
  if (globalFlagWriteFaultForTesting_ == Fault::Flush ||
      !FlushFileBuffers(handle)) {
    const auto code = GetLastError();
    CloseHandle(handle);
    return Fail(error, ("School Days GlobalFlag temp flush failed (Win32 " +
                        std::to_string(code) + ")").c_str());
  }
  if (!CloseHandle(handle))
    return Fail(error, "School Days GlobalFlag temp close failed");
  if (globalFlagWriteFaultForTesting_ == Fault::SimulatedInterruption) {
    cleanup.keep = true; // a simulated crash deliberately leaves an orphan.
    return Fail(error, "School Days GlobalFlag simulated interruption");
  }
  if (globalFlagWriteFaultForTesting_ == Fault::Publish)
    return Fail(error, "School Days GlobalFlag simulated publish failure");

  DWORD last = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (MoveFileExW(temp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return true;
    last = GetLastError();
    if (attempt == 2 || (last != ERROR_SHARING_VIOLATION &&
                         last != ERROR_LOCK_VIOLATION &&
                         last != ERROR_ACCESS_DENIED))
      break;
    Sleep(40u * static_cast<DWORD>(attempt + 1));
  }
  return Fail(error, ("School Days GlobalFlag atomic replace failed at " +
                      path.u8string() + " (Win32 " +
                      std::to_string(last) + "); previous file preserved")
                         .c_str());
#else
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0)
    return Fail(error, "School Days GlobalFlag temp create failed");
  if (globalFlagWriteFaultForTesting_ == Fault::Write) {
    (void)::write(fd, bytes.data(), 1);
    ::close(fd);
    return Fail(error, "School Days GlobalFlag simulated write failure");
  }
  size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset,
                               bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      ::close(fd);
      return Fail(error, "School Days GlobalFlag temp write failed");
    }
    offset += static_cast<size_t>(count);
  }
  if (globalFlagWriteFaultForTesting_ == Fault::Flush || ::fsync(fd) != 0) {
    ::close(fd);
    return Fail(error, "School Days GlobalFlag temp fsync failed");
  }
  if (::close(fd) != 0)
    return Fail(error, "School Days GlobalFlag temp close failed");
  if (globalFlagWriteFaultForTesting_ == Fault::SimulatedInterruption) {
    cleanup.keep = true;
    return Fail(error, "School Days GlobalFlag simulated interruption");
  }
  if (globalFlagWriteFaultForTesting_ == Fault::Publish)
    return Fail(error, "School Days GlobalFlag simulated publish failure");

  const int dirfd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (dirfd < 0)
    return Fail(error, "School Days GlobalFlag directory open failed");
  if (::fsync(dirfd) != 0) {
    ::close(dirfd);
    return Fail(error, "School Days GlobalFlag pre-publication fsync failed");
  }
  int renameError = 0;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (::rename(temp.c_str(), path.c_str()) == 0) {
      renameError = 0;
      break;
    }
    renameError = errno;
    if (attempt == 2 || (renameError != EACCES && renameError != EBUSY))
      break;
    ::usleep(static_cast<useconds_t>((attempt + 1) * 40000));
  }
  if (renameError != 0) {
    ::close(dirfd);
    return Fail(error, "School Days GlobalFlag atomic rename failed");
  }
  if (::fsync(dirfd) != 0) {
    ::close(dirfd);
    // Publication has already happened. Do not roll back here: restoring an
    // old inode can destroy a newer commit from another writer.
    return Fail(error, "School Days GlobalFlag published, but directory "
                       "durability could not be confirmed");
  }
  ::close(dirfd);
  return true;
#endif
}

bool SchoolDaysSaveStorage::LoadGlobalFlag(std::vector<uint8_t> *bytes,
                                           bool *present,
                                           std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || bytes == nullptr || present == nullptr) {
    return Fail(error,
                "School Days GlobalFlag load storage is not initialized");
  }
  *present = false;
  bytes->clear();
  std::filesystem::path path;
  if (!ResolveRelative(profile_.flagFileName, &path, error))
    return false;
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec)
    return Fail(error, "School Days GlobalFlag existence query failed");
  if (!exists)
    return true; // OPEN_EXISTING miss is a normal first-run state.
  if (!ReadFile(path, bytes, error))
    return false;
  *present = true;
  return true;
}

bool SchoolDaysSaveStorage::ProbeSaveFile(int slot, bool *openable,
                                          std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || openable == nullptr || slot < 0 || slot >= 100) {
    return Fail(error, "School Days save-slot probe arguments are invalid");
  }
  *openable = false;

  std::string configured;
  std::string profileError;
  if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                            &configured, &profileError)) {
    return Fail(error, profileError.empty()
                           ? "School Days save-slot path format is invalid"
                           : profileError.c_str());
  }

  std::filesystem::path path;
  if (!ResolveRelative(configured, &path, error))
    return false;

  // Native +0x9C treats an unsuccessful file open as an unoccupied slot and
  // returns 0; it does not escalate the miss through the SysMenu interface.
  Kotonoha::Filesystem::InputFile file(path, std::ios::binary);
  if (!file)
    return true;
  *openable = true;
  return true;
}

bool SchoolDaysSaveStorage::ReadSaveFile(int slot, std::vector<uint8_t> *bytes,
                                         std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || bytes == nullptr || slot < 0 || slot >= 100) {
    return Fail(error, "School Days save-slot read arguments are invalid");
  }
  bytes->clear();

  std::string configured;
  std::string profileError;
  if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                            &configured, &profileError)) {
    return Fail(error, profileError.empty()
                           ? "School Days save-slot path format is invalid"
                           : profileError.c_str());
  }
  std::filesystem::path path;
  if (!ResolveRelative(configured, &path, error))
    return false;
  return ReadFile(path, bytes, error);
}

bool SchoolDaysSaveStorage::SaveFilePath(
    int slot, std::filesystem::path *path, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_ || path == nullptr || slot < 0 || slot >= 100)
    return Fail(error, "School Days save-slot path arguments are invalid");
  std::string configured;
  std::string profileError;
  if (!SchoolDaysSaveProfile::FormatIndexed(profile_.saveFileName, slot,
                                           &configured, &profileError))
    return Fail(error, profileError.empty()
                           ? "School Days save-slot path format is invalid"
                           : profileError.c_str());
  return ResolveRelative(configured, path, error);
}

bool SchoolDaysSaveStorage::WriteSaveFile(
    const SchoolDaysSaveService::Plan &plan, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(plan.saveFilePath, &path, error))
    return false;
  // No lazy mkdir here: native writer relies on init-time SaveFolder creation.
  return WriteFileTruncate(path, plan.saveFileBytes, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlag(
    const SchoolDaysSaveService::Plan &plan, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(plan.globalFlagPath, &path, error))
    return false;
  return WriteGlobalFlagAtomic(path, plan.globalFlagBytes, error);
}

bool SchoolDaysSaveStorage::WriteGlobalFlagBytes(
    const std::vector<uint8_t> &bytes, std::string *error) const {
  if (error != nullptr)
    error->clear();
  if (!initialized_)
    return Fail(error, "School Days save storage is not initialized");
  std::filesystem::path path;
  if (!ResolveRelative(profile_.flagFileName, &path, error))
    return false;
  return WriteGlobalFlagAtomic(path, bytes, error);
}

} // namespace Kotonoha
