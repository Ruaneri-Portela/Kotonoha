#include "LargePonPonRuntime.hpp"
#include <Kotonoha/Formats/Adpcm.hpp>
#include <Kotonoha/Formats/Crgb.hpp>
#include <Kotonoha/Formats/DecodedData.hpp>
#include <Kotonoha/Formats/Smfd.hpp>
#include <Pmail/GcPack.hpp>
#include <Pmail/LargePonPonAssets.hpp>
#include <Pmail/LargePonPonVm.hpp>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Retail parity gates must really verify in Release builds, where NDEBUG
// compiles standard assert() away. Use an always-active check instead.
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "[LargePonPonTest] CHECK failed: " #expr " (" << __FILE__   \
                << ":" << __LINE__ << ")" << std::endl;                        \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

namespace {

using namespace Kotonoha::Providers;
using namespace Kotonoha::LargePonPon;

void writeU16LE(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void writeU32LE(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

void testGcPackSynthetic() {
  uint8_t packed[12];
  GcPackArchive::encodePackedName("TITLE.OBJ", packed);
  CHECK(GcPackArchive::decodePackedName(packed) == "TITLE.OBJ");

  GcPackArchive::encodePackedName("OVERFLOW.CRGB", packed);
  CHECK(GcPackArchive::decodePackedName(packed) == "OVERFLOW.CRGB");

  GcPackArchive::encodePackedName("CGMODE01.P256", packed);
  CHECK(GcPackArchive::decodePackedName(packed) == "CGMODE01.P256");

  // Build minimal valid 4-sector GCPackFile: 3 header sectors + 1 data sector
  std::vector<uint8_t> archive(4 * 512, 0);
  archive[0] = 3; // headerSectors = 3 (1536 bytes)
  GcPackArchive::encodePackedName("TEST.OBJ", &archive[0x400]);
  archive[0x40C] = 3;                     // startSector = 3
  std::memset(&archive[0x410], 0xFF, 12); // sentinel
  archive[0x41C] = 4;                     // total sectors = 4
  archive[3 * 512 + 0] = 0xDE;
  archive[3 * 512 + 1] = 0xAD;

  GcPackArchive pack;
  CHECK(pack.openBytes(archive));
  CHECK(pack.entries().size() == 1);
  CHECK(pack.contains("test.obj"));
  auto data = pack.read("TEST.OBJ");
  CHECK(data.size() == 512);
  CHECK(data[0] == 0xDE && data[1] == 0xAD);
}

void testCodecsSynthetic() {
  // 1. CRGB: literal 1 pixel BGR(10, 20, 30) + repeat 1 + end
  std::vector<uint8_t> crgb = {0xC0, 10, 20, 30, 0x80, 0xFF};
  auto surf =
      Kotonoha::Formats::Crgb::decodeCrgbOrBgd(crgb.data(), crgb.size());
  CHECK(surf.width == 800 && surf.height == 600);
  auto *px = surf.rgba.data();
  // Row 0 bottom-up maps to dstY = 599
  const uint8_t *p0 = px + 599 * surf.width * 4;
  CHECK(p0[0] == 30 && p0[1] == 20 && p0[2] == 10 && p0[3] == 255);
  CHECK(p0[4] == 30 && p0[5] == 20 && p0[6] == 10 && p0[7] == 255);

  // 2. CHAR: 1 transparent skip (0x9F), 1 opaque pixel (0x7F, B=5,G=6,R=7), EOL
  std::vector<uint8_t> ch = {0x9F, 0x7F, 5, 6, 7, 0xFF};
  auto chSurf =
      Kotonoha::Formats::Crgb::decodeCharStream(ch.data(), ch.size(), 4, 2);
  CHECK(chSurf.width == 4 && chSurf.height == 2);
  const uint8_t *cp = chSurf.rgba.data() + 1 * chSurf.width * 4;
  CHECK(cp[3] == 0); // skipped pixel transparent
  CHECK(cp[4] == 7 && cp[5] == 6 && cp[6] == 5 && cp[7] == 255);

  // 3. PRGB: 1 sprite 2x1 BGR
  std::vector<uint8_t> prgb;
  writeU32LE(prgb, 1);
  writeU32LE(prgb, 8);
  writeU16LE(prgb, 2);
  writeU16LE(prgb, 1);
  prgb.insert(prgb.end(), {1, 2, 3, 4, 5, 6});
  auto sprites = Kotonoha::Formats::Crgb::decodePrgb(prgb.data(), prgb.size());
  CHECK(sprites.size() == 1 && sprites[0].width == 2 &&
         sprites[0].height == 1);

  // 4. ADP4: 4 silence pairs (8 zero samples)
  std::vector<uint8_t> adp4;
  writeU32LE(adp4, 4);
  adp4.push_back(0x01); // bit 0 = 1 (silence)
  adp4.push_back(0x83); // count = (3 + 1) = 4 pairs
  auto dec = Kotonoha::Formats::Adpcm::decodeAdp4(adp4.data(), adp4.size());
  CHECK(dec.samples.size() == 8);
  for (int16_t s : dec.samples)
    CHECK(s == 0);

  // 5. SMFD -> Standard MIDI + PCM
  std::vector<uint8_t> smfd;
  writeU16LE(smfd, 480);
  smfd.insert(smfd.end(), {0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0, 0x00, 0xFF});
  auto mid =
      Kotonoha::Formats::Smfd::smfdToStandardMidi(smfd.data(), smfd.size());
  CHECK(mid.size() > 22 && std::memcmp(mid.data(), "MThd", 4) == 0);
  auto pcm =
      Kotonoha::Formats::Smfd::decodeSmfdToPcm(smfd.data(), smfd.size(), 22050);
  CHECK(!pcm.samples.empty() && pcm.sampleRate == 22050 && pcm.channels == 2);
}

class DummyHost : public LargePonPonHost {
public:
  std::vector<uint8_t> loadScript(const std::string &) override { return {}; }
  void onMsgAppend(const std::vector<uint16_t> &words) override {
    lastWords = words;
  }
  bool onMsgShow() override {
    ++msgShows;
    return true;
  }
  bool onWinOpen() override { return false; }
  bool onWinClose() override { return false; }
  void onSetSpeaker(const std::vector<uint16_t> &) override {}
  void onChoiceClear() override {}
  void onChoiceAdd(int32_t, int32_t, const std::vector<uint16_t> &) override {}
  bool onChoiceRun(int32_t) override { return true; }
  void onSetSeenFlag(int32_t) override {}
  void onAttachVoice(const std::string &, int32_t) override {}
  bool onTransition(int32_t, int32_t) override { return false; }
  void onLoadCg(const std::string &) override {}
  void onLoadBg(const std::string &) override {}
  void onLoadChar(int32_t, const std::string &) override {}
  void onFillColor(int32_t) override {}
  bool onPlayPchr(const std::string &, int32_t, int32_t, int32_t, int32_t,
                  const std::string &) override {
    return false;
  }
  bool onScreenShake(int32_t, int32_t, int32_t) override { return false; }
  void onLoadCgVariant(const std::string &, int32_t, int32_t) override {}
  void onLoadCanm(int32_t, const std::string &) override {}
  void onComposite() override {}
  void onDrawCanm(int32_t, int32_t, int32_t, int32_t) override {}
  void onSetCharPos(int32_t, int32_t, int32_t) override {}
  void onBgmPlay(const std::string &, int32_t) override {}
  void onBgmStop() override {}
  void onBgmFade(int32_t) override {}
  void onVoicePlay(const std::string &, int32_t) override {}
  void onVoiceStop() override {}
  bool onVoiceWait(int32_t) override { return false; }
  void onSetVoiceSync(int32_t) override {}
  bool onWait(int32_t, int32_t) override { return false; }
  bool onNameInput() override { return false; }
  bool onMapMode() override { return false; }
  void onSaveCheckpoint() override {}
  void onSetSaveEnable(int32_t) override {}
  bool onCheckSaveData() override { return false; }
  bool onLoadDialog() override { return false; }
  bool onReturnTitle(int32_t) override { return false; }
  bool onHeartMode(int32_t) override { return false; }
  bool onCgMode(int32_t) override { return false; }
  bool onTitleMenu(int32_t) override { return false; }
  bool onEndingWait(int32_t) override { return false; }

  std::vector<uint16_t> lastWords;
  int msgShows = 0;
};

void testVmSynthetic() {
  DummyHost host;
  LargePonPonVm vm(host);

  // DEFINE.OBJ: DEF_SUB slot=1, body: EVAL (44 10, 44 25, 20 [+], 00), STORE_W
  // 16, RET
  std::vector<uint8_t> def = {
      0x52, 1,    0,  11, // DEF_SUB slot 1, skip len = 11 -> offset 3+11 = 14
      0x04, 0x44, 10, 0x44, 25, 0x20, 0x00, // EVAL 10 + 25 = 35
      0x15, 16,                             // STORE_W [16]
      0x0A,                                 // RET
      0x00                                  // END
  };
  CHECK(vm.loadDefine(def));

  // Script: CALL_SUB 1, SET_BIT 2048, SAVE_CHECKPOINT, END
  struct ScriptHost : DummyHost {
    std::vector<uint8_t> script;
    std::vector<uint8_t> loadScript(const std::string &) override {
      return script;
    }
  } sh;
  sh.script = {0x53, 1, 0x64, 0x98, 0x00, 0xF5, 0x00};
  // Note: 0x98 0x00 -> varint: extra=(0x98>>4)&3=1, v=(8<<8)|0 = 2048
  LargePonPonVm vm2(sh);
  CHECK(vm2.loadDefine(def));
  CHECK(vm2.jumpScript("TEST", 0));
  vm2.runUntilYield(100);
  CHECK(vm2.getW(5) == -1);
  CHECK(vm2.getW(16) == 35);
  CHECK(vm2.getBit(2048));

  // Save/load round-trip (1,568 bytes)
  auto save = vm2.serializeSaveData(true);
  CHECK(save.size() == LargePonPonVm::kSaveFileSize);
  LargePonPonVm vm3(sh);
  std::string scriptName;
  uint32_t savedPc = 0;
  CHECK(vm3.deserializeSaveData(save, scriptName, savedPc));
  CHECK(scriptName == "TEST");
  CHECK(vm3.getW(16) == 35);
  CHECK(vm3.getBit(2048));
}

void testRetail(const std::string &dir) {
  std::cout << "[LargePonPonTest] Verifying retail directory: " << dir
            << std::endl;
  CHECK(isLargePonPonDirectory(dir));
  auto assets = std::make_shared<LargePonPonAssets>(dir);
  CHECK(assets->scriptPack().entries().size() == 241);
  CHECK(assets->graphicPack().entries().size() == 703);
  CHECK(assets->soundPack().entries().size() == 4400);
  CHECK(assets->config().config[0] == 8);
  CHECK(assets->config().names[0] == "伊能");

  // Decode ALL 703 graphic files in GRAPHIC.DAT
  size_t decodedGraphics = 0;
  for (const auto &e : assets->graphicPack().entries()) {
    const auto dot = e.name.find_last_of('.');
    const std::string ext =
        (dot != std::string::npos) ? e.name.substr(dot) : "";
    const auto bytes = assets->graphicPack().read(e.name);
    CHECK(!bytes.empty());
    if (ext == ".CRGB" || ext == ".BGD") {
      auto s =
          Kotonoha::Formats::Crgb::decodeCrgbOrBgd(bytes.data(), bytes.size());
      CHECK(s.width == 800 && s.height == 600);
    } else if (ext == ".CHAR") {
      auto s = Kotonoha::Formats::Crgb::decodeCharStream(
          bytes.data(), bytes.size(), 800, 600);
      CHECK(s.width == 800 && s.height == 600);
    } else if (ext == ".PCHR") {
      auto frames = Kotonoha::Formats::Crgb::decodePchrOrIcon(
          bytes.data(), bytes.size(), 800, 600);
      CHECK(!frames.empty() && frames[0].width == 800);
    } else if (ext == ".ICON") {
      auto frames = Kotonoha::Formats::Crgb::decodePchrOrIcon(
          bytes.data(), bytes.size(), 32, 32);
      CHECK(!frames.empty() && frames[0].width == 32);
    } else if (ext == ".PRGB") {
      auto sprites =
          Kotonoha::Formats::Crgb::decodePrgb(bytes.data(), bytes.size());
      CHECK(!sprites.empty() && sprites[0].width > 0);
    } else if (ext == ".P256") {
      CHECK(Kotonoha::Formats::Crgb::p256ThumbnailCount(bytes.size()) > 0);
      auto t = Kotonoha::Formats::Crgb::decodeP256Thumbnail(
          bytes.data(), bytes.size(), 0, true);
      CHECK(t.width == 150 && t.height == 112);
    } else if (ext == ".CANM") {
      auto c = Kotonoha::Formats::Crgb::decodeCanm(bytes.data(), bytes.size());
      bool hasAny = false;
      for (const auto &sl : c.slots)
        if (!sl.frames.empty())
          hasAny = true;
      CHECK(hasAny);
    } else if (ext == ".ALPF") {
      auto m =
          Kotonoha::Formats::Crgb::decodeAlpfMask(bytes.data(), bytes.size());
      CHECK(m.size() == 800 * 600);
    } else if (ext == ".ALPM") {
      auto m =
          Kotonoha::Formats::Crgb::decodeAlpmMask(bytes.data(), bytes.size());
      CHECK(m.size() == 800 * 600);
    }
    ++decodedGraphics;
  }
  CHECK(decodedGraphics == 703);
  std::cout << "[LargePonPonTest] Decoded all " << decodedGraphics
            << " graphics in GRAPHIC.DAT cleanly!" << std::endl;

  // Decode all 15 SMFD + first 20 ADP4
  size_t smfdCount = 0;
  size_t adp4Count = 0;
  for (const auto &e : assets->soundPack().entries()) {
    const auto dot = e.name.find_last_of('.');
    const std::string ext =
        (dot != std::string::npos) ? e.name.substr(dot) : "";
    if (ext == ".SMFD") {
      const auto bytes = assets->soundPack().read(e.name);
      auto mid = Kotonoha::Formats::Smfd::smfdToStandardMidi(bytes.data(),
                                                             bytes.size());
      CHECK(mid.size() > 32);
      auto pcm = Kotonoha::Formats::Smfd::decodeSmfdToPcm(bytes.data(),
                                                          bytes.size(), 22050);
      CHECK(!pcm.samples.empty());
      ++smfdCount;
    } else if (ext == ".ADP4" && adp4Count < 20) {
      const auto bytes = assets->soundPack().read(e.name);
      auto dec =
          Kotonoha::Formats::Adpcm::decodeAdp4(bytes.data(), bytes.size());
      CHECK(!dec.samples.empty());
      ++adp4Count;
    }
  }
  CHECK(smfdCount == 15 && adp4Count == 20);
  std::cout << "[LargePonPonTest] Decoded all " << smfdCount << " SMFD and "
            << adp4Count << " sample ADP4 files cleanly!" << std::endl;

  // Boot LargePonPonRuntime headlessly: DEFINE -> TITLE -> START -> PRO
  // dialogue
  Kotonoha::PmailPort::LargePonPonRuntime runtime(dir, assets);
  CHECK(runtime.vm().loadDefine(assets->loadScriptBytes("DEFINE")));
  CHECK(runtime.vm().jumpScript("TITLE", 0));

  bool reachedStart = false;
  bool reachedProDialogue = false;
  for (int iter = 0; iter < 60; ++iter) {
    runtime.vm().runUntilYield(4096);
    if (runtime.vm().currentScriptName() == "START")
      reachedStart = true;
    if (runtime.vm().currentScriptName() == "PRO" &&
        !runtime.dialogueText().empty()) {
      reachedProDialogue = true;
      std::cout << "[LargePonPonTest] Reached PRO dialogue: "
                << runtime.dialogueText() << std::endl;
      break;
    }
    runtime.advanceInput();
  }
  CHECK(reachedStart);
  CHECK(reachedProDialogue);
  std::cout << "[LargePonPonTest] Retail boot & story progression verified!"
            << std::endl;
}

} // namespace

int main(int argc, char **argv) {
  testGcPackSynthetic();
  testCodecsSynthetic();
  testVmSynthetic();
  std::cout << "[LargePonPonTest] All synthetic unit tests passed!"
            << std::endl;
  // CTest registers this binary twice: without an argument for the
  // always-on synthetic gate and with LARGEPONPON_TEST_DATA for the retail
  // gate. An empty/missing retail argument means "no data supplied", so
  // exit 77 to let CTest report the real-labelled test as skipped.
  if (argc > 1) {
    const char *dir = argv[1];
    if (!dir || dir[0] == '\0')
      return 77;
    testRetail(dir);
  }
  return 0;
}
