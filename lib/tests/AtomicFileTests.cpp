#include <Kotonoha/Filesystem/AtomicFile.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace atomic = Kotonoha::Filesystem;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::vector<std::uint8_t> read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "Cannot read atomic test file");
  return {std::istreambuf_iterator<char>(input), {}};
}
std::size_t pending(const fs::path &path) {
  std::size_t count = 0;
  const std::string prefix = path.filename().u8string() + ".pending.";
  for (const auto &entry : fs::directory_iterator(path.parent_path())) {
    const auto name = entry.path().filename().u8string();
    if (name.compare(0, prefix.size(), prefix) == 0)
      ++count;
  }
  return count;
}
struct Scratch {
  fs::path dir;
  Scratch() {
    dir = fs::temp_directory_path() /
          ("kotonoha-atomic-test-" +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directories(dir), "Cannot create sandbox");
  }
  ~Scratch() {
    std::error_code ignored;
    fs::remove_all(dir, ignored);
  }
};

void test() {
  Scratch scratch;
  const fs::path path = scratch.dir / fs::u8path(u8"configuração.dat");
  const std::vector<std::uint8_t> first(2048, 0x31);
  const std::vector<std::uint8_t> second(4096, 0x52);
  std::string error;
  check(atomic::writeBinaryFileAtomically(path, first, &error),
        "Cannot publish first UTF-8 destination");
  check(read(path) == first && pending(path) == 0,
        "First publication altered data or left a temporary file");
  check(atomic::writeBinaryFileAtomically(path, second, &error),
        "Cannot replace existing file");
  check(read(path) == second && pending(path) == 0,
        "Replacement changed bytes or left pending file");

  for (auto fault :
       {atomic::AtomicFileFault::Write, atomic::AtomicFileFault::Flush,
        atomic::AtomicFileFault::Publish,
        atomic::AtomicFileFault::DirectoryFlushBefore}) {
    check(!atomic::writeBinaryFileAtomically(path, first, &error, fault) &&
              !error.empty() && read(path) == second && pending(path) == 0,
          "Pre-publication failure must preserve old file and clean own temp");
  }
  check(!atomic::writeBinaryFileAtomically(
            path, first, &error,
            atomic::AtomicFileFault::SimulatedInterruption) &&
            pending(path) == 1 && read(path) == second,
        "Simulated crash must leave unpublished data isolated");
  check(atomic::writeBinaryFileAtomically(path, first, &error) &&
            read(path) == first && pending(path) == 1,
        "Other writers must ignore and preserve unknown pending files");
  for (const auto &entry : fs::directory_iterator(path.parent_path()))
    if (entry.path() != path)
      check(fs::remove(entry.path()), "Cannot remove synthetic orphan");
  check(
      !atomic::writeBinaryFileAtomically(
          path, second, &error, atomic::AtomicFileFault::DirectoryFlushAfter) &&
          !error.empty() && read(path) == second && pending(path) == 0,
      "Post-publication failure cannot roll back a completed rename");

  // Prove that several writers never produce partial published files.
  std::vector<std::thread> threads;
  std::vector<std::string> failures(4);
  for (std::size_t t = 0; t < failures.size(); ++t) {
    threads.emplace_back([&, t] {
      const auto &bytes = t % 2 ? first : second;
      for (int i = 0; i < 30; ++i) {
        std::string message;
        if (!atomic::writeBinaryFileAtomically(path, bytes, &message)) {
          failures[t] = message;
          return;
        }
      }
    });
  }
  for (auto &thread : threads)
    thread.join();
  for (const auto &failure : failures)
    if (!failure.empty())
      throw std::runtime_error("Concurrent writer failed: " + failure);
  check((read(path) == first || read(path) == second) && pending(path) == 0,
        "Concurrent publishers must leave exactly one complete file");

  check(!atomic::writeBinaryFileAtomically(scratch.dir, first, &error),
        "Atomic file API must reject directory destinations");
  const auto missing = scratch.dir / "missing" / "target.dat";
  check(!atomic::writeBinaryFileAtomically(missing, first, &error),
        "Writer must not create an arbitrary parent directory");
  const auto link = scratch.dir / "symlink.dat";
  std::error_code linkError;
  fs::create_symlink(path, link, linkError);
  if (!linkError)
    check(!atomic::writeBinaryFileAtomically(link, first, &error),
          "Atomic file API must reject a symlink destination");
}
} // namespace

int main() {
  try {
    test();
    std::cout << "KotonohaAtomicFileTests PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "KotonohaAtomicFileTests FAIL: " << e.what() << '\n';
    return 1;
  }
}
