// Retail-only route smoke audit. The original game's data is supplied by the
// tester; no commercial assets are compiled into Kotonoha or the CI.
#include <Pmail/Port.hpp>
#include <Pmail/TransitionTrace.hpp>
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
#include <utility>
#include <vector>

namespace {
std::vector<size_t> parseChoices(const std::string &text) {
  std::vector<size_t> choices;
  if (text.empty())
    return choices;
  size_t cursor = 0;
  while (cursor < text.size()) {
    const size_t end = text.find(',', cursor);
    const auto token = text.substr(
        cursor, end == std::string::npos ? std::string::npos : end - cursor);
    if (token.empty() ||
        token.find_first_not_of("0123456789") != std::string::npos ||
        token.size() > 5)
      throw std::runtime_error(
          "Choices must be comma-separated zero-based indices");
    choices.push_back(size_t(std::stoul(token)));
    cursor = end == std::string::npos ? text.size() : end + 1;
  }
  return choices;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: PmailRouteAuditCheck <game-data-directory> "
                 "[choice-indexes: 0,1,...] [max-seconds: 1..1800]\n";
    return 2;
  }
  try {
    const auto choices =
        argc >= 3 ? parseChoices(argv[2]) : std::vector<size_t>{};
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
    const uint64_t traceStart = SDL_GetTicks();
    Kotonoha::PmailPort::TransitionTrace evidence(200);
    const auto trace = [&](std::string kind, std::string resource = {},
                           int frame = -1, int x = 0, int y = 0) {
      Kotonoha::PmailPort::TransitionEvidence e;
      e.elapsedMs = SDL_GetTicks() - traceStart;
      e.script = game.currentScript();
      e.pc = game.vm().pc();
      e.kind = std::move(kind);
      e.resource = std::move(resource);
      e.frame = frame;
      e.x = x;
      e.y = y;
      evidence.record(std::move(e));
    };
    game.onNative = [&](int id, const std::vector<Pmail::Arg> &args) {
      ++nativeCounts[id];
      if (id != 48 && id != 49 && id != 50 && id != 51 && id != 64 && id != 69)
        return;
      std::string resource;
      int x = 0, y = 0, frame = -1;
      if (id == 51 && !args.empty()) {
        const int preset = game.vm().value(args[0]);
        if (preset >= 0 &&
            size_t(preset) < Kotonoha::PmailPort::AnimationPresets.size()) {
          resource =
              std::string(
                  Kotonoha::PmailPort::AnimationPresets[size_t(preset)].name) +
              ".anim";
          frame = 0;
        }
        if (args.size() > 1)
          x = game.vm().value(args[1]);
        if (args.size() > 2)
          y = game.vm().value(args[2]);
      } else if (id == 48 || id == 49) {
        // Only capture named graphic assets. Other syscalls can contain
        // private story/dialogue strings, which are not trace evidence.
        for (const auto &arg : args) {
          if (arg.tag() == 2) {
            resource = game.vm().text(arg);
            break;
          }
        }
      }
      trace("SYS" + std::to_string(id), resource, frame, x, y);
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
    uint32_t previousScene = game.sceneVersion();
    std::string previousScript;
    int previousFrame = -1, previousPreset = -1;
    const uint64_t start = SDL_GetTicks();
    const uint64_t deadline = start + seconds * 1000u;
    while (true) {
      if (SDL_GetTicks() > deadline) {
        timedOut = true;
        break;
      }
      game.advanceAnimation(16);
      game.pump(10000);
      const auto script = game.currentScript();
      scripts.insert(script);
      if (script != previousScript) {
        trace("script", script);
        previousScript = script;
      }
      if (game.sceneVersion() != previousScene) {
        previousScene = game.sceneVersion();
        trace("background", game.background());
        for (const auto &sprite : game.sprites())
          trace("sprite", sprite.name, -1, sprite.x, sprite.y);
      }
      const auto active = game.animations();
      if (!active.empty()) {
        const auto &animation = active.back();
        const int preset = int(animation.preset);
        const int frame = int(animation.frame);
        if ((frame != previousFrame || preset != previousPreset) &&
            (frame == 0 || frame % 10 == 0 ||
             frame + 1 == int(animation.count) || !animation.playing)) {
          trace(animation.playing ? "animation-frame" : "animation-end",
                std::string(
                    Kotonoha::PmailPort::AnimationPresets[animation.preset]
                        .name) +
                    ".anim",
                frame, animation.x, animation.y);
        }
        previousPreset = preset;
        previousFrame = frame;
      } else {
        previousFrame = -1;
        previousPreset = -1;
      }
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
        const auto requested =
            choicePoints < choices.size() ? choices[choicePoints] : size_t(0);
        if (requested >= current.size())
          throw std::runtime_error(
              "Requested choice index is out of range at " +
              game.currentScript());
        if (!game.choose(requested))
          throw std::runtime_error("Choice was not committed");
        ++choicePoints;
      } else if (game.nativeTimeWaitActive()) {
        if (!game.click())
          throw std::runtime_error("Timed wait would not skip");
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
    std::cout << "PMAIL route audit: "
              << (returnedToTitle ? "title returned"
                  : timedOut      ? "time budget exceeded"
                  : game.ended()  ? "script ended"
                                  : "stopped")
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
    // A machine-readable coverage report of *all encountered* unknown call
    // sites. A single SYS id can have multiple argument signatures depending
    // on the script and state. Never print game dialogue or player text.
    for (const auto &site : game.unhandledNativeSites())
      std::cerr << "PMAIL_UNHANDLED_SITE\t" << site.id << "\t" << site.script
                << "\t" << site.pc << "\t" << site.hits << "\t"
                << site.signature << "\n";
    // A bounded, machine-readable timeline of scene/asset/animation events.
    // Captures the first events, last events and (when named) the history
    // preceding an Overflow resource, without emitting retail story text.
    evidence.write(std::cout);
    if (!started || timedOut || missingScripts || unknownBytecode ||
        !unsupported.empty())
      return 1;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "PMAIL route audit error: " << error.what() << "\n";
    return 1;
  }
}
