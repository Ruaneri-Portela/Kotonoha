// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/Archive.hpp>
#include <Ponscripter/PonscripterAssets.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

} // namespace

int main() {
  try {
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          "kotonoha_ponscr_provider_test";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot / "cg" / "menu");
    std::filesystem::create_directories(tempRoot / "voice" / "12");

    {
      std::ofstream f00(tempRoot / "00.utf", std::ios::binary);
      f00 << "\xEF\xBB\xBF;mode800\n*define\ngame\n*start\nend\n";
    }
    {
      std::ofstream img(tempRoot / "cg" / "menu" / "title01.png",
                        std::ios::binary);
      img << "PNGDATA";
    }
    {
      std::ofstream vox(tempRoot / "voice" / "12" / "001400.ogg",
                        std::ios::binary);
      vox << "OGGVOICE";
    }

    Ponscripter::PonscripterAssets assets(tempRoot.string());

    // 1. Script discovery
    const auto scripts = assets.discoverScripts();
    check(scripts.size() == 1 && scripts[0].first == "00.utf",
          "discoverScripts must find 00.utf");

    // 2. Case-insensitive and backslash path resolution
    check(assets.contains("cg\\MENU\\TITLE01.PNG"),
          "Case-insensitive cg\\MENU\\TITLE01.PNG lookup must succeed");

    // 3. SayaNoUta_PT-BR sound/<dir>/<clip>.ogg -> voice/<dir>/<clip>.ogg
    // fallback
    check(assets.contains("sound\\12\\001400.ogg"),
          "sound\\12\\001400.ogg must fall back to voice/12/001400.ogg");
    const auto voxBytes = assets.read("sound\\12\\001400.ogg");
    check(std::string(voxBytes.begin(), voxBytes.end()) == "OGGVOICE",
          "Fallback voice bytes must match");

    // 4. Mounted NSA archive lookup
    const auto nsaBytes = Ponscripter::ArchiveReader::buildSyntheticNsa(
        {{"data\\cur00.png", {'C', 'U', 'R'}}});
    assets.mountArchive(nsaBytes, Ponscripter::ArchiveFormat::NSA);
    check(assets.contains("data\\cur00.png"),
          "Mounted NSA asset must be found");

    std::filesystem::remove_all(tempRoot);
    std::cout << "Ponscripter.Archives passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Ponscripter.Archives failed: " << e.what() << '\n';
    return 1;
  }
}
