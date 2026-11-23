// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Kotonoha::Filesystem {

// Canonical engine identifiers across Kotonoha ports and supported visual novel
// engine families.
enum class EngineKind {
  Unknown,
  // Reference-backed & active Kotonoha ports
  LucaSystem,
  RealLive,
  Kirikiri,
  RenPy,
  N2System,
  Amagami,
  SchoolDays,
  PureMail,
  SnowRadish,
  SummerRadish,
  ZeroNoTsukaima,
  // Additional multi-engine catalog families
  ONScripter,
  AVG32,
  Siglus,
  Artemis,
  CatSystem2,
  EthornellBGI,
  FVP,
  CMVS,
  AI5,
  UK2,
  TyranoScript,
  RPGMaker,
};

struct DetectedEngine {
  EngineKind kind = EngineKind::Unknown;
  std::string slug;        // Kotonoha target slug when implemented
  std::string displayName; // Human-readable engine family name
  std::string dataPrefix;  // Subdirectory inside VFS (e.g. "", "files/")
  std::string evidence;    // Matched signature or magic header
  bool implementedInKotonoha = false;
};

namespace Detail {

inline std::string toLowerAscii(std::string_view input) {
  std::string out(input);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

inline std::string filenameLower(std::string_view path) {
  const auto slash = path.find_last_of("/\\");
  const auto base =
      slash == std::string_view::npos ? path : path.substr(slash + 1);
  return toLowerAscii(base);
}

inline bool endsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool startsWithBytes(const Filesystem &source, std::string_view path,
                            std::string_view magic) {
  if (magic.empty() || !source.exists(path))
    return false;
  try {
    if (source.stat(path).type != FileType::Regular)
      return false;
    auto handle = source.open(path);
    if (!handle)
      return false;
    std::vector<char> buffer(magic.size(), 0);
    const auto got = handle->read(buffer.data(), buffer.size());
    return got == static_cast<std::int64_t>(magic.size()) &&
           std::memcmp(buffer.data(), magic.data(), magic.size()) == 0;
  } catch (...) {
    return false;
  }
}

inline bool hasRegularFile(const Filesystem &source, std::string_view path) {
  try {
    return source.exists(path) && source.stat(path).type == FileType::Regular;
  } catch (...) {
    return false;
  }
}

inline bool hasDirectory(const Filesystem &source, std::string_view path) {
  try {
    return source.exists(path) && source.stat(path).type == FileType::Directory;
  } catch (...) {
    return false;
  }
}

inline std::vector<FileInfo> safeList(const Filesystem &source,
                                      std::string_view dir = {}) {
  try {
    return source.list(dir);
  } catch (...) {
    return {};
  }
}

} // namespace Detail

inline std::string_view engineKindSlug(EngineKind kind) noexcept {
  switch (kind) {
  case EngineKind::LucaSystem:
    return "luca-system";
  case EngineKind::RealLive:
    return "reallive";
  case EngineKind::Kirikiri:
    return "kirikiri";
  case EngineKind::RenPy:
    return "doki-doki-literature-club";
  case EngineKind::N2System:
    return "n2system";
  case EngineKind::Amagami:
    return "amagami";
  case EngineKind::SchoolDays:
    return "school-days";
  case EngineKind::PureMail:
    return "pmail";
  case EngineKind::SnowRadish:
    return "snow-radish-vacation";
  case EngineKind::SummerRadish:
    return "summer-radish-vacation";
  case EngineKind::ZeroNoTsukaima:
    return "zero-no-tsukaima";
  case EngineKind::ONScripter:
    return "onscripter";
  case EngineKind::AVG32:
    return "avg32";
  case EngineKind::Siglus:
    return "siglus";
  case EngineKind::Artemis:
    return "artemis";
  case EngineKind::CatSystem2:
    return "catsystem2";
  case EngineKind::EthornellBGI:
    return "ethornell";
  case EngineKind::FVP:
    return "fvp";
  case EngineKind::CMVS:
    return "cmvs";
  case EngineKind::AI5:
    return "ai5";
  case EngineKind::UK2:
    return "uk2";
  case EngineKind::TyranoScript:
    return "tyranoscript";
  case EngineKind::RPGMaker:
    return "rpgmaker";
  case EngineKind::Unknown:
    break;
  }
  return "unknown";
}

// Inspect a mounted read-only VFS (directory or ISO image) and identify its
// visual novel engine without loading full archives into memory.
inline DetectedEngine detectEngine(const Filesystem &source) {
  using namespace Detail;

  // 1. LucaSystem (PARAM.PAK + SCRIPT.PAK + SYSSE.PAK)
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"PARAM.PAK", "SCRIPT.PAK", "SYSSE.PAK"}, {"", "files/"})) {
    return {EngineKind::LucaSystem, "luca-system", "LucaSystem", *prefix,
            *prefix + "SCRIPT.PAK", true};
  }

  // 2. SiglusEngine (Scene.pck + Gameexe.dat)
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"Scene.pck", "Gameexe.dat"}, {"", "files/"})) {
    return {EngineKind::Siglus, "siglus", "SiglusEngine", *prefix,
            *prefix + "Scene.pck", false};
  }

  // 3. RealLive vs AVG32 (Seen.txt + Gameexe.ini)
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"Seen.txt", "Gameexe.ini"}, {"", "kineticdata/"})) {
    const std::string seenPath = *prefix + "Seen.txt";
    const bool isAvg32Pacl = startsWithBytes(source, seenPath, "PACL");
    const bool hasAvg32Exe = hasRegularFile(source, *prefix + "avg32.exe") ||
                             hasRegularFile(source, *prefix + "avg3216d.exe") ||
                             hasRegularFile(source, *prefix + "avg3217d.exe");
    const bool hasPdtOnly = hasDirectory(source, *prefix + "PDT") &&
                            !hasDirectory(source, *prefix + "G00") &&
                            !hasRegularFile(source, *prefix + "RealLive.exe");
    if (isAvg32Pacl || hasAvg32Exe || hasPdtOnly) {
      return {EngineKind::AVG32, "avg32", "AVG32", *prefix, seenPath, false};
    }
    return {EngineKind::RealLive, "reallive", "RealLive", *prefix, seenPath,
            true};
  }

  // 4. Ren'Py (scripts.rpa / archive.rpa / renpy runtime)
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"scripts.rpa", "images.rpa"}, {"game/", ""})) {
    return {EngineKind::RenPy, "renpy", "Ren'Py", *prefix,
            *prefix + "scripts.rpa", true};
  }

  // 5. TyranoScript (data/scenario/first.ks + tyrano/)
  for (const std::string_view prefix :
       {"", "resources/app/", "www/", "package.nw/"}) {
    const std::string ks = std::string(prefix) + "data/scenario/first.ks";
    if (hasRegularFile(source, ks) &&
        hasDirectory(source, std::string(prefix) + "tyrano")) {
      return {EngineKind::TyranoScript, "tyranoscript", "TyranoScript",
              std::string(prefix), ks, false};
    }
  }

  // 6. RPG Maker (MV/MZ System.json or XP/VX/VXAce RGSS archives/rxdata)
  for (const std::string_view prefix : {"", "www/", "resources/app/www/"}) {
    const std::string sysJson = std::string(prefix) + "data/System.json";
    const std::string pkgJson = std::string(prefix) + "package.json";
    if (hasRegularFile(source, sysJson) &&
        (hasRegularFile(source, pkgJson) ||
         hasDirectory(source, std::string(prefix) + "js"))) {
      return {EngineKind::RPGMaker, "rpgmaker", "RPG Maker (MV/MZ)",
              std::string(prefix), sysJson, false};
    }
  }
  for (const std::string_view rgss :
       {"Game.rgssad", "Game.rgss2a", "Game.rgss3a", "Data/System.rxdata",
        "Data/System.rvdata", "Data/System.rvdata2"}) {
    if (hasRegularFile(source, rgss)) {
      return {EngineKind::RPGMaker, "rpgmaker", "RPG Maker (RGSS)", "",
              std::string(rgss), false};
    }
  }

  // 7. Pure Mail (SCRIPT.DAT + GRAPHIC.DAT)
  if (const auto prefix = findRequiredFilesPrefix(
          source, {"SCRIPT.DAT", "GRAPHIC.DAT"}, {"", "pack/", "data/pack/"})) {
    return {EngineKind::PureMail, "pmail", "Pure Mail", *prefix,
            *prefix + "SCRIPT.DAT", true};
  }

  // 8. ONScripter / NScripter / PONScripter
  for (const std::string_view marker :
       {"nscript.dat", "nscript.___", "0.txt", "00.txt", "0.utf", "00.utf",
        "pscript.dat", "arc.nsa", "arc.sar"}) {
    if (hasRegularFile(source, marker)) {
      return {EngineKind::ONScripter, "ponscripter", "PONScripter / ONScripter",
              "", std::string(marker), true};
    }
  }

  // HTTP(S) mounts without a manifest cannot list undiscovered files.
  // Probe canonical Kirikiri entrypoints before falling back to enumeration;
  // this also preserves the same detector behavior for directory/ISO mounts.
  for (const char *id :
       {"data.xp3", "patch.xp3", "startup.tjs", "startup.ks",
        "scenario/startup.ks", "scenario/first.ks", "first.ks",
        "data/data.xp3"}) {
    if (hasRegularFile(source, id)) {
      return {EngineKind::Kirikiri, "kirikiri", "KiriKiri2 / KiriKiriZ",
              "", id, true};
    }
  }

  // 9. Scan top-level and common subdirectories for archive extensions & magic
  for (const std::string_view scanDir :
       {"", "game", "data", "archive", "Packs", "USRDIR"}) {
    const auto entries = safeList(source, scanDir);
    if (entries.empty())
      continue;
    const std::string prefix =
        scanDir.empty() ? std::string() : (std::string(scanDir) + "/");

    bool hasFvpBin = false;
    std::string fvpHcb;
    bool hasAi5Arc = false;
    bool hasAi5Mes = false;

    for (const auto &entry : entries) {
      if (entry.type != FileType::Regular)
        continue;
      const std::string fname = filenameLower(entry.path);

      // KiriKiri (.xp3 or startup.tjs)
      if (endsWith(fname, ".xp3") || fname == "startup.tjs") {
        return {EngineKind::Kirikiri, "kirikiri", "KiriKiri2 / KiriKiriZ",
                prefix, entry.path, true};
      }
      // Archives and compiled scripts (including compiled Python modules).
      if (endsWith(fname, ".rpa") || endsWith(fname, ".rpx") ||
          endsWith(fname, ".rpyc") || endsWith(fname, ".rpymc")) {
        return {EngineKind::RenPy, "renpy", "Ren'Py",
                prefix, entry.path, true};
      }
      // N2System / Nitroplus (.npa / .npk)
      if (endsWith(fname, ".npa") || endsWith(fname, ".npk") ||
          startsWithBytes(source, entry.path, "NPA\x01")) {
        return {EngineKind::N2System, "n2system", "N2System / Nitroplus",
                prefix, entry.path, true};
      }
      // Artemis (.pfs / *.pfs.000 / pf8 / pf6)
      if (endsWith(fname, ".pfs") || fname.find(".pfs.") != std::string::npos ||
          startsWithBytes(source, entry.path, "pf8") ||
          startsWithBytes(source, entry.path, "pf6")) {
        return {EngineKind::Artemis, "artemis", "Artemis Engine", prefix,
                entry.path, true};
      }
      // CatSystem2 (.int with KIF\0 header)
      if (endsWith(fname, ".int") &&
          startsWithBytes(source, entry.path, std::string_view("KIF\0", 4))) {
        return {EngineKind::CatSystem2, "catsystem2", "CatSystem2", prefix,
                entry.path, false};
      }
      // CMVS (.cpz with CPZ header)
      if (endsWith(fname, ".cpz") ||
          startsWithBytes(source, entry.path, "CPZ")) {
        return {EngineKind::CMVS, "cmvs", "CMVS", prefix, entry.path, false};
      }
      // Ethornell / BGI (PackFile    or BURIKO ARC20)
      if (endsWith(fname, ".arc") &&
          (startsWithBytes(source, entry.path, "PackFile    ") ||
           startsWithBytes(source, entry.path, "BURIKO ARC20"))) {
        return {EngineKind::EthornellBGI, "ethornell", "Ethornell (BGI)",
                prefix, entry.path, false};
      }
      // School Days (.gpk)
      if (endsWith(fname, ".gpk")) {
        return {EngineKind::SchoolDays, "school-days", "Overflow GPK", prefix,
                entry.path, true};
      }
      // FVP (.hcb + .bin)
      if (endsWith(fname, ".hcb"))
        fvpHcb = entry.path;
      if (endsWith(fname, ".bin"))
        hasFvpBin = true;

      // AI5 (mes.arc / .mes / ai5win.exe)
      if (fname == "mes.arc" || fname == "ai5win.exe")
        hasAi5Arc = true;
      if (endsWith(fname, ".mes"))
        hasAi5Mes = true;

      // UK2 (.ovl + .dat or uk2.exe / concert.ovl)
      if (endsWith(fname, ".ovl") || fname == "uk2.exe") {
        return {EngineKind::UK2, "uk2", "UK2", prefix, entry.path, false};
      }

      // PS2 / Vita disc identifiers (Amagami & Zero no Tsukaima)
      if (fname.rfind("slpm_551", 0) == 0 || fname.rfind("slps_259", 0) == 0 ||
          fname.rfind("uljs_003", 0) == 0 || fname == "amagami.scf") {
        return {EngineKind::Amagami, "amagami", "Amagami (SCF)", prefix,
                entry.path, true};
      }
      if (fname.rfind("slps_257", 0) == 0 || fname.rfind("slps_258", 0) == 0 ||
          fname.rfind("slpm_666", 0) == 0 || fname.rfind("slpm_550", 0) == 0) {
        return {EngineKind::ZeroNoTsukaima, "zero-no-tsukaima",
                "Zero no Tsukaima", prefix, entry.path, true};
      }
    }

    if (!fvpHcb.empty() && (hasFvpBin || scanDir.empty())) {
      return {EngineKind::FVP, "fvp", "FVP (Favorite View Point)", prefix,
              fvpHcb, false};
    }
    if (hasAi5Arc || hasAi5Mes) {
      return {EngineKind::AI5, "ai5", "AI5WIN", prefix, prefix + "MES", false};
    }
  }

  return {};
}

inline DetectedEngine detectEngineFromPath(const std::string &path) {
  auto source = Filesystem::mount(path);
  return source ? detectEngine(*source) : DetectedEngine{};
}

} // namespace Kotonoha::Filesystem
