// Synthetic Pure Mail save/load regression: valid indexed script pack built
// in memory. No retail assets or game text required.
#include <Pmail/Port.hpp>
#include <Pmail/StateIO.hpp>
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void u16(std::vector<uint8_t> &d, size_t pos, uint16_t v) {
  d.at(pos) = uint8_t(v);
  d.at(pos + 1) = uint8_t(v >> 8);
}
void u32(std::vector<uint8_t> &d, size_t pos, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    d.at(pos + i) = uint8_t(v >> (i * 8));
}
void append32(std::vector<uint8_t> &d, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    d.push_back(uint8_t(v >> (i * 8)));
}
std::vector<uint8_t> object(uint16_t id,
                            const std::vector<std::string> &strings,
                            const std::vector<uint32_t> &instructions = {}) {
  std::vector<uint8_t> d(4 + strings.size() * 4, 0);
  for (size_t i = 0; i < strings.size(); ++i) {
    const auto offset = d.size();
    require(offset + strings[i].size() + 1 < 65536,
            "Synthetic descriptor offset exceeds 16 bits");
    u16(d, 4 + i * 4, uint16_t(offset));
    u16(d, 6 + i * 4, uint16_t(strings[i].size() + 1));
    d.insert(d.end(), strings[i].begin(), strings[i].end());
    d.push_back(0);
  }
  while (d.size() % 4)
    d.push_back(0);
  u16(d, 0, uint16_t(d.size())); // user table offset
  u16(d, 2, uint16_t(strings.size()));
  append32(d, 4); // user table length
  for (const auto instruction : instructions)
    append32(d, instruction);
  append32(d, id); // parser reads the object ID from final 4 bytes
  return d;
}
std::vector<uint8_t> pack() {
  std::vector<uint8_t> archive;
  std::vector<uint8_t> index(4 * 0x50, 0);
  const std::array<std::pair<std::string, std::vector<std::string>>, 4> items{
      {{"pmail.obj", {"room.obj"}},
       {"start.obj", {}},
       {"text.obj", {"$NO at $HO", "12:30", "hello"}},
       {"room.obj", {}}}};
  for (size_t i = 0; i < items.size(); ++i) {
    const auto pos = i * 0x50;
    // The caller script writes a variable and ends. A child room.obj
    // ends immediately, exercising the host's return-frame restoration.
    const std::vector<uint32_t> instructions =
        items[i].first == "start.obj"
            ? std::vector<uint32_t>{0x1103002au, 0x10204000u, 0x050300d5u}
            // SYS 213 original title
            : std::vector<uint32_t>{};
    const auto data = object(uint16_t(i + 1), items[i].second, instructions);
    require(items[i].first.size() < 0x40, "Synthetic object name too long");
    u16(index, pos, uint16_t(i + 1));
    for (size_t n = 0; n < items[i].first.size(); ++n)
      index[pos + 4 + n] = uint8_t(items[i].first[n]);
    u32(index, pos + 0x44, uint32_t(archive.size()));
    u32(index, pos + 0x48, uint32_t(data.size()));
    archive.insert(archive.end(), data.begin(), data.end());
  }
  archive.insert(archive.end(), index.begin(), index.end());
  append32(archive, uint32_t(index.size()) ^ 0xf0f0f0f0u);
  append32(archive, 0xf0f0f0f0u); // raw, uncompressed index
  append32(archive, 0xf0f0f0f0u);
  return archive;
}
Pmail::Arg number(int value) { return {uint32_t(uint16_t(value))}; }
Pmail::Arg text(size_t id) { return {(3u << 20) | (2u << 16) | uint32_t(id)}; }
void checkPanels(const Kotonoha::PmailPort::PmailGame &game) {
  const auto chat = game.chatPanel();
  const auto mail = game.mailPanel();
  require(chat.active && chat.entries.size() == 1 &&
              chat.entries[0].text == "Alice at Bob",
          "Chat panel did not round-trip with player name substitutions");
  require(chat.params[6] == 7, "Chat panel numeric parameters were lost");
  require(mail.active && mail.entries.size() == 1 &&
              mail.entries[0].text == "hello" && mail.selection == 9,
          "Mail panel was not restored independently");
  const auto icons = game.computerIcons();
  require(icons[24].active && icons[24].x == 245 && icons[24].y == 152,
          "SYS 50 icon position was not restored");
  require(game.computerClock() == "12:30" && game.playerNameNO() == "Alice" &&
              game.playerNameHO() == "Bob",
          "Clock or customized names were not restored");
}
void checksum(std::string &blob) {
  require(blob.size() >= 8, "Invalid synthetic save");
  const auto len = blob.size() - 8;
  const auto hash = Pmail::StateIO::fingerprint(blob.data(), len);
  for (unsigned i = 0; i < 8; ++i)
    blob[len + i] = char(hash >> (8u * i));
}
void testChildScriptReturn() {
  using Kotonoha::PmailPort::MouthRegionFrame;
  require(MouthRegionFrame(20, 3, 2) == 22 &&
              MouthRegionFrame(20, 2, 2) == 20 &&
              MouthRegionFrame(7, 1, 2) == 7 && MouthRegionFrame(7, 2, -1) == 7,
          "Audio mouth phases must stay inside each decoded frame region");
  const auto archive = pack();
  const std::string logicalPath = "data/pack/script.dat";
  Kotonoha::PmailPort::PmailGame game(archive, logicalPath);
  require(game.init(), "Synthetic script chain did not initialize");
  // The library contains one string: the name of the child scene.
  constexpr uint32_t roomName = (1u << 20) | (2u << 16);
  game.native(game.vm(), 16, {{roomName}});
  require(game.currentScript() == "room.obj" && !game.vm().ended() &&
              game.vm().current(),
          "SYS 16 did not transfer to the child scene");

  // Real saves taken while the child script is active must carry the
  // caller frame and resume the original script after the child HALT.
  const auto checkpoint = game.serialize();
  Kotonoha::PmailPort::PmailGame restored(archive, logicalPath);
  restored.deserialize(checkpoint);
  require(restored.currentScript() == "room.obj",
          "Save/load lost the pending child script");

  for (auto *current : {&game, &restored}) {
    current->pump(20);
    require(current->currentScript() == "start.obj" && current->atTitle() &&
                !current->vm().ended() && current->vm().var(0x4000) == 42u,
            "Child HALT did not resume the parent and reach the title menu");
  }
}

void testSaveAndCorruption() {
  const auto archive = pack();
  const std::string logicalPath = "data/pack/script.dat";
  auto frames = [](const std::string &name) {
    return name == "osboot" ? size_t(80) : size_t(0);
  };
  Kotonoha::PmailPort::PmailGame game(archive, logicalPath);
  game.animationFrameCount = frames;
  require(game.init(), "Synthetic PMAIL pack failed to initialize");
  game.setPlayerNames("Alice", "Bob");
  auto call = [&](int id, std::vector<Pmail::Arg> args) {
    game.native(game.vm(), id, args);
  };
  call(50, {number(24), number(245), number(152)});
  call(52, {text(0), number(1), number(2), number(200), number(30), number(16),
            number(8)});
  call(80, {text(1)});
  call(176, {number(1), text(0)});
  call(179, {number(1), number(2), number(3), number(4), number(5), number(6),
             number(7)});
  call(192, {number(2), text(2)});
  call(195, {number(9)});
  call(51, {number(0), number(245), number(152)});
  for (int i = 0; i < 4; ++i)
    game.advanceAnimation(54);
  game.advanceAnimation(17);
  require(game.animating() && game.animations().back().frame == 4,
          "Synthetic OSBOOT pre-logo animation checkpoint failed");
  checkPanels(game);
  const auto saved = game.serialize();
  require(saved.find("PMAIL-GAME-5") != std::string::npos,
          "Save must have V5 state marker");

  Kotonoha::PmailPort::PmailGame restored(archive, logicalPath);
  restored.animationFrameCount = frames;
  restored.deserialize(saved);
  checkPanels(restored);
  require(restored.serialize() == saved,
          "PMAIL V5 save/load/save differs byte-for-byte");
  require(restored.animations().size() == 1 &&
              restored.animations()[0].frame == 4 &&
              restored.animations()[0].elapsedMs == 17,
          "Restored animation lost current frame or sub-frame phase");
  restored.advanceAnimation(37);
  require(restored.animations()[0].frame == 5,
          "Animation did not resume from saved phase");

  const auto afterResume = restored.serialize();
  std::string corrupt = afterResume;
  corrupt.back() ^= 1;
  bool rejected = false;
  try {
    restored.deserialize(corrupt);
  } catch (...) {
    rejected = true;
  }
  require(rejected && restored.serialize() == afterResume,
          "Corrupt checksum modified live computer/animation state");

  // A checksum-valid but out-of-range icon coordinate must also be
  // rejected before any live state is replaced.
  corrupt = afterResume;
  const size_t iconTable = corrupt.size() - 8 - 25u * 12u;
  for (int i = 0; i < 4; ++i)
    corrupt[iconTable + 4 + i] = char(uint32_t(9999) >> (8 * i));
  checksum(corrupt);
  rejected = false;
  try {
    restored.deserialize(corrupt);
  } catch (...) {
    rejected = true;
  }
  require(rejected && restored.serialize() == afterResume,
          "Invalid icon coordinates modified live state");

  // Restore a version-4 save that has the exact same *prefix* fields but
  // no V5 panels/icon table; this checks compatibility without retail data.
  Kotonoha::PmailPort::PmailGame oldGame(archive, logicalPath);
  require(oldGame.init(), "Legacy synthetic pack did not load");
  oldGame.setPlayerNames("Alice", "Bob");
  std::string legacy = oldGame.serialize();
  constexpr size_t extra = 2u * (4u + 4u + 7u * 4u + 4u) + 25u * (4u + 4u + 4u);
  require(legacy.size() > extra + 8 + 16, "Legacy V5 fixture too small");
  legacy.erase(legacy.size() - 8 - extra, extra);
  legacy.replace(4, 12, "PMAIL-GAME-4");
  checksum(legacy);
  Kotonoha::PmailPort::PmailGame upgraded(archive, logicalPath);
  upgraded.deserialize(legacy);
  require(!upgraded.chatPanel().active && !upgraded.mailPanel().active &&
              upgraded.playerNameNO() == "Alice" &&
              upgraded.playerNameHO() == "Bob",
          "Legacy V4 save failed to restore without V5 panel data");
  require(upgraded.serialize().find("PMAIL-GAME-5") != std::string::npos,
          "Legacy V4 state did not upgrade when saved");
  game.returnToTitle();
  require(!game.hasComputerOverlay() && !game.computerIcons()[24].active,
          "Return to title retained the previous desktop state");
}
} // namespace

int main() {
  try {
    testSaveAndCorruption();
    testChildScriptReturn();
    std::cout << "PMAIL V5 computer save/load, child-script continuation, "
                 "animation resume, corruption "
                 "rejection, V4 upgrade and scene cleanup passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "PMAIL ComputerSave: " << e.what() << '\n';
    return 1;
  }
}
