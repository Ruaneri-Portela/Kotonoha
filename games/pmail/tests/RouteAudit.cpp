// Retail-only route smoke audit. The original game's data is supplied by the
// tester; no commercial assets are compiled into Kotonoha or the CI.
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <SnowRadish/SnowAssets.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<size_t> parseChoices(const std::string &text) {
  std::vector<size_t> choices;
  if (text.empty())
    return choices;
  size_t cursor = 0;
  while (cursor < text.size()) {
    const size_t end = text.find(',', cursor);
    const auto token = text.substr(cursor, end == std::string::npos
                                              ? std::string::npos
                                              : end - cursor);
    if (token.empty() || token.find_first_not_of("0123456789") !=
                             std::string::npos || token.size() > 5)
      throw std::runtime_error("Choices must be comma-separated zero-based indices");
    choices.push_back(size_t(std::stoul(token)));
    cursor = end == std::string::npos ? text.size() : end + 1;
  }
  return choices;
}
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: PmailRouteAuditCheck <game-data-directory> "
                 "[choice-indexes: 0,1,...] [max-seconds: 1..1800]\n";
    return 2;
  }
  try {
    const auto choices = argc >= 3 ? parseChoices(argv[2])
                                    : std::vector<size_t>{};
    const auto seconds = argc >= 4 ? std::stoul(argv[3]) : 90ul;
    if (seconds < 1 || seconds > 1800)
      throw std::runtime_error("Maximum time must be 1..1800 seconds");
    const auto dataDir = std::filesystem::path(argv[1]);
    const auto packPath = dataDir / "data/pack/script.dat";
    std::ifstream file(packPath, std::ios::binary);
    if (!file)
      throw std::runtime_error("Missing retail data/pack/script.dat");
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    if (bytes.empty())
      throw std::runtime_error("Empty retail script pack");
    Kotonoha::PmailPort::PmailGame game(std::move(bytes), packPath.string());
    Kotonoha::Providers::SnowAssets assets(dataDir.string());
    game.animationFrameCount = [&](const std::string &name) {
      return assets.AnimationOf(name).frames.size();
    };
    std::map<int, size_t> nativeCounts;
    std::set<std::string> scripts;
    size_t missingScripts = 0, unknownBytecode = 0;
    game.onNative = [&](int id, const std::vector<Pmail::Arg> &) {
      ++nativeCounts[id];
    };
    game.trace = [&](const std::string &message) {
      // Normal dialogue and native commands can produce millions of lines.
      // Emit actionable diagnostics, not private story or commercial text.
      if (message.rfind("! ", 0) != 0)
        return;
      std::cerr << message << '\n';
      if (message.find("Missing PMAIL script:") != std::string::npos)
        ++missingScripts;
      if (message.find("Unsupported PMAIL bytecode") != std::string::npos)
        ++unknownBytecode;
    };
    if (!game.init())
      throw std::runtime_error("PMAIL boot could not load pmail.obj/start.obj");

    bool started = false, returnedToTitle = false, timedOut = false;
    size_t messages = 0, choicePoints = 0, animations = 0;
    const uint64_t start = SDL_GetTicks();
    const uint64_t deadline = start + seconds * 1000u;
    while (true) {
      if (SDL_GetTicks() > deadline) {
        timedOut = true;
        break;
      }
      game.advanceAnimation(16);
      game.pump(10000);
      scripts.insert(game.currentScript());
      if (game.atTitle()) {
        if (started) {
          returnedToTitle = true;
          break;
        }
        if (!game.titleChoose(0))
          throw std::runtime_error("Cannot start original title script");
        started = true;
      } else if (game.animating()) {
        ++animations;
      } else if (game.choosing()) {
        const auto current = game.choices();
        const auto requested = choicePoints < choices.size()
                                 ? choices[choicePoints] : size_t(0);
        if (requested >= current.size())
          throw std::runtime_error("Requested choice index is out of range at " +
                                   game.currentScript());
        if (!game.choose(requested))
          throw std::runtime_error("Choice was not committed");
        ++choicePoints;
      } else if (game.waitingForClick()) {
        if (!game.click())
          throw std::runtime_error("Dialogue would not advance");
        ++messages;
      } else if (game.ended()) {
        break;
      } else {
        SDL_Delay(1);
      }
    }
    std::cout << "PMAIL route audit: " << (returnedToTitle ? "title returned"
                            : timedOut ? "time budget exceeded"
                            : game.ended() ? "script ended" : "stopped")
              << ", dialogues=" << messages << ", choices=" << choicePoints
              << ", animation ticks=" << animations << "\n";
    std::cout << "Scripts visited (" << scripts.size() << "):\n";
    for (const auto &script : scripts)
      std::cout << "  " << script << "\n";
    std::cout << "Native commands seen:\n";
    for (const auto &[id, count] : nativeCounts)
      std::cout << "  SYS " << id << ": " << count << "\n";
    const auto unsupported = game.unhandledNativeIds();
    for (const int id : unsupported)
      std::cerr << "Unimplemented native SYS " << id << "\n";
    if (!started || timedOut || missingScripts || unknownBytecode ||
        !unsupported.empty())
      return 1;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PMAIL route audit error: " << error.what() << "\n";
    return 1;
  }
}
