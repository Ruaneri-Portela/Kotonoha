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

// Atomically publishes an exclusive, same-directory temporary. On Windows
// the temp is flushed with FlushFileBuffers and replaced via MoveFileExW
// (REPLACE_EXISTING | WRITE_THROUGH). On POSIX, fsync applies to the file and
// to the parent directory before and after rename; macOS uses F_FULLFSYNC
// when supported. Errors before publication leave the previous file intact.
// Errors after publication report durability uncertainty without rollback.
// Only old regular temporaries whose owner PID is confirmed dead are removed.
// The Emscripten SDL/IDBFS fallback offers logical atomicity, NOT
// hardware-power-loss durability (it cannot fsync a physical directory).
// Platform storage may still lie about persistence after an acknowledged sync.
bool writeBinaryFileAtomically(const std::filesystem::path &path,
                               const std::vector<std::uint8_t> &bytes,
                               std::string *error = nullptr,
                               AtomicFileFault fault = AtomicFileFault::None);

} // namespace Kotonoha::Filesystem
