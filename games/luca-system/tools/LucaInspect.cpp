#include <LucaSystem/CzImage.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemScript.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
const char *names[] = {"EQU",
                       "EQUN",
                       "EQUV",
                       "ADD",
                       "SUB",
                       "MUL",
                       "DIV",
                       "MOD",
                       "AND",
                       "OR",
                       "RANDOM",
                       "VARSTR",
                       "VARSTR_ADD",
                       "SET",
                       "FLAGCLR",
                       "GOTO",
                       "ONGOTO",
                       "GOSUB",
                       "IFY",
                       "IFN",
                       "RETURN",
                       "JUMP",
                       "FARCALL",
                       "FARRETURN",
                       "JUMPPOINT",
                       "END",
                       "STARTUP_BEGIN",
                       "STARTUP_END",
                       "TASKSCRVAR",
                       "VARSTR_SET",
                       "VARSTR_ALLOC",
                       "ARFLAGSET",
                       "COLORBG_SET",
                       "SPLINE_SET",
                       "SHAKELIST_SET",
                       "SCISSOR_TRIANGLELIST_SET",
                       "MESSAGE",
                       "MESSAGE_CLEAR",
                       "MESSAGE_WAIT",
                       "MESSAGE_AR_SET",
                       "SELECT",
                       "CLOSE_WINDOW",
                       "FADE_WINDOW",
                       "LOG_BEGIN",
                       "LOG_BREAK",
                       "LOG_END",
                       "VOICE",
                       "VOICE_STOP",
                       "WAIT_COUNT",
                       "WAIT_TIME",
                       "WAIT_TEXTFEED",
                       "COUNTER_WAIT",
                       "FFSTOP",
                       "INIT",
                       "STOP",
                       "IMAGELOAD",
                       "IMAGEUPDATE",
                       "ARC",
                       "MOVE",
                       "MOVE_SKIP",
                       "ROT",
                       "PEND",
                       "FADE",
                       "SCALE",
                       "SHAKE",
                       "SHAKELIST",
                       "BASE",
                       "MCMOVE",
                       "MCARC",
                       "MCROT",
                       "MCSHAKE",
                       "MCFADE",
                       "WAIT",
                       "WAIT_BSKIP",
                       "DRAW",
                       "WIPE",
                       "FRAMEON",
                       "FRAMEOFF",
                       "FW",
                       "SCISSOR",
                       "DELAY",
                       "RASTER",
                       "TONE",
                       "SCALECOSSIN",
                       "BMODE",
                       "SIZE",
                       "SPLINE",
                       "DISP",
                       "MASK",
                       "FACE",
                       "SEPIA",
                       "SEPIA_COLOR",
                       "CUSTOMMOVE",
                       "SWAP",
                       "ADDCOLOR",
                       "SUBCOLOR",
                       "SATURATION",
                       "CONTRAST",
                       "PRIORITY",
                       "UVWH",
                       "EVSCROLL",
                       "COLORLEVEL",
                       "NEGA",
                       "TONECURVE",
                       "SKIP_SCOPE_BEGIN",
                       "SKIP_SCOPE_END",
                       "QUAKE",
                       "BGM",
                       "BGM_WAIT_START",
                       "BGM_WAIT_FADE",
                       "BGM_PUSH",
                       "BGM_POP",
                       "BGM_ASYNC_FADE_STOP",
                       "SE",
                       "SE_STOP",
                       "SE_WAIT",
                       "SE_WAIT_COUNT",
                       "SE_WAIT_FADE",
                       "VOLUME",
                       "MOVIE",
                       "SETCGFLAG",
                       "EX",
                       "TROPHY",
                       "SETBGMFLAG",
                       "TASK",
                       "PRINTF",
                       "DIALOG",
                       "VIB_PLAY",
                       "VIB_FILE",
                       "VIB_STOP",
                       "CHAR_VOLUME",
                       "SCENE_REPLAY_END",
                       "SAVE_THUMBNAIL",
                       "TEXTURE_PREFETCH",
                       "CLOSE_PRELOAD_FILE",
                       "MANPU",
                       "SCENARIO",
                       "HAIKEI_SET",
                       "UNKNOWN"};
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: LucaInspect DATA_DIR SCRIPT [OPCODE]\n";
    return 2;
  }
  LucaSystem::LucaSystemAssets assets(argv[1], {});
  if (std::string(argv[2]) == "--dump-image") {
    if (argc != 5) {
      std::cerr
          << "Usage: LucaInspect DATA_DIR --dump-image ASSET OUTPUT.ppm\n";
      return 2;
    }
    auto image = LucaSystem::DecodeCzImage(assets.readAsset(argv[3]));
    std::ofstream out(argv[4], std::ios::binary);
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
      const unsigned a = image.rgba[i + 3];
      const unsigned checker =
          ((i / 4 / 8 + (i / 4 / image.width) / 8) & 1) ? 48 : 24;
      const unsigned char rgb[3] = {
          static_cast<unsigned char>((image.rgba[i] * a + checker * (255 - a)) /
                                     255),
          static_cast<unsigned char>(
              (image.rgba[i + 1] * a + checker * (255 - a)) / 255),
          static_cast<unsigned char>(
              (image.rgba[i + 2] * a + checker * (255 - a)) / 255)};
      out.write(reinterpret_cast<const char *>(rgb), 3);
    }
    return out ? 0 : 1;
  }
  auto bytes = assets.readAsset(argv[2]);
  LucaSystem::Script script(argv[2], std::move(bytes));
  const int filter = argc > 3 ? std::atoi(argv[3]) : -1;
  for (const auto &ins : script.instructions()) {
    if (filter >= 0 && ins.opcode != filter)
      continue;
    std::cout << ins.pos << ' ';
    if (ins.opcode < sizeof(names) / sizeof(names[0]))
      std::cout << names[ins.opcode];
    else
      std::cout << "OP_" << unsigned(ins.opcode);
    std::cout << " ff=" << unsigned(ins.fixedFlag) << " fixed=";
    for (auto b : ins.fixedParam)
      std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    std::cout << " param=";
    for (auto b : ins.paramBytes)
      std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    std::cout << std::dec << '\n';
  }
}
