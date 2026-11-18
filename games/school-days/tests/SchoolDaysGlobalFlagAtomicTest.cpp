#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace {
namespace fs = std::filesystem;
using Storage = Kotonoha::SchoolDaysSaveStorage;

void Require(bool condition, const std::string &what) {
  if (!condition)
    throw std::runtime_error(what);
}

struct Scratch {
  fs::path root;
  Scratch() {
    const auto tick = std::chrono::steady_clock::now()
                          .time_since_epoch().count();
    root = fs::temp_directory_path() /
           ("school-days-globalflag-atomic-" + std::to_string(tick));
    fs::create_directories(root);
  }
  ~Scratch() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

std::vector<uint8_t> Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  Require(bool(input), "Cannot read synthetic GlobalFlag");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> SyntheticFlag(int ending, bool replay) {
  Kotonoha::SchoolDaysFlgH flag;
  Kotonoha::SchoolDaysFlgHEntry finished;
  finished.key = u"EndClear";
  finished.type = 11;
  finished.integer = -1;
  flag.entries.push_back(finished);
  Kotonoha::SchoolDaysFlgHEntry endNo;
  endNo.key = u"EndNo";
  endNo.type = 3;
  endNo.integer = ending;
  flag.entries.push_back(endNo);
  if (replay) {
    Kotonoha::SchoolDaysFlgHEntry rep;
    rep.key = u"REPLAY_SYNTHETIC";
    rep.type = 11;
    rep.integer = -1;
    flag.entries.push_back(rep);
  }
  std::vector<uint8_t> result;
  std::string error;
  Require(Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
              flag, &result, &error),
          "Cannot serialize synthetic FlgH: " + error);
  return result;
}

void CheckFlag(const fs::path &path, const std::vector<uint8_t> &expected) {
  Require(Read(path) == expected,
          "GlobalFlag bytes differ from original FlgH serializer output");
  Kotonoha::SchoolDaysFlgH parsed;
  std::string error;
  Require(Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
              Read(path), &parsed, &error),
          "Atomic GlobalFlag is not parseable: " + error);
}

size_t PendingCount(const fs::path &target) {
  size_t count = 0;
  for (const auto &file : fs::directory_iterator(target.parent_path())) {
    if (file.path().filename().wstring().find(
            target.filename().wstring() + L".pending.") == 0)
      ++count;
  }
  return count;
}

void CheckFailure(Storage &storage, const fs::path &target,
                  const std::vector<uint8_t> &previous,
                  const std::vector<uint8_t> &next,
                  Storage::GlobalFlagWriteFault fault, bool keepOrphan) {
  const auto before = PendingCount(target);
  storage.SetGlobalFlagWriteFaultForTesting(fault);
  std::string error;
  Require(!storage.WriteGlobalFlagBytes(next, &error) && !error.empty(),
          "Fault injection did not report write failure");
  CheckFlag(target, previous);
  Require(PendingCount(target) == before + (keepOrphan ? 1u : 0u),
          "Fault injection left or removed an unexpected temp");
  storage.SetGlobalFlagWriteFaultForTesting(
      Storage::GlobalFlagWriteFault::None);
}

void CheckStorage(const fs::path &root) {
  Storage storage;
  Kotonoha::SchoolDaysSaveProfile profile;
  profile.saveFolder = "Save";
  profile.flagFileName = "Save/GlobalFlag.DAT";
  // The retail-style profile encodes the leading zeroes as literal filename
  // characters; FormatIndexed intentionally accepts only one native %d token.
  profile.saveFileName = "Save/SaveFile00%d.DAT";
  std::string error;
  Require(storage.Initialize(root, profile, &error),
          "Cannot initialize synthetic save storage: " + error);
  const auto target = root / "Save" / "GlobalFlag.DAT";
  const auto first = SyntheticFlag(1, false);
  const auto second = SyntheticFlag(2, true);

  std::vector<uint8_t> loaded;
  bool present = true;
  Require(storage.LoadGlobalFlag(&loaded, &present, &error) && !present,
          "Missing GlobalFlag not treated as a fresh profile");
  Require(storage.WriteGlobalFlagBytes(first, &error),
          "Cannot write initial GlobalFlag: " + error);
  CheckFlag(target, first);
  Require(PendingCount(target) == 0, "Initial write left pending file");
  Kotonoha::SchoolDaysSaveService::Plan plan;
  plan.globalFlagPath = profile.flagFileName;
  plan.globalFlagBytes = second;
  Require(storage.WriteGlobalFlag(plan, &error),
          "Slot-independent GlobalFlag write failed: " + error);
  CheckFlag(target, second);
  Require(storage.LoadGlobalFlag(&loaded, &present, &error) && present &&
              loaded == second,
          "GlobalFlag load differs after atomic publish");
  Require(!fs::exists(root / "Save" / "SaveFile000.DAT"),
          "GlobalFlag write unexpectedly created a slot save");

  // Preserve upstream's Unicode slot read/probe coverage while using the
  // exact retail-style %d format supported by SchoolDaysSaveProfile.
  const auto slot = root / "Save" / "SaveFile000.DAT";
  const std::string slotText = "synthetic-slot";
  {
    std::ofstream output(slot, std::ios::binary);
    output << slotText;
    Require(bool(output), "Cannot create synthetic slot");
  }
  bool openable = false;
  Require(storage.ProbeSaveFile(0, &openable, &error) && openable,
          "Unicode slot folder was not probed correctly");
  std::vector<uint8_t> slotBytes;
  Require(storage.ReadSaveFile(0, &slotBytes, &error) &&
              slotBytes ==
                  std::vector<uint8_t>(slotText.begin(), slotText.end()),
          "Unicode slot folder was not read correctly");
  Require(fs::remove(slot), "Cannot remove synthetic slot fixture");

  const auto faultWrite = Storage::GlobalFlagWriteFault::Write;
  const auto faultFlush = Storage::GlobalFlagWriteFault::Flush;
  const auto faultPublish = Storage::GlobalFlagWriteFault::Publish;
  CheckFailure(storage, target, second, first, faultWrite, false);
  CheckFailure(storage, target, second, first, faultFlush, false);
  CheckFailure(storage, target, second, first, faultPublish, false);
  CheckFailure(storage, target, second, first,
               Storage::GlobalFlagWriteFault::DirectoryFlushBefore, false);
  CheckFailure(storage, target, second, first,
               Storage::GlobalFlagWriteFault::SimulatedInterruption, true);

  // A simulated crash leaves a valid but unreferenced temp. A subsequent
  // writer must ignore it as a profile, and remove it only when safely old.
  Require(storage.LoadGlobalFlag(&loaded, &present, &error) &&
              present && loaded == second,
          "Interrupted writer exposed orphan temp as GlobalFlag");
  fs::path orphan;
  for (const auto &file : fs::directory_iterator(target.parent_path())) {
    if (file.path().filename().wstring().find(
            target.filename().wstring() + L".pending.") == 0) {
      orphan = file.path();
      break;
    }
  }
  Require(!orphan.empty(), "Synthetic interruption orphan missing");
  // The interrupted process is still alive in this test; its temp must not
  // be scavenged. Simulate an orphan left by a process that has exited.
  fs::remove(orphan);
  orphan = target;
  orphan += ".pending.99999999.1.1";
  {
    std::ofstream out(orphan, std::ios::binary);
    out << "unpublished";
  }
  fs::last_write_time(orphan,
                      fs::file_time_type::clock::now() -
                          std::chrono::hours(48));
  Require(storage.WriteGlobalFlagBytes(first, &error),
          "Cannot recover after orphan: " + error);
  CheckFlag(target, first);
  Require(PendingCount(target) == 0, "Stale orphan was not pruned");

  // A recently created pending file may belong to another active process.
  const auto recent = fs::path(target.wstring() + L".pending.active");
  {
    std::ofstream out(recent, std::ios::binary);
    out << "unpublished";
  }
  Require(storage.WriteGlobalFlagBytes(second, &error),
          "Write with recent pending temp failed: " + error);
  Require(fs::exists(recent) && PendingCount(target) == 1,
          "Recent pending file was deleted without checking its age");
  CheckFlag(target, second);
  fs::remove(recent);

  const std::vector<uint8_t> empty;
  Require(!storage.WriteGlobalFlagBytes(empty, &error) &&
              Read(target) == second,
          "Empty GlobalFlag payload replaced a valid profile");
  const std::vector<uint8_t> enormous(32u * 1024u * 1024u + 1u, 3);
  Require(!storage.WriteGlobalFlagBytes(enormous, &error) &&
              Read(target) == second,
          "Oversized GlobalFlag replaced a valid profile");
  Require(PendingCount(target) == 0, "Size rejection left pending files");

#ifdef _WIN32
  HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  Require(locked != INVALID_HANDLE_VALUE,
          "Could not lock synthetic GlobalFlag for sharing test");
  Require(!storage.WriteGlobalFlagBytes(first, &error) && !error.empty(),
          "Locked destination was replaced without file-delete sharing");
  CloseHandle(locked);
  CheckFlag(target, second);
  Require(PendingCount(target) == 0, "Sharing failure left pending file");

  Require(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY) != 0,
          "Could not set read-only attribute in synthetic profile");
  const bool wroteReadonly = storage.WriteGlobalFlagBytes(first, &error);
  Require(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL) != 0,
          "Could not restore synthetic file attributes");
  Require(!wroteReadonly, "Read-only GlobalFlag destination was overwritten");
  CheckFlag(target, second);
  Require(PendingCount(target) == 0, "Read-only failure left pending file");
#endif

  // After the atomic rename a failed directory durability confirmation
  // reports uncertainty, but must not roll back the newly published FlgH.
  storage.SetGlobalFlagWriteFaultForTesting(
      Storage::GlobalFlagWriteFault::DirectoryFlushAfter);
  Require(!storage.WriteGlobalFlagBytes(first, &error) && !error.empty(),
          "Post-publication uncertainty was not reported");
  CheckFlag(target, first);
  Require(PendingCount(target) == 0,
          "Post-publication error left an unpublished temporary");
  storage.SetGlobalFlagWriteFaultForTesting(
      Storage::GlobalFlagWriteFault::None);
}
} // namespace

int main() {
  try {
    Scratch files;
    CheckStorage(files.root / "regular");
    CheckStorage(files.root / fs::u8path(u8"configuração-segura"));
    std::cout << "SchoolDaysGlobalFlagAtomicTest: PASS (synthetic only)\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "SchoolDaysGlobalFlagAtomicTest: " << exception.what()
              << '\n';
    return 2;
  }
}
