// Retail-gated evidence test for KiriKiri/KirikiriZ XP3 protection.
// Assets are read-only. No protected data is decoded or redistributed.
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/PsbFile.hpp>
#include <Kirikiri/ScnRunner.hpp>
#include <Kirikiri/TjsBootstrap.hpp>
#include <Kirikiri/TjsStartupVm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

std::uint64_t readLe64(const std::uint8_t *p) {
  std::uint64_t value = 0;
  for (int i = 7; i >= 0; --i)
    value = (value << 8) | p[i];
  return value;
}

// Errors that mean the XP3 index/payload cannot be interpreted by the
// standard clean-room format parser (encryption or a commercial filter).
bool protectedXp3Error(const std::string &message) {
  static const char *const markers[] = {
      "XP3 index exceeds size limit",
      "XP3 unsupported index flags",
      "XP3 invalid or cyclic index chain",
      "XP3 index chain limit exceeded",
      "XP3 truncated index continuation",
      "XP3 truncated File chunk",
      "XP3 truncated sub-chunk",
      "XP3 truncated info",
      "XP3 invalid segment array",
      "XP3 invalid segment range",
      "XP3 zlib decoded size mismatch",
      "Truncated or stalled XP3 zlib stream",
      "zlib inflate failed",
      "XP3 unsupported segment method",
      "XP3 unknown segment method",
  };
  return std::any_of(std::begin(markers), std::end(markers),
                     [&](const char *marker) {
                       return message.find(marker) != std::string::npos;
                     });
}

// Compiled TJS2 stores its literals as UTF-16LE. Recover printable runs so a
// protected archive's embedded diagnostic can be quoted verbatim.
std::string findUtf16Diagnostic(const std::vector<std::uint8_t> &bytes) {
  std::string run;
  for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
    const std::uint16_t unit =
        static_cast<std::uint16_t>(bytes[i] | (bytes[i + 1] << 8));
    if (unit >= 0x20 && unit <= 0x7e) {
      run.push_back(static_cast<char>(unit));
      continue;
    }
    if ((run.find("corrupt") != std::string::npos ||
         run.find("failed") != std::string::npos) &&
        run.size() > 8)
      return run;
    run.clear();
  }
  if (run.find("corrupt") != std::string::npos ||
      run.find("failed") != std::string::npos)
    return run;
  return {};
}

void printHeaderEvidence(const std::filesystem::path &directory) {
  const auto path = directory / "data.xp3";
  std::ifstream in(path, std::ios::binary);
  std::array<std::uint8_t, 20> head{};
  if (!in.read(reinterpret_cast<char *>(head.data()), head.size()))
    return;
  const auto indexOffset = readLe64(head.data() + 11);
  std::array<std::uint8_t, 9> meta{};
  std::ifstream metaIn(path, std::ios::binary);
  if (!metaIn.seekg(static_cast<std::streamoff>(indexOffset)) ||
      !metaIn.read(reinterpret_cast<char *>(meta.data()), meta.size()))
    return;
  std::printf("evidence data.xp3: magic=XP3 index_offset=0x%llx "
              "flags=0x%02x stored_size=0x%llx\n",
              static_cast<unsigned long long>(indexOffset), meta[0],
              static_cast<unsigned long long>(readLe64(meta.data() + 1)));

  std::ifstream sig(directory / "data.xp3.sig", std::ios::binary);
  std::string sigLine;
  if (std::getline(sig, sigLine)) {
    if (!sigLine.empty() && sigLine.back() == '\r')
      sigLine.pop_back();
    std::printf("evidence data.xp3.sig: %s\n", sigLine.c_str());
  }
}

// Mount the shipped XP3 stack and try to resolve the same startup the runtime
// uses (TJS2 bootstrap -> KAG entry -> scenario bytes). A protected archive
// is an expected, explicitly reported skip, never a pass.
int checkXp3(const std::filesystem::path &directory) {
  for (const char *name : {"data.xp3", "patch.xp3", "data.xp3.sig"}) {
    if (!std::filesystem::is_regular_file(directory / name)) {
      std::fprintf(stderr, "[SKIP] retail file not present: %s\n", name);
      return kSkip;
    }
  }
  printHeaderEvidence(directory);

  Kotonoha::Providers::KirikiriAssets assets(directory);
  std::vector<std::string> errors;
  for (const char *name : {"data.xp3", "patch.xp3"}) {
    try {
      assets.mountXp3(name);
    } catch (const std::exception &error) {
      errors.emplace_back(std::string(name) + ": " + error.what());
    }
  }
  if (std::any_of(errors.begin(), errors.end(), protectedXp3Error)) {
    for (const auto &error : errors)
      std::fprintf(stderr, "[SKIP] protected XP3: %s\n", error.c_str());
    return kSkip;
  }
  if (!errors.empty()) {
    for (const auto &error : errors)
      std::fprintf(stderr, "FAIL: XP3 mount error: %s\n", error.c_str());
    return 1;
  }

  const auto index = assets.xp3IndexReport();
  const Kotonoha::Providers::KirikiriAssets loose(directory);
  std::size_t xp3Only = 0;
  for (const auto &id : assets.listAsset(""))
    if (!loose.containsAsset(id))
      ++xp3Only;
  const auto scenarios = assets.scenarioFiles();
  std::size_t xp3Scenarios = 0;
  for (const auto &id : scenarios)
    if (!loose.containsAsset(id))
      ++xp3Scenarios;
  std::printf("XP3 index: %zu archives, %zu entries (%zu protected), "
              "%zu XP3-only ids, %zu scenarios (%zu XP3-only)\n",
              index.archives, index.entries, index.protectedEntries, xp3Only,
              scenarios.size(), xp3Scenarios);

  // Probe the archive's own startup, not the loose translation overlay. The
  // protected retail package stores compiled TJS2 bytecode with an embedded
  // "corrupted execution file" diagnostic instead of a runnable bootstrap.
  std::string startupDiagnostic;
  try {
    const auto bytes = assets.read("startup.tjs");
    const auto strings = Kotonoha::Providers::extractTjs2CompiledStrings(bytes);
    const bool compiled =
        bytes.size() >= 7 &&
        std::string(bytes.begin(), bytes.begin() + 7) == "TJS2100";
    std::printf("XP3 startup.tjs: %zu bytes, compiled=%d, strings=%zu\n",
                bytes.size(), compiled ? 1 : 0, strings.size());
    startupDiagnostic = findUtf16Diagnostic(bytes);
    if (startupDiagnostic.empty())
      for (const auto &text : strings)
        if (text.find("corrupt") != std::string::npos ||
            text.find("failed") != std::string::npos)
          startupDiagnostic = text;
  } catch (const std::exception &error) {
    const std::string message = error.what();
    if (protectedXp3Error(message)) {
      std::fprintf(stderr, "[SKIP] protected XP3: %s\n", message.c_str());
      return kSkip;
    }
    std::fprintf(stderr, "FAIL: XP3 startup.tjs: %s\n", message.c_str());
    return 1;
  }
  const auto bootstrap = Kotonoha::Providers::inspectTjsBootstrap(assets);
  const auto run = Kotonoha::Providers::runTjsStartupSubset(assets);
  std::printf("TJS2 subset: entry='%s' completed=%d kag='%s' error='%s'\n",
              bootstrap.entry.c_str(), run.subsetCompleted ? 1 : 0,
              run.kagEntry.c_str(), run.error.c_str());

  if (index.protectedEntries > 0) {
    std::fprintf(stderr,
                 "[SKIP] protected XP3: %zu/%zu entries carry info flag "
                 "0x80000000 (encrypted payload); startup error: %s\n",
                 index.protectedEntries, index.entries,
                 startupDiagnostic.empty() ? "no runnable TJS2 bootstrap"
                                           : startupDiagnostic.c_str());
    return kSkip;
  }
  if (scenarios.empty()) {
    std::fprintf(stderr, "FAIL: no scenario found in mounted XP3 stack\n");
    return 1;
  }
  try {
    const std::string startup =
        !run.kagEntry.empty() ? run.kagEntry : assets.startupScenario();
    if (startup.empty()) {
      std::fprintf(stderr, "FAIL: no KAG startup resolved (TJS entry '%s')\n",
                   bootstrap.entry.c_str());
      return 1;
    }
    const auto bytes = assets.read(startup);
    const auto text = assets.loadTextUtf8(startup);
    if (bytes.empty() || text.empty()) {
      std::fprintf(stderr, "FAIL: startup '%s' decoded to no text\n",
                   startup.c_str());
      return 1;
    }
    std::printf("PASS: TJS entry='%s' startup='%s' bytes=%zu text=%zu\n",
                bootstrap.entry.c_str(), startup.c_str(), bytes.size(),
                text.size());
    return 0;
  } catch (const std::exception &error) {
    const std::string message = error.what();
    if (protectedXp3Error(message)) {
      std::fprintf(stderr, "[SKIP] protected XP3: %s\n", message.c_str());
      return kSkip;
    }
    std::fprintf(stderr, "FAIL: startup read: %s\n", message.c_str());
    return 1;
  }
}

// Diagnostic-only mode used to collect evidence about archive payloads.
int probeXp3(const std::filesystem::path &directory) {
  Kotonoha::Providers::KirikiriAssets assets(directory);
  assets.mountXp3("data.xp3");
  assets.mountXp3("patch.xp3");
  const auto ids = assets.listAsset("");
  std::printf("total ids: %zu\n", ids.size());
  const auto isHashName = [](const std::string &name) {
    if (name.size() != 32)
      return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
  };
  for (const auto &id : ids) {
    if (isHashName(id))
      continue;
    std::printf("named: %s\n", id.c_str());
  }
  try {
    const auto boot = Kotonoha::Providers::inspectTjsBootstrap(assets);
    std::printf("bootstrap entry='%s' static=%zu error='%s'\n",
                boot.entry.c_str(), boot.staticallyReferenced.size(),
                boot.error.c_str());
    const auto text = assets.loadTextUtf8("startup.tjs");
    std::printf(
        "startup.tjs bytes=%zu first='%s'\n", text.size(),
        text.substr(0, std::min<std::size_t>(120, text.size())).c_str());
  } catch (const std::exception &error) {
    std::printf("startup.tjs ERROR %s\n", error.what());
  }
  for (std::size_t i = 0; i < std::min<std::size_t>(60, ids.size()); ++i) {
    const auto &id = ids[i];
    try {
      const auto bytes = assets.read(id);
      std::string name = id;
      if (name.size() > 46)
        name = name.substr(0, 46) + "...";
      std::printf("%2zu %-49s %9zu", i, name.c_str(), bytes.size());
      for (std::size_t j = 0; j < std::min<std::size_t>(8, bytes.size()); ++j)
        std::printf(" %02x", bytes[j]);
      std::printf("\n");
    } catch (const std::exception &error) {
      std::printf("%2zu %s ERROR %s\n", i, id.c_str(), error.what());
    }
  }
  return 0;
}

// The unencrypted VNX+ translation overlay is independent of the protected
// XP3s. Verify the real loose scripts actually load, rather than assuming
// parity from synthetic fixtures alone.
int checkVnx(const std::filesystem::path &directory) {
  Kotonoha::Providers::KirikiriAssets assets(directory);
  int failures = 0;
  const char *const required[] = {"VNX+/sysmenu.ks", "VNX+/scn/scenelist.scn",
                                  "VNX+/scn/00_01.ks.scn"};
  for (const char *id : required) {
    if (!assets.containsAsset(id)) {
      std::fprintf(stderr, "[SKIP] VNX+ loose asset not present: %s\n", id);
      return kSkip;
    }
  }

  std::size_t chapters = 0;
  std::size_t scnFiles = 0;
  for (const auto &file : assets.scenarioFiles()) {
    const auto name = std::filesystem::path(file).filename().string();
    if (name.size() > 3 && name.substr(name.size() - 4) == ".scn")
      ++scnFiles;
    if (name.size() >= 8 && name.rfind("00_", 0) == 0 &&
        name.substr(name.size() - 4) == ".scn")
      ++chapters;
  }
  std::printf("VNX+ loose scenarios: %zu (.scn %zu, numbered chapters %zu)\n",
              assets.scenarioFiles().size(), scnFiles, chapters);
  if (chapters < 15) {
    std::fprintf(stderr, "FAIL: expected 15 numbered VNX+ chapters, got %zu\n",
                 chapters);
    ++failures;
  }

  try {
    const auto menu = assets.loadTextUtf8("VNX+/sysmenu.ks");
    std::printf("VNX+ sysmenu.ks: %zu UTF-8 bytes\n", menu.size());
    if (menu.empty()) {
      std::fprintf(stderr, "FAIL: VNX+ sysmenu.ks decoded empty\n");
      ++failures;
    }
    const auto listBytes = assets.read("VNX+/scn/scenelist.scn");
    const auto list = Kotonoha::Providers::PsbFile::parse(listBytes);
    std::size_t listed = 0;
    for (const auto &key : {"scenelist", "list"}) {
      const auto &value = list.root()[key];
      if (value.isList())
        listed += value.size();
    }
    std::printf("VNX+ scenelist.scn entries: %zu\n", listed);
    if (listed == 0) {
      std::fprintf(stderr, "FAIL: VNX+ scenelist.scn has no chapter list\n");
      ++failures;
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: VNX+ script decode: %s\n", error.what());
    ++failures;
  }

  try {
    const auto psb = Kotonoha::Providers::PsbFile::parse(
        assets.read("VNX+/scn/00_01.ks.scn"));
    std::string firstDialogue;
    Kirikiri::ScnRunner::Callbacks callbacks;
    callbacks.showDialogue = [&](std::string, std::string text, std::string,
                                 std::vector<std::string>) {
      if (firstDialogue.empty())
        firstDialogue = std::move(text);
    };
    Kirikiri::ScnRunner runner(psb, callbacks, 2);
    runner.run();
    if (firstDialogue.empty() || !runner.isBlocked()) {
      std::fprintf(stderr,
                   "FAIL: chapter 00_01 produced no blocked PT-BR dialogue\n");
      ++failures;
    } else {
      std::printf("PASS: chapter 00_01 PT-BR dialogue: %zu bytes\n",
                  firstDialogue.size());
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: chapter 00_01 ScnRunner: %s\n", error.what());
    ++failures;
  }

  return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  std::string mode = "--xp3";
  std::string directory;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--xp3" || arg == "--vnx" || arg == "--probe")
      mode = arg;
    else
      directory = arg;
  }
  if (directory.empty()) {
    if (const char *env = std::getenv("KIRIKIRI_TEST_DATA"))
      directory = env;
  }
  if (directory.empty() || !std::filesystem::is_directory(directory)) {
    std::fprintf(stderr, "[SKIP] KIRIKIRI_TEST_DATA not provided or invalid\n");
    return kSkip;
  }
  if (mode == "--vnx")
    return checkVnx(directory);
  if (mode == "--probe")
    return probeXp3(directory);
  return checkXp3(directory);
}
