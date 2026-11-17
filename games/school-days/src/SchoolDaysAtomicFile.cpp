#include <Kotonoha/routing/SchoolDaysAtomicFile.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cerrno>
#include <cstdio>
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

namespace Kotonoha::SchoolDaysAtomicFile {
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

#ifndef _WIN32
// POSIX fsync may return EINTR while the descriptor remains valid. On macOS,
// F_FULLFSYNC requests the stronger flush through drive caches where present.
// ENOTSUP means that filesystem/device cannot provide it: use fsync instead.
bool DurableFileSync(int fd) {
#if defined(__APPLE__) && defined(F_FULLFSYNC)
  for (;;) {
    if (::fcntl(fd, F_FULLFSYNC) == 0)
      return true;
    if (errno == EINTR)
      continue;
    if (errno != ENOTSUP)
      return false;
    break;
  }
#endif
  for (;;) {
    if (::fsync(fd) == 0)
      return true;
    if (errno != EINTR)
      return false;
  }
}

bool SyncDirectory(int fd) {
  for (;;) {
    if (::fsync(fd) == 0)
      return true;
    if (errno != EINTR)
      return false;
  }
}
#endif

} // namespace

bool Write(const std::filesystem::path &path,
           const std::vector<uint8_t> &bytes, Fault fault,
           std::string *error) {
  if (bytes.empty() || bytes.size() > kMaxGlobalFlagBytes)
    return Fail(error, "School Days GlobalFlag payload is empty or too large");
  PruneStaleGlobalFlagTemps(path);
  const auto temp = UniqueGlobalFlagTemp(path);
  RemovePending cleanup{temp};

#ifdef _WIN32
  HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return Fail(error, ("School Days GlobalFlag temp create failed at " +
                        temp.u8string() + " (Win32 " +
                        std::to_string(GetLastError()) + ")").c_str());
  if (fault == Fault::Write) {
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
  if (fault == Fault::Flush ||
      !FlushFileBuffers(handle)) {
    const auto code = GetLastError();
    CloseHandle(handle);
    return Fail(error, ("School Days GlobalFlag temp flush failed (Win32 " +
                        std::to_string(code) + ")").c_str());
  }
  if (!CloseHandle(handle))
    return Fail(error, "School Days GlobalFlag temp close failed");
  if (fault == Fault::SimulatedInterruption) {
    cleanup.keep = true; // a simulated crash deliberately leaves an orphan.
    return Fail(error, "School Days GlobalFlag simulated interruption");
  }
  if (fault == Fault::Publish || fault == Fault::DirectoryFlushBefore)
    return Fail(error, "School Days GlobalFlag simulated publish failure");

  DWORD last = ERROR_SUCCESS;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (MoveFileExW(temp.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      if (fault == Fault::DirectoryFlushAfter)
        return Fail(error, "School Days GlobalFlag published, simulated "
                           "post-publication durability uncertainty");
      return true;
    }
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
  if (fault == Fault::Write) {
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
  if (fault == Fault::Flush || !DurableFileSync(fd)) {
    ::close(fd);
    return Fail(error, "School Days GlobalFlag temp fsync failed");
  }
  if (::close(fd) != 0)
    return Fail(error, "School Days GlobalFlag temp close failed");
  if (fault == Fault::SimulatedInterruption) {
    cleanup.keep = true;
    return Fail(error, "School Days GlobalFlag simulated interruption");
  }
  if (fault == Fault::Publish)
    return Fail(error, "School Days GlobalFlag simulated publish failure");

  const int dirfd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (dirfd < 0)
    return Fail(error, "School Days GlobalFlag directory open failed");
  if (fault == Fault::DirectoryFlushBefore || !SyncDirectory(dirfd)) {
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
  if (fault == Fault::DirectoryFlushAfter || !SyncDirectory(dirfd)) {
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

} // namespace Kotonoha::SchoolDaysAtomicFile
