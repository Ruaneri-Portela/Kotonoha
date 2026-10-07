extern "C" {
#include <Kotonoha/parsers/Ors.h>
}

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path path;
    Kotonoha_orsData events{};

    explicit Fixture(const std::string& lines) {
        static unsigned sequence = 0;
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("kotonoha_g2_1_" + std::to_string(stamp) + "_" +
                std::to_string(++sequence) + ".ors");
        {
            std::ofstream out(path, std::ios::binary);
            Require(static_cast<bool>(out), "fixture open failed");
            out << lines;
        }
        events = Kotonoha_OrsParser(path.string().c_str());
    }

    ~Fixture() {
        Kotonoha_OrsClean(&events);
        std::error_code error;
        fs::remove(path, error);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

std::vector<Kotonoha_orsEvent*> Collect(const Kotonoha_orsData& events) {
    std::vector<Kotonoha_orsEvent*> result;
    for (auto* event = events.data; event; event = event->next)
        result.push_back(event);
    Require(result.size() == events.size, "event list size mismatch");
    return result;
}

void RunFixtures() {
    {
        Fixture fixture(
            "[CreateBG]=00:00:00\tBGS\tEvent00/base\t00:02:00;\n"
            "[PrintText]=00:01:00\tMakoto\tHello\t00:02:00;\n"
            "[PlayVoice]=00:01:00\tVoice00/line\t1\tmak\t00:02:00;\n"
            "[PlaySe]=00:02:00\t1\tSe00/hit\t00:03:00;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 4, "normal event count");
        Require(events[0]->command == CREATE_BG &&
                    std::string(events[0]->data.create_bg->a) == "BGS",
                "CreateBG field0");
        Require(events[1]->command == PRINT_TEXT &&
                    events[2]->command == PLAY_VOICE,
                "same-start source order");
        Require(std::string(events[0]->data.create_bg->path) ==
                    "Event00/base" &&
                    std::string(events[1]->data.print_text->text) == "Hello" &&
                    std::string(events[3]->data.play_se->path) == "Se00/hit",
                "normal payload fields");
        Require(events[1]->start == events[2]->start,
                "same-start timestamps changed");
        Require(events[1]->end == events[3]->start,
                "previous END == next START was nudged");
        Require(events[2]->data.play_voice->a == 1 &&
                    std::string(events[2]->data.play_voice->character_short) == "mak",
                "normal PlayVoice fields");
    }
    {
        Fixture fixture(
            "[PlayVoice]=00:00:01\tVoice02/line\t\t\t00:01:00;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 1 && events[0]->command == PLAY_VOICE,
                "empty PlayVoice was discarded");
        Require(events[0]->data.play_voice->a == 0 &&
                    std::string(events[0]->data.play_voice->character_short).empty(),
                "empty numeric/key not preserved");
    }
    {
        Fixture fixture(
            "[PlayVoice]=00:00:01\tVoice02/line\t\t00:01:00;\n");
        Require(Collect(fixture.events).empty(),
                "missing PlayVoice field was treated as empty");
    }
    {
        Fixture fixture(
            "[PrintText]=02:52:05\tMakoto\tI know; me too.\t02:56:09;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 1 && events[0]->command == PRINT_TEXT &&
                    std::string(events[0]->data.print_text->text) ==
                        "I know; me too.",
                "semicolon in dialogue truncated the event");
    }
    {
        Fixture fixture(
            "[SetSELECT]=00:00:00\t'First'\tnull\t00:05:00;\n"
            "[SetSELECT]=00:05:00\t'Second'\tNULL\t00:10:00;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 2, "SetSELECT event count");
        Require(events[0]->data.set_select->size == 1 &&
                    events[1]->data.set_select->size == 1,
                "SetSELECT null/NULL became visible options");
    }
    {
        Fixture fixture(
            "[SetSELECT]=00:00:00\t'First'\tnullish\t00:05:00;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 1 &&
                    events[0]->data.set_select->size == 2,
                "SetSELECT treated a null prefix as null");
    }
    {
        Fixture fixture(
            "[MoveSom]=00:35:08\t1\t00:41:16;\n"
            "[MoveSom]=00:41:16\t2\t00:46:16;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 2 && events[0]->command == MOVE_SOM &&
                    events[1]->command == MOVE_SOM,
                "MoveSom not recognized");
        Require(events[0]->data.move_som->numeric == 1 &&
                    events[1]->data.move_som->numeric == 2 &&
                    events[0]->end == events[1]->start,
                "MoveSom payload/timestamps");
    }
    {
        Fixture fixture(
            "[PlaySe]=00:00:00, 1, BGM/Vocal/SDV02, 02:04:20;\n"
            "[PlayMovie]=00:00:00, System/OP/SDHQ_KOTONOHA, 0, 02:04:20;\n"
            "[PrintText]=00:16:19\tTaisuke\tText\t\t00:24:01;\n");
        Require(Collect(fixture.events).empty(),
                "malformed lines created events");
    }
    {
        Fixture fixture(
            "[SkipFRAME]=01:02:09;\n[Next]=01:09:00;\n");
        auto events = Collect(fixture.events);
        Require(events.size() == 2 && events[0]->command == SkipFRAME &&
                    events[1]->command == Next &&
                    events[0]->start != events[1]->start,
                "SkipFRAME/Next collapsed");
    }
}

int Audit(const fs::path& root) {
    Require(fs::is_directory(root), "ORS corpus root is missing");
    std::uint64_t files = 0, lines = 0, accepted = 0, unknown = 0;
    std::uint64_t moveSom = 0, emptyVoice = 0;
    std::map<std::string, std::uint64_t> rejectedByFile;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ORS")
            continue;
        ++files;
        std::ifstream input(entry.path(), std::ios::binary);
        std::string line;
        std::uint64_t fileLines = 0;
        while (std::getline(input, line)) {
            if (!line.empty() && line.front() == '[') ++fileLines;
        }
        lines += fileLines;
        auto parsed = Kotonoha_OrsParser(entry.path().string().c_str());
        accepted += parsed.size;
        for (auto* event = parsed.data; event; event = event->next) {
            if (event->command == UNKNOWN) ++unknown;
            if (event->command == MOVE_SOM) ++moveSom;
            if (event->command == PLAY_VOICE &&
                event->data.play_voice->a == 0 &&
                event->data.play_voice->character_short != nullptr &&
                event->data.play_voice->character_short[0] == '\0')
                ++emptyVoice;
        }
        if (fileLines != parsed.size)
            rejectedByFile[entry.path().filename().string()] =
                fileLines - parsed.size;
        Kotonoha_OrsClean(&parsed);
    }
    const bool referenceMatch =
        files == 1857 && lines == 86539 && accepted == 86536 &&
        moveSom == 281 && emptyVoice == 149 && unknown == 0 &&
        rejectedByFile == std::map<std::string, std::uint64_t>{
            {"03-KB-D10.ENG.ORS", 1},
            {"05-KI-OP1.ENG.ORS", 2}};
    std::cout << "{\n  \"files\": " << files
              << ",\n  \"event_lines\": " << lines
              << ",\n  \"events_accepted\": " << accepted
              << ",\n  \"valid_standard\": " << (accepted - emptyVoice)
              << ",\n  \"valid_optional_empty\": " << emptyVoice
              << ",\n  \"malformed_or_noop\": " << (lines - accepted)
              << ",\n  \"move_som\": " << moveSom
              << ",\n  \"play_voice_empty_numeric_key\": " << emptyVoice
              << ",\n  \"reference_v102_match\": "
              << (referenceMatch ? "true" : "false")
              << ",\n  \"rejected_by_file\": {";
    bool first = true;
    for (const auto& [file, count] : rejectedByFile) {
        std::cout << (first ? "\n" : ",\n") << "    \"" << file
                  << "\": " << count;
        first = false;
    }
    std::cout << (first ? "" : "\n") << "  }\n}\n";
    return referenceMatch ? 0 : 2;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--audit")
            return Audit(argv[2]);
        Require(argc == 1, "usage: OrsParserFidelityTest [--audit ORS_ROOT]");
        RunFixtures();
        std::cout << "OrsParserFidelityTest PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OrsParserFidelityTest FAIL: " << error.what() << '\n';
        return 1;
    }
}
