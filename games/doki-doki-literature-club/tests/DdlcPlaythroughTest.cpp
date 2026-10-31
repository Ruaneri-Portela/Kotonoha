// Headless playthrough of every act with the retail data. The base directory
// is a scratch folder below the working directory, so the character files the
// script deletes and restores are real files the test can inspect (and delete,
// as the player does to leave Act 3).
// Usage: DdlcPlaythroughTest <game dir> [cycles]
#include "DdlcAssets.hpp"
#include "Ddlc/Runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace Ddlc;

namespace {

struct HeadlessBackend : Backend {
    std::map<std::string, std::string> channels;
    std::set<std::string> logs;
    std::string persistent;
    bool imageSize(const std::string&, int& w, int& h) override {
        w = 1280, h = 720;
        return true;
    }
    void textSize(const std::string& text, const TextStyle& style, float& w, float& h) override {
        w = float(text.size()) * style.size * .5f;
        h = style.size;
    }
    void play(const std::string& ch, const std::string& spec, double, bool) override { channels[ch] = spec; }
    void stop(const std::string& ch, double) override { channels.erase(ch); }
    std::string playing(const std::string& ch) override {
        auto it = channels.find(ch);
        return it == channels.end() ? std::string() : it->second;
    }
    void savePersistent(const std::string& text) override { persistent = text; }
    void log(const std::string& line) override { logs.insert(line); }
};

std::string characters(const std::filesystem::path& user) {
    std::string present;
    for (const char* name : kCharacterFiles)
        if (std::filesystem::is_regular_file(charactersDirectory(user) / name))
            present += std::string(present.empty() ? "" : ",") + std::string(name).substr(0, std::string(name).find('.'));
    return present.empty() ? "-" : present;
}

int playthrough(Vm& vm) { return int(vm.py.getAttr(vm.persistent, "playthrough").integer()); }

// Answers interactions until the script ends. In Act 3 the player deletes
// monika.chr after a while, which is the only way forward.
struct Seen {
    std::set<int> acts;
    bool note = false;
};

int drive(Vm& vm, double& clock, int limit, const std::filesystem::path& user, Seen& seen) {
    int total = 0;
    while (!vm.finished() && vm.running() && total < limit) {
        const Interaction& in = vm.interaction();
        ++total;
        seen.acts.insert(playthrough(vm));
        if (std::filesystem::exists(user / "CAN YOU HEAR ME.txt")) seen.note = true;
        clock += in.k == Interaction::K::Say ? 100 : in.deadline > 0 ? in.deadline + .01 : 1;
        if (playthrough(vm) == 3 && total == 400) {
            std::error_code error;
            std::filesystem::remove(charactersDirectory(user) / "monika.chr", error);
            std::printf("  (player deletes monika.chr)\n");
        }
        switch (in.k) {
        case Interaction::K::Menu: {
            size_t choice = 0;
            while (choice + 1 < in.items.size() && !in.items[choice].enabled) ++choice;
            vm.resume(in.items.empty() ? Value() : in.items[choice].value);
            break;
        }
        case Interaction::K::Screen: vm.resume(Value::boolean(true)); break;
        case Interaction::K::Ui:
            vm.resume(in.items.empty() ? Value() : in.items[size_t(total) % in.items.size()].value);
            break;
        default: vm.resume(); break;
        }
    }
    return total;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: DdlcPlaythroughTest <game dir> [cycles]\n");
        return 2;
    }
    const int cycles = argc > 2 ? std::atoi(argv[2]) : 12;
    const auto user = std::filesystem::absolute("ddlc-playthrough-user");
    int failed = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) {
            std::fprintf(stderr, "FAILED: %s\n", what.c_str());
            ++failed;
        }
    };
    try {
        std::filesystem::remove_all(user);
        std::filesystem::create_directories(user);
        Assets assets;
        if (!assets.open(argv[1])) throw std::runtime_error(std::string("cannot open ") + argv[1]);
        Script script;
        std::string error;
        if (!script.load(assets, &error)) throw std::runtime_error("script load failed: " + error);

        check(seedCharacters(user, assets.dir(), &assets), "the characters folder is seeded");
        check(characters(user) == "monika,natsuki,sayori,yuri", "all four characters are seeded");
        check(!seedCharacters(user, assets.dir(), &assets), "an existing characters folder is kept");

        HeadlessBackend backend;
        double clock = 0;
        Seen seen;
        std::string label = "splashscreen";
        bool credits = false;
        for (int cycle = 0; cycle < cycles && !credits; ++cycle) {
            Vm vm(assets, script, backend);
            vm.userDirectory = user;
            vm.clock = [&] { return clock; };
            vm.init(backend.persistent);
            if (cycle == 0)
                check(vm.py.eval("config.basedir").text() == user.generic_string(), "config.basedir is the user directory");
            if (label == "start" && !vm.py.getAttr(vm.persistent, "playername").truthy())
                vm.py.setAttr(vm.persistent, "playername", Value::str("Tester"));
            const int act = playthrough(vm);
            vm.start(label);
            const int interactions = drive(vm, clock, 60000, user, seen);
            const bool restart = vm.interaction().k == Interaction::K::Restart;
            std::printf("cycle %d: %s (playthrough %d) -> %d interactions, %s, chapter=%s, autoload=%s, "
                        "characters=%s\n",
                        cycle, label.c_str(), act, interactions,
                        !vm.finished() ? "stuck" : restart ? "restart" : "end",
                        vm.py.lookup("chapter").repr().c_str(),
                        vm.py.getAttr(vm.persistent, "autoload").repr().c_str(), characters(user).c_str());
            check(vm.finished(), label + " finishes in playthrough " + std::to_string(act));
            std::string files;
            for (auto& entry : std::filesystem::directory_iterator(user)) files += " [" + entry.path().filename().string() + "]";
            std::string acts;
            for (int a : seen.acts) acts += " " + std::to_string(a);
            std::printf("  acts seen:%s; base directory:%s\n", acts.c_str(), files.c_str());
            if (!vm.finished() && vm.currentNode())
                std::printf("  stuck at %s:%d (interaction %d)\n", vm.currentNode()->file.c_str(),
                            vm.currentNode()->line, int(vm.interaction().k));
            for (auto& e : vm.errors) {
                std::printf("  error: %s\n", e.c_str());
                ++failed;
            }
            const std::string autoload = vm.py.getAttr(vm.persistent, "autoload").text();
            credits = autoload == "postcredits_loop";
            backend.persistent = vm.savePersistentText();
            label = restart || label == "start" ? "splashscreen" : "start";
            if (!vm.finished()) break;
        }
        check(credits, "the playthrough reaches the credits");
        check(seen.acts.count(1) && seen.acts.count(2) && seen.acts.count(3) && seen.acts.count(4), "every act is played");
        check(seen.note, "Act 2 leaves its note in the base directory");
        for (auto& line : backend.logs) std::printf("  log: %s\n", line.c_str());
        std::filesystem::remove_all(user);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "DdlcPlaythroughTest: %s\n", error.what());
        return 1;
    }
    std::printf(failed ? "DdlcPlaythroughTest: FAILED (%d)\n" : "DdlcPlaythroughTest: OK\n", failed);
    return failed ? 1 : 0;
}
