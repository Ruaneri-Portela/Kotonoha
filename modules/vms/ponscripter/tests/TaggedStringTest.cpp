// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/Archive.hpp>
#include <Ponscripter/PonscripterTypes.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool cond, const char *msg) {
  if (!cond)
    throw std::runtime_error(msg);
}

} // namespace

int main() {
  try {
    // 1. Ping-pong 5-cell title glow tag
    {
      const auto spec =
          Ponscripter::parseTaggedString(":c/5,400,2;data\\start.png");
      check(spec.trans == Ponscripter::TransMode::Copy, ":c must be Copy");
      check(spec.numCells == 5, "numCells must be 5");
      check(spec.durationMs == 400, "durationMs must be 400");
      check(spec.loopMode == 2, "loopMode must be 2 (ping-pong)");
      check(spec.imagePath == "data/start.png",
            "Backslashes must normalize to forward slashes");
    }

    // 2. Manual button cell tag (:l/2,0,3 and :a/3,0,3)
    {
      const auto spec =
          Ponscripter::parseTaggedString(":l/2,0,3;data\\startover.png");
      check(spec.trans == Ponscripter::TransMode::TopLeft,
            ":l must be TopLeft");
      check(spec.numCells == 2 && spec.durationMs == 0 && spec.loopMode == 3,
            "/2,0,3 must parse as 2-cell manual button mode");
      check(spec.imagePath == "data/startover.png", "imagePath match");
    }

    // 3. String sprites WITH and WITHOUT ';' after ':s'
    {
      const auto s1 =
          Ponscripter::parseTaggedString(":s#ffffff^Tenha um bom dia.");
      check(s1.trans == Ponscripter::TransMode::String,
            ":s without ; must parse as String");
      check(s1.color == 0xFFFFFFu, ":s#ffffff color must be 0xFFFFFF");
      check(s1.text == "Tenha um bom dia.", ":s text must match");

      const auto s2 =
          Ponscripter::parseTaggedString(":s;#ffffff^Quero de volta");
      check(s2.trans == Ponscripter::TransMode::String,
            ":s; with ; must parse as String");
      check(s2.color == 0xFFFFFFu, ":s;#ffffff color must be 0xFFFFFF");
      check(s2.text == "Quero de volta", ":s; text must match");
    }

    // 4. Direct colors (`black`, `white`, `#FFFFFF`)
    {
      const auto b = Ponscripter::parseTaggedString("black");
      check(b.trans == Ponscripter::TransMode::DirectColor &&
                b.color == 0x000000u,
            "black keyword must parse as DirectColor 0x000000");
      const auto w = Ponscripter::parseTaggedString("#FFFFFF");
      check(w.trans == Ponscripter::TransMode::DirectColor &&
                w.color == 0xFFFFFFu,
            "#FFFFFF must parse as DirectColor 0xFFFFFF");
    }

    // 5. Synthetic SAR and NSA archive round-trip
    {
      const std::vector<std::uint8_t> payload{'O', 'g', 'g', 'S', 1, 2, 3};
      const auto nsaBytes = Ponscripter::ArchiveReader::buildSyntheticNsa(
          {{"sound\\s01.ogg", payload}, {"CG\\MENU\\title01.png", {9, 8, 7}}});
      Ponscripter::ArchiveReader reader;
      check(reader.loadFromMemory(nsaBytes, Ponscripter::ArchiveFormat::NSA),
            "Synthetic NSA must load");
      check(reader.contains("sound/s01.ogg") &&
                reader.contains("cg/menu/title01.png"),
            "Case-insensitive and slash-normalized NSA lookup must succeed");
      check(reader.extract("SOUND/S01.OGG") == payload,
            "Extracted NSA payload must match original bytes");
    }

    std::cout << "Ponscripter.TaggedString passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Ponscripter.TaggedString failed: " << e.what() << '\n';
    return 1;
  }
}
