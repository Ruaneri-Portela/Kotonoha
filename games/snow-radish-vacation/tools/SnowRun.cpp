// Headless trace runner: executes the Snow Radish scripts, auto-clicking
// through messages, and prints every native call and message.
#include "SnowRadish/Game.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: SnowRun SCRIPT.DAT [maxClicks] [quiet]\n");
        return 1;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> pack((std::istreambuf_iterator<char>(f)), {});
    const int maxClicks = argc > 2 ? std::atoi(argv[2]) : 50;
    const bool quiet = argc > 3 && argv[3][0] == 'q';
    const char* entry = argc > 4 ? argv[4] : nullptr;
    SnowRadish::Game game(std::move(pack));
    if (!quiet) game.trace = [](const std::string& s) { std::printf("  %s\n", s.c_str()); };
    if (!game.init()) {
        std::fprintf(stderr, "init failed\n");
        return 1;
    }
    if (entry) game.gotoScript(entry);
    for (int i = 0; i < maxClicks; ++i) {
        game.pump();
        if (game.finished()) {
            std::printf("[script ended in %s]\n", game.currentScript().c_str());
            break;
        }
        if (game.choosing()) {
            static int pick = 0;
            std::printf("[%s] CHOICE:", game.currentScript().c_str());
            for (const auto& t : game.choices()) std::printf(" | %s", t.c_str());
            std::printf("\n");
            game.choose(size_t(pick++ % game.choices().size() == 0 ? 0 : 0));
        } else if (game.hasMessage()) {
            const auto& m = game.message();
            std::printf("[%s] %s%s%s\n", game.currentScript().c_str(), m.name.empty() ? "" : (m.name + ": ").c_str(),
                        m.text.c_str(), m.voice.empty() ? "" : (" <" + m.voice + ">").c_str());
            game.click();
        } else {
            std::printf("[stalled in %s]\n", game.currentScript().c_str());
            break;
        }
    }
    return 0;
}
