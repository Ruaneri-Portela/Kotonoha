#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Small game-specific GlobalFlag payload policy. File creation, Unicode
// filenames, flush, publication and cleanup are handled by the shared
// Kotonoha::Filesystem SDL backend; no OS-specific I/O belongs here.
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
