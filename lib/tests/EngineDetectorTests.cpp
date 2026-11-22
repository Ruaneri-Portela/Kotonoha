// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/Filesystem/EngineDetector.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;
using Kotonoha::Filesystem::DetectedEngine;
using Kotonoha::Filesystem::detectEngine;
using Kotonoha::Filesystem::EngineKind;
using Kotonoha::Filesystem::Filesystem;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

class TempSource {
public:
  fs::path root;
  explicit TempSource(const std::string &tag)
      : root(fs::temp_directory_path() /
             ("kotonoha-engine-det-" + tag + "-" +
              std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()))) {
    fs::create_directories(root);
  }
  ~TempSource() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  void write(const fs::path &rel, std::string_view content = "fixture") const {
    fs::create_directories((root / rel).parent_path());
    std::ofstream out(root / rel, std::ios::binary);
    if (!out)
      throw std::runtime_error("Cannot create fixture file: " + rel.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  void mkdir(const fs::path &rel) const { fs::create_directories(root / rel); }
  DetectedEngine detect() const {
    auto mounted = Filesystem::mount(root.string());
    return detectEngine(*mounted);
  }
};

} // namespace

int main() {
  try {
    // 1. LucaSystem
    {
      TempSource src("luca");
      src.write("files/PARAM.PAK");
      src.write("files/SCRIPT.PAK");
      src.write("files/SYSSE.PAK");
      const auto det = src.detect();
      require(det.kind == EngineKind::LucaSystem && det.implementedInKotonoha &&
                  det.dataPrefix == "files/",
              "Detect LucaSystem nested in files/");
    }

    // 2. RealLive vs AVG32 disambiguation
    {
      TempSource rl("reallive");
      rl.write("Seen.txt", "KPAC");
      rl.write("Gameexe.ini", "#CAPTION=\"AIR\"");
      rl.mkdir("G00");
      const auto det = rl.detect();
      require(det.kind == EngineKind::RealLive && det.slug == "reallive",
              "Detect RealLive when G00/KPAC present");
    }
    {
      TempSource avg("avg32");
      avg.write("SEEN.TXT", "PACL");
      avg.write("GAMEEXE.INI", "#CAPTION=\"KANON\"");
      avg.mkdir("PDT");
      const auto det = avg.detect();
      require(det.kind == EngineKind::AVG32 && det.slug == "avg32",
              "Disambiguate AVG32 from RealLive via PACL/PDT");
    }

    // 3. SiglusEngine
    {
      TempSource sig("siglus");
      sig.write("Scene.pck");
      sig.write("Gameexe.dat");
      require(sig.detect().kind == EngineKind::Siglus, "Detect SiglusEngine");
    }

    // 4. ONScripter / PONScripter
    {
      TempSource ons("onscripter");
      ons.write("00.utf", ";mode800\n*define\ngame\n*start\n");
      ons.write("arc.nsa");
      require(ons.detect().kind == EngineKind::ONScripter,
              "Detect ONScripter/PONScripter");
    }

    // 5. KiriKiri
    {
      TempSource krkr("kirikiri");
      krkr.write("data.xp3", "XP3\r\n \n\x1a\x8b\x67\x01");
      const auto det = krkr.detect();
      require(det.kind == EngineKind::Kirikiri && det.implementedInKotonoha,
              "Detect KiriKiri XP3");
    }

    // 6. Ren'Py
    {
      TempSource renpy("renpy");
      renpy.write("game/scripts.rpa", "RPA-3.0 0000000000000019 12345678\n");
      renpy.write("game/images.rpa", "RPA-3.0 0000000000000019 12345678\n");
      require(renpy.detect().kind == EngineKind::RenPy, "Detect Ren'Py");
    }

    {
      TempSource generic("renpy-archive");
      generic.write("game/archive.rpa",
                    "RPA-3.0 0000000000000019 12345678\n");
      const auto det = generic.detect();
      require(det.kind == EngineKind::RenPy && det.slug == "renpy",
              "Detect a generic Ren'Py game with archive.rpa");
    }
    {
      TempSource compiled("renpy-module");
      compiled.write("game/definitions.rpymc");
      const auto det = compiled.detect();
      require(det.kind == EngineKind::RenPy && det.slug == "renpy",
              "Detect Ren'Py compiled Python modules");
    }

    // 7. N2System / Nitroplus (nitrosharp)
    {
      TempSource n2("n2");
      n2.write("nss.npa", std::string_view("NPA\x01\x00\x00\x00\x00", 8));
      require(n2.detect().kind == EngineKind::N2System,
              "Detect N2System/Nitroplus NPA");
    }

    // 8. Artemis
    {
      TempSource art("artemis");
      art.write("root.pfs", "pf8\x00\x00\x00\x00");
      require(art.detect().kind == EngineKind::Artemis, "Detect Artemis PFS");
    }

    // 9. CatSystem2
    {
      TempSource cs2("cs2");
      cs2.write("scene.int", std::string_view("KIF\x00\x01\x00\x00\x00", 8));
      require(cs2.detect().kind == EngineKind::CatSystem2,
              "Detect CatSystem2 KIF .int");
    }

    // 10. Ethornell (BGI)
    {
      TempSource bgi("bgi");
      bgi.write("data01000.arc", "BURIKO ARC20\x00\x00\x00\x00");
      require(bgi.detect().kind == EngineKind::EthornellBGI,
              "Detect Ethornell BURIKO ARC20");
    }

    // 11. FVP
    {
      TempSource fvp("fvp");
      fvp.write("Hoshizora.hcb");
      fvp.write("bg.bin");
      require(fvp.detect().kind == EngineKind::FVP, "Detect FVP .hcb + .bin");
    }

    // 12. CMVS
    {
      TempSource cmvs("cmvs");
      cmvs.write("data01.cpz", "CPZ7");
      require(cmvs.detect().kind == EngineKind::CMVS, "Detect CMVS CPZ");
    }

    // 13. AI5
    {
      TempSource ai5("ai5");
      ai5.write("MES.ARC");
      require(ai5.detect().kind == EngineKind::AI5, "Detect AI5 MES.ARC");
    }

    // 14. UK2
    {
      TempSource uk2("uk2");
      uk2.write("concert.ovl");
      require(uk2.detect().kind == EngineKind::UK2, "Detect UK2 .ovl");
    }

    // 15. TyranoScript
    {
      TempSource tyrano("tyrano");
      tyrano.write("resources/app/data/scenario/first.ks", "*start");
      tyrano.mkdir("resources/app/tyrano");
      require(tyrano.detect().kind == EngineKind::TyranoScript,
              "Detect TyranoScript Electron layout");
    }

    // 16. RPG Maker (MV/MZ & RGSS)
    {
      TempSource rmmv("rmmv");
      rmmv.write("www/data/System.json", "{}");
      rmmv.write("www/package.json", "{}");
      require(rmmv.detect().kind == EngineKind::RPGMaker,
              "Detect RPG Maker MV/MZ");

      TempSource rgss("rgss");
      rgss.write("Game.rgss3a", "RGSSAD\x00\x03");
      require(rgss.detect().kind == EngineKind::RPGMaker,
              "Detect RPG Maker VX Ace RGSS3A");
    }

    // 17. Unrelated empty folder returns Unknown
    {
      TempSource empty("empty");
      empty.write("readme.txt", "hello");
      require(empty.detect().kind == EngineKind::Unknown,
              "Reject non-game folder");
    }

    std::cout << "All 17 Kotonoha engine signature tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
