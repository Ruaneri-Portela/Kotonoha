#include <Kotonoha/Filesystem/AtomicFile.hpp>

#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>

namespace Kotonoha::Filesystem {
namespace {

std::atomic<std::uint64_t> nextTemporary{0};
// Serialize in-process publishes. SDL_RenamePath on Windows can transiently
// reject another thread's replacement while the destination is being opened.
// This mutex is shared by all filesystem clients, not a game-specific lock.
std::mutex atomicPublicationMutex;

bool fail(std::string *error, const std::string &reason) {
  if (error)
    *error = reason;
  return false;
}

std::string uniqueTemporary(const std::filesystem::path &path) {
  // The path is encoded as UTF-8 for SDL, including on Windows. SDL's "x"
  // file mode makes creation exclusive, so two writers cannot truncate each
  // other's temporary files even if a suffix collides.
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto sequence = nextTemporary.fetch_add(1, std::memory_order_relaxed);
  std::random_device random;
  std::filesystem::path temporary = path;
  temporary += ".pending." + std::to_string(tick) + "." +
               std::to_string(sequence) + "." + std::to_string(random()) + "." +
               std::to_string(random());
  return temporary.u8string();
}

struct PendingFile {
  std::string name;
  bool retain = false;
  ~PendingFile() {
    if (!retain && !name.empty())
      (void)SDL_RemovePath(name.c_str());
  }
};

} // namespace

bool writeBinaryFileAtomically(const std::filesystem::path &path,
                               const std::vector<std::uint8_t> &bytes,
                               std::string *error, AtomicFileFault fault) {
  if (error)
    error->clear();
  std::lock_guard<std::mutex> lock(atomicPublicationMutex);
  if (path.empty() || path.filename().empty())
    return fail(error, "Atomic file path is empty");
  // Do not mutate directories or follow a symlink over a save file.
  std::error_code statusError;
  const auto status = std::filesystem::symlink_status(path, statusError);
  if (statusError && statusError != std::errc::no_such_file_or_directory)
    return fail(error, "Cannot inspect atomic file destination");
  if (!statusError && (std::filesystem::is_symlink(status) ||
                       (std::filesystem::exists(status) &&
                        !std::filesystem::is_regular_file(status))))
    return fail(error, "Atomic file destination is not a regular file");
  const auto parent = path.parent_path().empty() ? std::filesystem::path(".")
                                                 : path.parent_path();
  statusError.clear();
  const auto directory = std::filesystem::symlink_status(parent, statusError);
  if (statusError || !std::filesystem::is_directory(directory) ||
      std::filesystem::is_symlink(directory))
    return fail(error, "Atomic file parent must be an existing directory");

  // Limit the process-local critical section to a temporary file. Creating
  // the parent directory is owned by the caller's storage-path policy.
  PendingFile pending{uniqueTemporary(path)};
  SDL_IOStream *output = SDL_IOFromFile(pending.name.c_str(), "wxb");
  if (!output)
    return fail(error, std::string("Cannot create unique atomic file: ") +
                           SDL_GetError());

  bool success = true;
  std::size_t offset = 0;
  if (fault == AtomicFileFault::Write) {
    if (!bytes.empty())
      (void)SDL_WriteIO(output, bytes.data(), 1);
    success = false;
  } else {
    while (offset < bytes.size()) {
      const std::size_t written =
          SDL_WriteIO(output, bytes.data() + offset, bytes.size() - offset);
      if (written == 0) {
        success = false;
        break;
      }
      offset += written;
    }
  }
  if (success && fault != AtomicFileFault::Flush)
    success = SDL_FlushIO(output);
  else
    success = false;
  const bool closed = SDL_CloseIO(output);
  if (!success || !closed)
    return fail(error, "Atomic file write, flush or close failed");

  if (fault == AtomicFileFault::SimulatedInterruption) {
    pending.retain = true;
    return fail(error, "Simulated interruption before atomic publish");
  }
  if (fault == AtomicFileFault::Publish ||
      fault == AtomicFileFault::DirectoryFlushBefore)
    return fail(error, "Simulated failure before atomic publish");

  // No delete-then-rename fallback: that would destroy the previous file if
  // publication fails. SDL_RenamePath replaces an existing destination.
  const std::string destination = path.u8string();
  bool published = false;
  std::string renameError;
  for (int attempt = 0; attempt < 4; ++attempt) {
    if (SDL_RenamePath(pending.name.c_str(), destination.c_str())) {
      published = true;
      break;
    }
    renameError = SDL_GetError();
    if (attempt != 3)
      SDL_Delay(static_cast<Uint32>(15 * (attempt + 1)));
  }
  if (!published)
    return fail(error, "Atomic file publish failed: " + renameError);
  if (fault == AtomicFileFault::DirectoryFlushAfter)
    return fail(error,
                "Atomic file published; simulated durability uncertainty");
  return true;
}

} // namespace Kotonoha::Filesystem
