#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// School Days-specific FlgH size and fault policy; the shared Filesystem
// atomic helper owns Unicode paths, exclusive temporaries, native sync/rename
// and old orphan cleanup. No platform-specific I/O belongs in this port.
namespace Kotonoha::SchoolDaysAtomicFile {

enum class Fault {
  None,
  Write,
  Flush,
  Publish,
  SimulatedInterruption,
  DirectoryFlushBefore,
  DirectoryFlushAfter
};

// A failure after rename is reported, but the newly published file is never
// rolled back (another writer may already have replaced it).
bool Write(const std::filesystem::path &path, const std::vector<uint8_t> &bytes,
           Fault fault = Fault::None, std::string *error = nullptr);

} // namespace Kotonoha::SchoolDaysAtomicFile
