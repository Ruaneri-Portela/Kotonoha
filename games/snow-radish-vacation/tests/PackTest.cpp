#include "SnowRadish/Pack.hpp"
#include "SnowRadish/Game.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <cctype>
#include <vector>

int main(int argc, char** argv)
{
    // flags 0x20: lit 'a', lit 'b', match to window 0xFEE (the 'a' just written), len 3.
    const uint8_t src[] = {0x20, 'a', 'b', 0xE0, 0xFE};
    uint8_t out[4] = {};
    SnowRadish::lzssDecode(src, sizeof src, out, 4);
    if (out[0] != 'a' || out[1] != 'b' || out[2] != 'a' || out[3] != 'b') { std::puts("FAIL literal"); return 1; }
    // 2x2 EVD: literal BGR(1,2,3), then a run of 3 repeating it. Rows are bottom-up.
    const std::vector<uint8_t> evd = {3, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 0xC0, 1, 2, 3, 0x82};
    SnowRadish::Image img;
    if (!SnowRadish::decodeEvd(evd, img) || img.width != 2 || img.height != 2 || img.rgb[0] != 3 ||
        img.rgb[1] != 2 || img.rgb[2] != 1 || img.rgb[11] != 1) { std::puts("FAIL evd"); return 1; }
    SnowRadish::AlphaMask mask;
    std::vector<uint8_t> alpf(200 * 150, 128);
    if (!SnowRadish::decodeAlpha(alpf, false, mask) || mask.width != 200 || mask.height != 150 ||
        mask.alpha.front() != 128) { std::puts("FAIL ALPF"); return 1; }
    std::vector<uint8_t> alpa(250 * 200, 64);
    if (!SnowRadish::decodeAlpha(alpa, true, mask) || mask.width != 250 || mask.height != 200 ||
        mask.alpha.back() != 64) { std::puts("FAIL ALPA"); return 1; }
    alpa.pop_back();
    if (SnowRadish::decodeAlpha(alpa, true, mask)) { std::puts("FAIL alpha size"); return 1; }
    std::vector<uint8_t> anm(0x684 + 18, 0);
    auto set32 = [&](size_t offset, uint32_t value) {
        anm[offset] = uint8_t(value);
        anm[offset + 1] = uint8_t(value >> 8);
        anm[offset + 2] = uint8_t(value >> 16);
        anm[offset + 3] = uint8_t(value >> 24);
    };
    set32(0, 1);
    set32(4, 2);
    set32(12, 0x684);
    set32(16, 0x684 + 6);
    set32(20, 0x684 + 12);
    set32(44, 6);
    set32(48, 6);
    set32(52, 6);
    set32(76, 4);
    set32(80, 8);
    set32(92, 2);
    set32(96, 1);
    anm[0x684] = 1; anm[0x685] = 2; anm[0x686] = 3;
    anm[0x687] = 4; anm[0x688] = 5; anm[0x689] = 6;
    anm[0x68a] = 7; anm[0x68b] = 8; anm[0x68c] = 9;
    anm[0x68d] = 10; anm[0x68e] = 11; anm[0x68f] = 12;
    anm[0x690] = 13; anm[0x691] = 14; anm[0x692] = 15;
    SnowRadish::Animation animation;
    if (!SnowRadish::decodeAnm(anm, animation) || animation.frames.size() != 3 || animation.regions.size() != 1 || animation.regions[0].actor != 2 ||
        animation.frames[0].width != 2 || animation.frames[0].height != 1 ||
        animation.frames[0].x != 4 || animation.frames[0].rgba[0] != 3 ||
        animation.frames[2].rgba[0] != 15) { std::puts("FAIL ANM"); return 1; }
    // A second independently voiced region has its own three frames.
    set32(0, 2);
    for (size_t i = 4; i <= 96; i += 4) {
        const uint32_t value = uint32_t(anm[i]) | uint32_t(anm[i+1]) << 8 | uint32_t(anm[i+2]) << 16 | uint32_t(anm[i+3]) << 24;
        set32(i + 96, value);
    }
    set32(100, 5);
    if (!SnowRadish::decodeAnm(anm, animation) || animation.frames.size() != 6 || animation.regions[1].actor != 5)
        { std::puts("FAIL ANM multi-region"); return 1; }
    set32(108, uint32_t(anm.size() + 1));
    if (SnowRadish::decodeAnm(anm, animation) || !animation.frames.empty())
        { std::puts("FAIL ANM malformed region"); return 1; }
    SnowRadish::Game game({});
    game.native(game.vm(), 219, {{1},{10},{2},{9},{0},{0},{5},{720},{0},{0x1344d}});
    if (!game.mapChoosing() || game.choices().size() != 2 || game.destination(1) != 3 || !game.choose(1) ||
        game.mapChoosing() || game.vm().var(0x344d) != 3)
        { std::puts("FAIL map destinations"); return 1; }
    {
        // Native 220: uniform select, focus starts on the variable, pick stored as 1..3,
        // opened and closed with a type 1 fade at speed 3.
        SnowRadish::Game uniform({});
        int fades = 0;
        bool fading = false;
        uniform.transition = [&](int type, int, int speed) { if (type == 1 && speed == 3) { ++fades; fading = true; } };
        uniform.transitionPlaying = [&] { return fading; };
        uniform.vm().setVar(0x344c, 2);
        uniform.native(uniform.vm(), 220, {{0},{0x1344c}});
        if (!uniform.uniformChoosing() || uniform.choices().size() != 3 || uniform.initialFocus() != 1 || fades != 1)
            { std::puts("FAIL uniform select open"); return 1; }
        fading = false;
        if (!uniform.choose(2) || uniform.choosing() || uniform.vm().var(0x344c) != 3 || fades != 2 ||
            !uniform.waitingForAudio())
            { std::puts("FAIL uniform select pick"); return 1; }
        uniform.native(uniform.vm(), 9, {{0x1344c}});
        if (uniform.choiceKind() != SnowRadish::Game::ChoiceKind::Text || uniform.initialFocus() != -1)
            { std::puts("FAIL text choice kind"); return 1; }
    }
    int pattern = -1;
    game.transition = [&](int type, int pat, int speed) { if (type == 11 && speed == 1) pattern = pat; };
    game.native(game.vm(), 64, {{11},{8206},{1}});
    if (pattern != 8206 || game.sprites().size() != 1 || game.sprites()[0].name != "STKGR2c" || game.sprites()[0].tint[0] != 200)
        { std::puts("FAIL type 11 sprite"); return 1; }
    if (std::string(SnowRadish::titleSuffix(false, false)) != "b" ||
        std::string(SnowRadish::titleSuffix(false, true)) != "c" ||
        std::string(SnowRadish::titleSuffix(true, false)) != "" ||
        std::string(SnowRadish::titleSuffix(true, true)) != "a")
        { std::puts("FAIL cleared title variants"); return 1; }
    bool cleared = false;
    int clearUpdates = 0;
    game.cleared = [&](bool flag) { cleared = flag; ++clearUpdates; };
    game.vm().setVar(0x50cb, 1);
    game.pump();
    if (!cleared || clearUpdates != 1) { std::puts("FAIL cleared flag"); return 1; }
    game.pump();
    if (clearUpdates != 1) { std::puts("FAIL duplicate cleared flag"); return 1; }
    game.vm().setVar(0x50cb, 0);
    game.pump();
    if (cleared || clearUpdates != 2) { std::puts("FAIL ending flag reset"); return 1; }
    game.setClearFlag(true);
    game.pump();
    if (clearUpdates != 2 || game.vm().var(0x50cb) != 1)
        { std::puts("FAIL restoring persistent clear flag"); return 1; }
    game.vm().setVar(0x50cb, 0);
    game.pump();
    if (clearUpdates != 3) { std::puts("FAIL clear flag after replay"); return 1; }
    if (argc > 1) {
        auto read = [](const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
        };
        const std::filesystem::path root(argv[1]);
        auto gfx = read(root / "graphic.dat");
        size_t animations = 0, cursors = 0, masks = 0;
        for (const auto& entry : SnowRadish::readDirectory(gfx)) {
            std::string name = entry.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.size() >= 4 && name.substr(name.size()-4) == ".anm") {
                if (!SnowRadish::decodeAnm(SnowRadish::extractEntry(gfx, entry), animation))
                    { std::printf("FAIL retail animation %s\n", name.c_str()); return 1; }
                ++animations;
                cursors += animation.cursor;
            } else if ((name.size() >= 5 && name.substr(name.size()-5) == ".alpf") || (name.size() >= 5 && name.substr(name.size()-5) == ".alpa")) {
                if (!SnowRadish::decodeAlpha(SnowRadish::extractEntry(gfx, entry), (name.size() >= 5 && name.substr(name.size()-5) == ".alpa"), mask))
                    { std::printf("FAIL retail mask %s\n", name.c_str()); return 1; }
                ++masks;
            }
        }
        auto voice = read(root / "voice.dat");
        auto voices = SnowRadish::readDirectory(voice);
        std::set<std::string> voiceNames;
        for (const auto& entry : voices) {
            if (entry.name.size() != 13 || entry.name[0] != 'V' || entry.name.substr(8) != ".ADP4" ||
                !std::all_of(entry.name.begin() + 1, entry.name.begin() + 8, [](unsigned char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }))
                { std::printf("FAIL voice name %s\n", entry.name.c_str()); return 1; }
            voiceNames.insert(entry.name.substr(0,8));
        }
        auto scripts = read(root / "script.dat");
        SnowRadish::Game retail(scripts);
        std::set<std::string> references, missing;
        for (const auto& entry : SnowRadish::readDirectory(scripts)) {
            auto object = retail.loadByName(entry.name);
            if (!object) { std::puts("FAIL retail script"); return 1; }
            for (const auto& text : object->strings)
                if (text.size() == 8 && text[0] == 'V' && std::all_of(text.begin()+1,text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; })) {
                    references.insert(text);
                    if (voiceNames.find(text) == voiceNames.end()) missing.insert(text);
                }
        }
        if (!retail.init()) { std::puts("FAIL retail init"); return 1; }
        std::vector<int> inputs;
        retail.pump();
        while (!retail.mapChoosing() && !retail.finished() && inputs.size() < 4000) {
            const int input = retail.choosing() ? 0 : -1;
            inputs.push_back(input);
            if (input >= 0) retail.choose(0); else retail.click();
            retail.pump();
        }
        SnowRadish::Game replay(scripts);
        if (!replay.init()) { std::puts("FAIL replay init"); return 1; }
        replay.pump();
        for (int input : inputs) {
            if (input >= 0) replay.choose(size_t(input)); else replay.click();
            replay.pump();
        }
        if (!retail.mapChoosing() || !replay.mapChoosing() || retail.mapArgs() != replay.mapArgs() ||
            retail.choices() != replay.choices() || retail.currentScript() != replay.currentScript())
            { std::puts("FAIL retail map input replay"); return 1; }
        std::printf("Retail replay: %zu inputs reproduce first map\n", inputs.size());
        if (animations != 184 || cursors != 2 || masks == 0 || voices.empty())
            { std::puts("FAIL retail inventory"); return 1; }
        std::printf("Retail: %zu ANM (%zu cursors), %zu masks, %zu ADP4; %zu script voice IDs, %zu absent from pack\n",
                    animations, cursors, masks, voices.size(), references.size(), missing.size());
    }
    std::puts("OK");
    return 0;
}
