#include <Kotonoha/SchoolDaysRemainingOrs.hpp>

#include <algorithm>
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

const char* TypeName(Kotonoha_orsType type) {
    switch (type) {
    case CREATE_BG: return "CREATE_BG";
    case PLAY_SE: return "PLAY_SE";
    case PLAY_MOVIE: return "PLAY_MOVIE";
    case WHITE_FADE: return "WHITE_FADE";
    case BLACK_FADE: return "BLACK_FADE";
    case PLAY_BGM: return "PLAY_BGM";
    case PRINT_TEXT: return "PRINT_TEXT";
    case PLAY_VOICE: return "PLAY_VOICE";
    case SkipFRAME: return "SkipFRAME";
    case SetSELECT: return "SetSELECT";
    case END_BGM: return "END_BGM";
    case END_ROLL: return "END_ROLL";
    case Next: return "Next";
    case MOVE_SOM: return "MOVE_SOM";
    default: return "UNKNOWN";
    }
}

bool ResourceBearing(Kotonoha_orsType type) {
    switch (type) {
    case CREATE_BG: case PLAY_SE: case PLAY_MOVIE: case PLAY_BGM:
    case PLAY_VOICE: case END_BGM: case END_ROLL: return true;
    default: return false;
    }
}

void Quoted(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else out << c;
    }
    out << '"';
}

void Counts(std::ostream& out, const std::map<std::string, uint64_t>& counts) {
    out << '{';
    bool first = true;
    for (const auto& [key, value] : counts) {
        if (!first) out << ',';
        Quoted(out, key);
        out << ':' << value;
        first = false;
    }
    out << '}';
}

struct FileAudit {
    std::string scene;
    uint64_t eventLines = 0, accepted = 0, rejected = 0;
    uint64_t nextCount = 0, skipCount = 0, choices = 0;
    uint64_t nextTick = 0, skipTick = 0, resourceEvents = 0;
    bool choiceAtSkip = false;
    std::map<std::string, uint64_t> byType;
};

int Audit(const fs::path& root) {
    if (!fs::is_directory(root)) throw std::runtime_error("ORS root missing");
    std::vector<FileAudit> files;
    std::map<std::string, uint64_t> byType;
    uint64_t lines = 0, accepted = 0, rejected = 0, unknown = 0;
    uint64_t equal = 0, less = 0, greater = 0, markerErrors = 0;
    uint64_t choiceMarkerErrors = 0;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        if (!item.is_regular_file() || item.path().extension() != ".ORS") continue;
        FileAudit file;
        file.scene = fs::relative(item.path(), root).generic_string();
        std::ifstream raw(item.path(), std::ios::binary);
        std::string line;
        while (std::getline(raw, line))
            if (!line.empty() && line[0] == '[') ++file.eventLines;
        auto parsed = Kotonoha_OrsParser(item.path().string().c_str());
        file.accepted = parsed.size;
        if (file.eventLines < file.accepted)
            throw std::runtime_error("parser accepted more events than input lines");
        file.rejected = file.eventLines - file.accepted;
        for (auto* event = parsed.data; event; event = event->next) {
            const char* name = TypeName(event->command);
            ++file.byType[name];
            ++byType[name];
            if (event->command == UNKNOWN ||
                Kotonoha::ClassifySchoolDaysOrs(event->command) ==
                    Kotonoha::SchoolDaysOrsClass::Unknown) ++unknown;
            if (ResourceBearing(event->command)) ++file.resourceEvents;
            if (event->command == Next) {
                ++file.nextCount;
                file.nextTick = event->startTick;
            }
            if (event->command == SkipFRAME) {
                ++file.skipCount;
                file.skipTick = event->startTick;
            }
            if (event->command == SetSELECT) {
                ++file.choices;
            }
        }
        /* Resolve against the final marker value regardless of source order. */
        if (file.choices)
            for (auto* event = parsed.data; event; event = event->next)
                if (event->command == SetSELECT &&
                    event->startTick == file.skipTick) file.choiceAtSkip = true;
        if (file.nextCount != 1 || file.skipCount != 1) ++markerErrors;
        else if (file.skipTick == file.nextTick) ++equal;
        else if (file.skipTick < file.nextTick) ++less;
        else ++greater;
        if (file.skipTick < file.nextTick &&
            (file.choices != 1 || !file.choiceAtSkip)) ++choiceMarkerErrors;
        lines += file.eventLines;
        accepted += file.accepted;
        rejected += file.rejected;
        Kotonoha_OrsClean(&parsed);
        files.push_back(std::move(file));
    }
    std::sort(files.begin(), files.end(), [](const FileAudit& a, const FileAudit& b) {
        return a.scene < b.scene;
    });
    const std::map<std::string, uint64_t> expected = {
        {"BLACK_FADE",847},{"CREATE_BG",13024},{"END_BGM",67},
        {"END_ROLL",67},{"MOVE_SOM",281},{"Next",1857},
        {"PLAY_BGM",1488},{"PLAY_MOVIE",2041},{"PLAY_SE",3551},
        {"PLAY_VOICE",30413},{"PRINT_TEXT",30484},{"SetSELECT",287},
        {"SkipFRAME",1857},{"WHITE_FADE",272}
    };
    const bool pass = files.size() == 1857 && lines == 86539 &&
        accepted == 86536 && rejected == 3 && unknown == 0 &&
        byType == expected && markerErrors == 0 && greater == 0 &&
        equal == 1570 && less == 287 && choiceMarkerErrors == 0;
    std::cout << "{\"summary\":{\"files\":" << files.size()
              << ",\"event_lines\":" << lines
              << ",\"accepted\":" << accepted
              << ",\"rejected\":" << rejected
              << ",\"unknown\":" << unknown
              << ",\"skip_equals_next\":" << equal
              << ",\"skip_before_next\":" << less
              << ",\"skip_after_next\":" << greater
              << ",\"marker_errors\":" << markerErrors
              << ",\"choice_marker_errors\":" << choiceMarkerErrors
              << ",\"reference_v102_match\":" << (pass ? "true" : "false")
              << "},\"by_event_type\":";
    Counts(std::cout, byType);
    std::cout << ",\"files\":[";
    bool first = true;
    for (const auto& file : files) {
        if (!first) std::cout << ',';
        std::cout << "{\"scene\":";
        Quoted(std::cout, file.scene);
        std::cout << ",\"event_lines\":" << file.eventLines
                  << ",\"event_count\":" << file.accepted
                  << ",\"malformed_count\":" << file.rejected
                  << ",\"next_tick\":" << file.nextTick
                  << ",\"skipframe_tick\":" << file.skipTick
                  << ",\"choice_count\":" << file.choices
                  << ",\"choice_at_skipframe\":"
                  << (file.choiceAtSkip ? "true" : "false")
                  << ",\"resource_bearing_events\":" << file.resourceEvents
                  << ",\"event_types\":";
        Counts(std::cout, file.byType);
        std::cout << '}';
        first = false;
    }
    std::cout << "]}\n";
    return pass ? 0 : 2;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: SchoolDaysRuntimeCorpusAudit ASSETS_ROOT");
        return Audit(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "SchoolDaysRuntimeCorpusAudit: " << error.what() << '\n';
        return 1;
    }
}
