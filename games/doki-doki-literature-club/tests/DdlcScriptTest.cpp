// Headless run of the DDLC script: loads the original archives, runs the init
// code, the splash screen and Act 1 from the start label through the first
// poem game, answering every interaction automatically.
// Usage: DdlcScriptTest [game dir] [max interactions]   (or DDLC_DIR)
#include "Ddlc/Runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>

using namespace Ddlc;

namespace {

struct HeadlessBackend : Backend {
    std::map<std::string, std::string> channels;
    std::string persistent;
    int plays = 0;
    bool quiet = false;
    bool imageSize(const std::string&, int& w, int& h) override {
        w = 1280, h = 720;
        return true;
    }
    void textSize(const std::string& text, const TextStyle& style, float& w, float& h) override {
        w = float(text.size()) * style.size * .5f;
        h = style.size;
    }
    void play(const std::string& ch, const std::string& spec, double, bool) override {
        channels[ch] = spec;
        ++plays;
    }
    void stop(const std::string& ch, double) override { channels.erase(ch); }
    std::string playing(const std::string& ch) override {
        auto it = channels.find(ch);
        return it == channels.end() ? std::string() : it->second;
    }
    void savePersistent(const std::string& text) override { persistent = text; }
    void log(const std::string& line) override {
        if (!quiet) std::fprintf(stderr, "[ddlc] %s\n", line.c_str());
    }
};

struct Stats {
    int says = 0, menus = 0, pauses = 0, transitions = 0, screens = 0, ui = 0, total = 0;
};

// Answers interactions until the script ends or `limit` is reached.
bool drive(Vm& vm, double& clock, int limit, Stats& st, bool verbose) {
    while (!vm.finished() && vm.running() && st.total < limit) {
        const Interaction& in = vm.interaction();
        ++st.total;
        clock += 0.05;
        switch (in.k) {
        case Interaction::K::Say:
            ++st.says;
            if (verbose)
                std::printf("  %s: %s\n", vm.scene.window.who.c_str(), stripTags(vm.scene.window.what).c_str());
            clock += 100;
            vm.resume();
            break;
        case Interaction::K::Pause:
        case Interaction::K::Transition:
            (in.k == Interaction::K::Pause ? st.pauses : st.transitions)++;
            clock += in.deadline > 0 ? in.deadline + .01 : 1;
            vm.resume();
            break;
        case Interaction::K::Menu:
            ++st.menus;
            if (verbose) {
                std::printf("  MENU");
                for (auto& i : in.items) std::printf(" [%s]", i.text.c_str());
                std::printf("\n");
            }
            vm.resume(Value::integer(0));
            break;
        case Interaction::K::Screen:
            ++st.screens;
            if (verbose) std::printf("  SCREEN %s: %s\n", in.screen.c_str(), in.message.c_str());
            vm.resume(Value::boolean(true));
            break;
        case Interaction::K::Ui:
            ++st.ui;
            if (in.items.size() != 10) {
                std::fprintf(stderr, "poem game offered %zu words\n", in.items.size());
                return false;
            }
            vm.resume(in.items[size_t(st.ui) % in.items.size()].value);
            break;
        default: vm.resume(); break;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* env = std::getenv("DDLC_DIR");
    std::string dir = argc > 1 ? argv[1] : env ? env : "";
    const int limit = argc > 2 ? std::atoi(argv[2]) : 4000;
    if (dir.empty()) {
        std::printf("DdlcScriptTest: no game dir (pass it or set DDLC_DIR); skipped\n");
        return 0;
    }
    Assets assets;
    if (!assets.open(dir)) {
        std::printf("DdlcScriptTest: cannot open %s; skipped\n", dir.c_str());
        return 0;
    }
    Script script;
    std::string err;
    if (!script.load(assets, &err)) {
        std::fprintf(stderr, "script load failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("script: %zu files, %zu statements, %zu inits\n", script.fileCount(), script.nodeCount(),
                script.inits().size());

    HeadlessBackend backend;
    double clock = 0;
    int failed = 0;

    // Splash screen (first run: disclaimer + menu).
    {
        Vm vm(assets, script, backend);
        vm.clock = [&] { return clock; };
        vm.init("");
        std::printf("init: %zu images, %zu errors\n", vm.images.size(), vm.errors.size());
        for (auto& e : vm.errors) std::printf("  init error: %s\n", e.c_str());
        vm.errors.clear();
        vm.start("splashscreen");
        Stats st;
        drive(vm, clock, 200, st, true);
        std::printf("splash: %d interactions (%d say, %d menu, %d pause, %d transitions), finished=%d\n", st.total,
                    st.says, st.menus, st.pauses, st.transitions, int(vm.finished()));
        if (!vm.finished()) ++failed;
        if (backend.channels["music"].find("bgm/1.ogg") == std::string::npos) {
            std::fprintf(stderr, "main menu music not started (%s)\n", backend.channels["music"].c_str());
            ++failed;
        }
        for (auto& e : vm.errors) std::printf("  splash error: %s\n", e.c_str());
        failed += int(vm.errors.size());
        vm.renderLayer(vm.scene.master, clock);
        vm.py.setAttr(vm.persistent, "playername", Value::str("Tester"));
        backend.persistent = vm.savePersistentText();
    }

    // New game.
    {
        Vm vm(assets, script, backend);
        vm.clock = [&] { return clock; };
        vm.init(backend.persistent);
        vm.errors.clear();
        vm.start("start");
        Stats st;
        const bool ok = drive(vm, clock, limit, st, std::getenv("DDLC_VERBOSE") != nullptr);
        Render r = vm.renderLayer(vm.scene.master, clock);
        std::printf("start: %d interactions (%d say, %d menu, %d pause, %d transitions, %d screens, %d poem words), "
                    "%zu statements, %zu draw ops, chapter=%s\n",
                    st.total, st.says, st.menus, st.pauses, st.transitions, st.screens, st.ui, vm.statements,
                    r.ops.size(), vm.py.lookup("chapter").repr().c_str());
        if (!ok) ++failed;
        if (st.ui < 20) {
            std::fprintf(stderr, "the first poem game did not complete (%d words)\n", st.ui);
            ++failed;
        }
        if (st.says < 300) {
            std::fprintf(stderr, "too few lines (%d)\n", st.says);
            ++failed;
        }
        for (auto& e : vm.errors) std::printf("  runtime error: %s\n", e.c_str());
        failed += int(vm.errors.size());
        vm.stop();
    }
    std::printf(failed ? "DdlcScriptTest: FAILED (%d)\n" : "DdlcScriptTest: OK\n", failed);
    return failed ? 1 : 0;
}
