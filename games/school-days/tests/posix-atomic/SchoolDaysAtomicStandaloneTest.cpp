#include <Kotonoha/routing/SchoolDaysAtomicFile.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
namespace atomic = Kotonoha::SchoolDaysAtomicFile;

void Check(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}

struct Sandbox {
  fs::path directory;
  Sandbox() {
    const auto clock = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    directory = fs::temp_directory_path() /
                ("school-days-atomic-standalone-" + std::to_string(clock));
    Check(fs::create_directories(directory), "Cannot create synthetic sandbox");
  }
  ~Sandbox() {
    std::error_code ignored;
    fs::permissions(directory, fs::perms::owner_all,
                    fs::perm_options::add, ignored);
    fs::remove_all(directory, ignored);
  }
};

// Construct independent synthetic FlgH payloads (no retail content or engine
// dependency). The first four bytes are the format marker; entries use a short
// one-character UTF-16 key and integer type 3, matching the documented codec.
void PositiveVarint(unsigned value, std::vector<uint8_t> *out) {
  if (value < 64u) {
    out->push_back(static_cast<uint8_t>(value));
  } else if (value < 8192u) {
    out->push_back(static_cast<uint8_t>(0x80u | (value >> 7)));
    out->push_back(static_cast<uint8_t>(value & 0x7fu));
  } else {
    throw std::runtime_error("Synthetic varint outside test range");
  }
}

std::vector<uint8_t> Profile(unsigned generation) {
  std::vector<uint8_t> result{'F', 'l', 'g', 'H'};
  const unsigned count = 4000u;
  PositiveVarint(count, &result);
  for (unsigned i = 0; i < count; ++i) {
    PositiveVarint(1, &result);   // one-character UTF-16 key
    result.push_back('X');        // XOR with position zero
    result.push_back(0);
    PositiveVarint(0, &result);   // identifier
    PositiveVarint(3, &result);   // VT_I4
    PositiveVarint((i + generation) % 62u, &result);
  }
  return result;
}

std::vector<uint8_t> Read(const fs::path &file) {
  std::ifstream in(file, std::ios::binary);
  Check(bool(in), "Cannot read synthetic profile");
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void CheckComplete(const fs::path &file, const std::vector<uint8_t> &a,
                   const std::vector<uint8_t> &b) {
  const auto bytes = Read(file);
  Check(bytes == a || bytes == b, "Atomic file is incomplete or torn");
}

size_t PendingFiles(const fs::path &file) {
  const auto prefix = file.filename().string() + ".pending.";
  size_t count = 0;
  for (const auto &entry : fs::directory_iterator(file.parent_path())) {
    if (entry.path().filename().string().compare(0, prefix.size(), prefix) == 0)
      ++count;
  }
  return count;
}

void ExpectFailure(const fs::path &file, const std::vector<uint8_t> &oldData,
                   const std::vector<uint8_t> &newData, atomic::Fault fault,
                   bool publishes, bool leavesOrphan) {
  const auto before = PendingFiles(file);
  std::string error;
  Check(!atomic::Write(file, newData, fault, &error) && !error.empty(),
        "Injected fault unexpectedly succeeded");
  Check(Read(file) == (publishes ? newData : oldData),
        "Injected fault violated atomicity");
  Check(PendingFiles(file) == before + (leavesOrphan ? 1u : 0u),
        "Injected fault left unexpected pending file");
}

void RunPortable() {
  Sandbox sandbox;
  const auto target = sandbox.directory / "GlobalFlag.DAT";
  const auto a = Profile(1), b = Profile(2);
  std::string error;
  Check(!fs::exists(target), "Fresh sandbox unexpectedly has a profile");
  Check(atomic::Write(target, a, atomic::Fault::None, &error),
        "First atomic publication failed");
  Check(Read(target) == a, "First publication changed bytes");
  Check(atomic::Write(target, b, atomic::Fault::None, &error),
        "Atomic replacement failed");
  Check(Read(target) == b, "Replacement changed bytes");
  ExpectFailure(target, b, a, atomic::Fault::Write, false, false);
  ExpectFailure(target, b, a, atomic::Fault::Flush, false, false);
  ExpectFailure(target, b, a, atomic::Fault::Publish, false, false);
  ExpectFailure(target, b, a, atomic::Fault::DirectoryFlushBefore, false, false);
  ExpectFailure(target, b, a, atomic::Fault::SimulatedInterruption, false, true);
  Check(atomic::Write(target, b, atomic::Fault::None, &error),
        "Recovery from simulated crash failed");
  Check(Read(target) == b, "Orphan was accidentally promoted");
  // A second, pre-existing orphan with a verifiably dead PID and an old
  // timestamp can be pruned. A live or recent writer's temp must not be.
  auto staleOrphan = target;
  staleOrphan += ".pending.99999999.1.1";
  {
    std::ofstream orphan(staleOrphan, std::ios::binary);
    orphan << "synthetic-unpublished-temp";
    Check(bool(orphan), "Cannot create synthetic old orphan");
  }
  fs::last_write_time(staleOrphan, fs::file_time_type::clock::now() -
                                      std::chrono::hours(48));
  Check(atomic::Write(target, b, atomic::Fault::None, &error),
        "Recovery with old pending orphan failed");
  Check(!fs::exists(staleOrphan), "Dead-process old orphan was not pruned");
  Check(Read(target) == b, "Prune changed the published profile");
  // Post-publication sync failures must be reported, but cannot restore an old
  // inode because another writer might have committed subsequently.
  ExpectFailure(target, b, a, atomic::Fault::DirectoryFlushAfter, true, false);
  Check(atomic::Write(target, b, atomic::Fault::None, &error),
        "Recovery from post-publication uncertainty failed");
  Check(!atomic::Write(target, {}, atomic::Fault::None, &error),
        "Empty profile unexpectedly published");
  Check(Read(target) == b, "Invalid payload overwrote valid profile");

  // The same production helper is used on both platforms for 1000 complete
  // replacements; no mock fsync/rename and no SDL/vcpkg anywhere in this test.
  for (unsigned i = 0; i < 1000; ++i) {
    Check(atomic::Write(target, i % 2 ? a : b, atomic::Fault::None, &error),
          "1000 sequential writes failed");
    CheckComplete(target, a, b);
  }

#ifdef _WIN32
  HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  Check(locked != INVALID_HANDLE_VALUE, "Cannot open locked destination");
  const bool wroteLocked = atomic::Write(target, a, atomic::Fault::None, &error);
  CloseHandle(locked);
  Check(!wroteLocked, "Writer replaced a locked destination");
  CheckComplete(target, a, b);
  Check(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY) != 0,
        "Cannot set Windows read-only synthetic destination");
  const bool wroteReadonly = atomic::Write(target, a, atomic::Fault::None, &error);
  Check(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL) != 0,
        "Cannot restore Windows destination attributes");
  Check(!wroteReadonly, "Writer replaced read-only Windows destination");
#else
  // A POSIX file's read-only bit does not prevent rename in a writable parent.
  // Test the meaningful protection boundary: deny directory mutation.
  if (::geteuid() != 0) {
    Check(::chmod(sandbox.directory.c_str(), 0500) == 0,
          "Cannot make synthetic directory read-only");
    const bool wroteReadonly = atomic::Write(target, a, atomic::Fault::None, &error);
    Check(::chmod(sandbox.directory.c_str(), 0700) == 0,
          "Cannot restore synthetic directory permissions");
    Check(!wroteReadonly, "Writer bypassed POSIX read-only directory");
    CheckComplete(target, a, b);
  }

  // Crash consistency: the parent kills a real forked writer at randomized
  // intervals and checks that the published target is an entire old/new FlgH.
  std::mt19937 random(0xA70F2026u);
  std::uniform_int_distribution<int> delay(0, 2500);
  unsigned kills = 0;
  for (unsigned i = 0; i < 220; ++i) {
    const pid_t pid = ::fork();
    Check(pid >= 0, "Cannot fork synthetic writer");
    if (pid == 0) {
      std::string childError;
      const bool ok = atomic::Write(target, i % 2 ? a : b,
                                    atomic::Fault::None, &childError);
      ::_exit(ok ? 0 : 3);
    }
    ::usleep(static_cast<useconds_t>(delay(random)));
    if (::kill(pid, SIGKILL) == 0)
      ++kills;
    int status = 0;
    Check(::waitpid(pid, &status, 0) == pid, "Cannot reap synthetic writer");
    CheckComplete(target, a, b);
  }
  Check(kills >= 200, "Fewer than 200 real SIGKILL attempts succeeded");
#endif
  std::cout << "SchoolDaysAtomicStandaloneTest: PASS (synthetic only)\n";
}
} // namespace

int main() {
  try {
    RunPortable();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysAtomicStandaloneTest: " << error.what() << '\n';
    return 2;
  }
}
