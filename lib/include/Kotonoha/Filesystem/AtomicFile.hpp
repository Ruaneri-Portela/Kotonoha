#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Kotonoha::Filesystem {

// Test-only failure injection shared by storage clients. Production callers
// always use None. No game-specific flags are stored by the engine.
enum class AtomicFileFault {
  None,
  Write,
  Flush,
  Publish,
  SimulatedInterruption,
  DirectoryFlushBefore,
  DirectoryFlushAfter
};

// Stages an entire payload in an exclusive same-directory temporary file,
// flushes and closes that file, then publishes via SDL_RenamePath. A reader
// sees either the former complete file or the newly published complete file.
// This API does NOT promise a hardware-power-loss fsync of the parent directory.
// "After" faults report publication uncertainty and never roll back a newer
// file. Pending files from interrupted writers are ignored, never loaded as
// save data, and are not opportunistically deleted without process ownership.
bool writeBinaryFileAtomically(const std::filesystem::path &path,
                               const std::vector<std::uint8_t> &bytes,
                               std::string *error = nullptr,
                               AtomicFileFault fault = AtomicFileFault::None);

} // namespace Kotonoha::Filesystem
