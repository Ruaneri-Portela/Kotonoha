#include <Kotonoha/routing/SchoolDaysAtomicFile.hpp>

#include <Kotonoha/Filesystem/AtomicFile.hpp>

namespace Kotonoha::SchoolDaysAtomicFile {

bool Write(const std::filesystem::path &path,
           const std::vector<uint8_t> &bytes, Fault fault,
           std::string *error) {
  constexpr std::size_t maxGlobalFlagBytes = 32u * 1024u * 1024u;
  if (bytes.empty() || bytes.size() > maxGlobalFlagBytes) {
    if (error)
      *error = "School Days GlobalFlag payload is empty or too large";
    return false;
  }

  Filesystem::AtomicFileFault injected = Filesystem::AtomicFileFault::None;
  switch (fault) {
  case Fault::None:
    break;
  case Fault::Write:
    injected = Filesystem::AtomicFileFault::Write;
    break;
  case Fault::Flush:
    injected = Filesystem::AtomicFileFault::Flush;
    break;
  case Fault::Publish:
    injected = Filesystem::AtomicFileFault::Publish;
    break;
  case Fault::SimulatedInterruption:
    injected = Filesystem::AtomicFileFault::SimulatedInterruption;
    break;
  case Fault::DirectoryFlushBefore:
    injected = Filesystem::AtomicFileFault::DirectoryFlushBefore;
    break;
  case Fault::DirectoryFlushAfter:
    injected = Filesystem::AtomicFileFault::DirectoryFlushAfter;
    break;
  }
  return Filesystem::writeBinaryFileAtomically(path, bytes, error, injected);
}

} // namespace Kotonoha::SchoolDaysAtomicFile
