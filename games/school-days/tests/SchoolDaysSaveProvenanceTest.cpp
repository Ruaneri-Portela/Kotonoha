#include <Kotonoha/parsers/Documents.hpp>
#include <Kotonoha/routing/SchoolDaysSaveProvenance.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Provenance = Kotonoha::SchoolDaysSaveProvenance;
namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct Scratch {
  fs::path root = fs::temp_directory_path() /
      ("school-days-sidecar-synthetic-" +
       std::to_string(SDL_GetPerformanceCounter()));
  Scratch() { fs::create_directories(root); }
  ~Scratch() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  fs::path Save(const std::string& profile, const std::string& file) {
    const auto dir = root / profile / "Save";
    fs::create_directories(dir);
    return dir / file;
  }
};

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  out.close();
  Require(bool(out), "Cannot write synthetic SaveFile");
}

void Write(const fs::path& path, const std::string& text) {
  Write(path, std::vector<uint8_t>(text.begin(), text.end()));
}

std::vector<uint8_t> SyntheticSave(const std::u16string& scene) {
  Kotonoha::SchoolDaysSLog records;
  Kotonoha::SchoolDaysSLogRecord record;
  record.tag = 1;
  record.name = scene;
  record.floatBits = 0x3f800000u;
  records.records.push_back(record);
  std::vector<uint8_t> bytes;
  std::string error;
  Require(Kotonoha::SchoolDaysPersistenceCodec::SerializeSLog(
      records, &bytes, &error), "Cannot generate synthetic SLog");
  return bytes;
}

void TestSha256() {
  Require(Provenance::Sha256({}) ==
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "SHA-256 empty-string vector differs from FIPS 180-4");
  const std::vector<uint8_t> abc{'a', 'b', 'c'};
  Require(Provenance::Sha256(abc) ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "SHA-256 abc vector differs from FIPS 180-4");
  // NIST long-message vector spans 15,625 64-byte blocks and exercises
  // the length-field/padding path used by ordinary 20+ KiB SaveFiles.
  const std::vector<uint8_t> millionAs(1000000u, uint8_t('a'));
  Require(Provenance::Sha256(millionAs) ==
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
      "SHA-256 million-a multi-block vector differs from NIST");
}

void TestActualOrsBoundary() {
  for (const std::string ending : {
       "05-A2-F00", "05-A2-Z00", "05-B0-F00", "05-G0-L03",
       "05-KC-F00", "05-SB-E01", "05-SC-L03"}) {
    const std::string script =
        "[SkipFRAME]=00:00:00;\n"
        "[PrintText]=00:00:01\tSekai\tOriginal HQ.\t00:00:10;\n"
        "[EndRoll]=00:01:00\tCredits\t00:02:00;\n"
        "[PlayMovie]=00:02:01\tLxH/Media/" + ending +
        "-LXH\t0\t00:03:00;\n"
        "[PrintText]=00:02:04\tKotonoha\tEpilogue.\t00:02:12;\n"
        "[Next]=00:04:00;\n";
    Kotonoha::OrsDocument ors(
        Kotonoha_OrsParserBytes(script.data(), script.size()));
    Require(ors.get().data, "Synthetic ORS did not parse");
    Kotonoha_orsEvent* movie = nullptr;
    uint32_t start = 0, next = 0;
    for (auto* event = ors.get().data; event; event = event->next) {
      if (event->command == PLAY_MOVIE) {
        movie = event;
        start = event->startTick;
      } else if (event->command == Next) {
        next = event->startTick;
      }
    }
    Require(movie && next > start, "Synthetic graft lacks movie/Next");
    const auto scene = "05/" + ending;
    const auto capture = [&](uint32_t tick, bool temp,
                             bool handoff, bool exiting, bool started) {
      return Provenance::Capture(scene, ors.get(), tick, temp,
                                 handoff, exiting, started).lxhEpilogue;
    };
    Require(!capture(start, false, false, false, true),
            "An untouched movie was incorrectly classified LxH");
    movie->eventTouched = true; // Simulate real SceneTimeline dispatch.
    Require(!capture(start - 1, false, false, false, true),
            "Credits/HQ were incorrectly marked as LxH");
    Require(capture(start, false, false, false, true) &&
            capture(next - 1, false, false, false, true),
            "A real touched epilogue tick was not recognized");
    Require(!capture(next, false, false, false, true),
            "Countdown/Next was incorrectly marked LxH");
    Require(!capture(start, true, false, false, true) &&
            !capture(start, false, true, false, true) &&
            !capture(start, false, false, true, true) &&
            !capture(start, false, false, false, false),
            "Replay/SaveFeel/pending/movie-not-started marked LxH");
    const auto touched = movie->eventTouched;
    (void)Provenance::Capture(scene, ors.get(), start,
                             false, false, false, true);
    Require(movie->eventTouched == touched,
            "Origin capture unexpectedly mutated the ORS timeline");
  }
}

void TestSidecarOverwriteAndProfiles() {
  Scratch temp;
  const auto save = SyntheticSave(u"05/05-A2-F00");
  const auto other = SyntheticSave(u"02/02-A00");
  const auto a = temp.Save("docs-A", "SaveFile000.DAT");
  const auto b = temp.Save("docs-B", "SaveFile000.DAT");
  Write(a, save);
  Require(Provenance::SaveSceneKey(save) == "05/05-A2-F00",
          "Tag-1 SLog SceneKey not decoded");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Untrusted,
          "Legacy commercial save without sidecar must remain untrusted metadata");
  std::string error;
  Provenance::Origin origin{true, 3100u, 2900u, 5000u};
  Require(Provenance::Invalidate(a, &error), "First sidecar invalidation failed");
  Require(Provenance::Publish(a, 0, save, origin, &error),
          "Cannot publish synthetic LxH marker");
  // LxH epilogue provenance remains a trustworthy marker for an integrated
  // scene; it no longer means the save requires enabling a separate DLC.
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::LxhEpilogue,
          "Integrated LxH save lost its authentic epilogue marker");
  Require(Provenance::Check(a, 99, save) == Provenance::Verdict::Untrusted,
          "Slot identity was not checked");
  Require(Provenance::Check(a, 0, other) == Provenance::Verdict::Untrusted,
          "A changed SaveFile still trusted its old LxH marker");

  // Two independent --documents profiles with identical commercial save bytes
  // must not share an origin, even if a sidecar is manually copied.
  Write(b, save);
  fs::copy_file(Provenance::SidecarPath(a), Provenance::SidecarPath(b));
  Require(Provenance::Check(b, 0, save) == Provenance::Verdict::Untrusted,
          "Profile B accepted metadata belonging to --documents profile A");

  // Slot overwrite invalidates OLD origin before any native truncation.
  Require(Provenance::Invalidate(a, &error), "Overwrite invalidation failed");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Untrusted,
          "Interrupted save incorrectly retained earlier LxH attestation");
  // Same SaveFile bytes (runtime SLog has no tick!) are still a NEW HQ save.
  Write(a, save);
  Require(Provenance::Publish(a, 0, save, {}, &error),
          "Cannot publish new HQ origin over old LxH slot");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Normal,
          "HQ overwrite incorrectly inherited old LxH origin");

  // Tampering or malformed/partial sidecars never result in a positive claim.
  Write(Provenance::SidecarPath(a), "malformed\n");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Untrusted,
          "Corrupt sidecar was trusted");
  Require(Provenance::Invalidate(a, &error), "Malformed marker invalidation failed");
  Require(!Provenance::Publish(a, 0, other, origin, &error),
          "Mismatched saved bytes were incorrectly certified");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Untrusted,
          "Failed publish emitted stale sidecar");

  // Interrupted overwrite cannot accidentally preserve an old sidecar if
  // invalidation itself fails: native SaveFile has not been touched yet.
  const auto path = Provenance::SidecarPath(a);
  fs::create_directories(path);
  Write(path / "locked", "content");
  Require(!Provenance::Invalidate(a, &error),
          "Non-removable sidecar did not fail closed");
  Require(Provenance::Check(a, 0, save) == Provenance::Verdict::Untrusted,
          "Unreadable marker was incorrectly trusted");
  Require(Provenance::SaveSceneKey(save) == "05/05-A2-F00",
          "Failed sidecar invalidation touched the synthetic SLog");

  const auto quick = temp.root / "docs-A" / "QuickSave";
  Write(quick, other);
  Require(Provenance::Invalidate(quick, &error), "Quicksave invalidation failed");
  Require(Provenance::Publish(quick, -1, other, {}, &error),
          "Quicksave false-origin marker failed");
  Require(Provenance::Check(quick, -1, other) == Provenance::Verdict::Normal &&
          Provenance::Check(quick, 0, other) == Provenance::Verdict::Untrusted,
          "Slot and direct-file identities leaked into one another");
  // A saved integrated LxH epilogue must retain provenance for both native
  // slot saves and the independent QuickSave file (slot binding -1).
  Write(quick, save);
  Require(Provenance::Invalidate(quick, &error) &&
          Provenance::Publish(quick, -1, save, origin, &error) &&
          Provenance::Check(quick, -1, save) == Provenance::Verdict::LxhEpilogue &&
          Provenance::Check(quick, 0, save) == Provenance::Verdict::Untrusted,
          "Integrated LxH QuickSave provenance path or slot identity failed");
  // A path close to the classic Windows MAX_PATH limit still round-trips.
  // The full binding includes the actual resolved absolute path, not basename.
  const auto longDir = temp.root / "docs-long" /
      std::string(45, 'a') / std::string(45, 'b');
  fs::create_directories(longDir);
  const auto longPath = longDir / "SaveFile99.DAT";
  Write(longPath, other);
  Require(Provenance::Invalidate(longPath, &error) &&
          Provenance::Publish(longPath, 99, other, {}, &error) &&
          Provenance::Check(longPath, 99, other) == Provenance::Verdict::Normal,
          "Long profile path lost binding or sidecar verification");
}

} // namespace

int main(int argc, char** argv) {
  try {
    // Existing test runners may still pass the route fixture as argv[1].
    // Integrated LxH provenance tests need no game executable or GUI fixture.
    (void)argc;
    (void)argv;
    TestSha256();
    TestActualOrsBoundary();
    TestSidecarOverwriteAndProfiles();
    std::cout << "SchoolDaysSaveProvenanceTest PASS: SHA-256, integrated LxH "
                 "event/tick boundaries, legacy, overwrite, corruption, "
                 "--documents isolation, native slot and QuickSave origins\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "SchoolDaysSaveProvenanceTest FAIL: " << ex.what() << '\n';
    return 1;
  }
}
