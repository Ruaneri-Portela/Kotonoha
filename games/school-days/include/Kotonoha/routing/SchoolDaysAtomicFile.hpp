#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Game-specific atomic GlobalFlag storage, with no engine or SDL dependency.
// Kept standalone so this exact implementation can be compiled on POSIX CI.
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
