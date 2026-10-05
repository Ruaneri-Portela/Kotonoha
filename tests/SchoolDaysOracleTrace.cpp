#include <Kotonoha/SchoolDaysRouter.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using Kotonoha::SchoolDaysRouter;

namespace {

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                static const char* hex = "0123456789abcdef";
                out << "\\u00" << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

void WriteString(std::ostream& out, const std::string& value) {
    out << '"' << JsonEscape(value) << '"';
}

void WriteStringArray(std::ostream& out, const std::vector<std::string>& values) {
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        WriteString(out, values[i]);
    }
    out << ']';
}

void WriteIntArray(std::ostream& out, const std::vector<int>& values) {
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
    }
    out << ']';
}

void WriteIntMap(std::ostream& out, const std::map<std::string, int>& values) {
    out << '{';
    bool first = true;
    for (const auto& entry : values) {
        if (!first) out << ',';
        first = false;
        WriteString(out, entry.first);
        out << ':' << entry.second;
    }
    out << '}';
}

const char* KindName(SchoolDaysRouter::NextKind kind) {
    switch (kind) {
    case SchoolDaysRouter::NextKind::Advanced: return "advanced";
    case SchoolDaysRouter::NextKind::Terminal: return "terminal";
    case SchoolDaysRouter::NextKind::Unresolved: return "unresolved";
    }
    return "unknown";
}

[[noreturn]] void Fail(int step, const std::string& message) {
    std::cerr << "SchoolDaysOracleTrace step " << step << ": " << message << '\n';
    std::exit(2);
}

void EmitResult(
    SchoolDaysRouter& router,
    const SchoolDaysRouter::NextResult& result,
    int stepIndex
) {
    const auto& state = router.State();
    const std::string currentScene = router.CurrentScene().value;

    std::cout << '{';
    std::cout << "\"step\":" << stepIndex << ',';
    std::cout << "\"transition\":" << result.transitionId << ',';
    std::cout << "\"kind\":";
    WriteString(std::cout, KindName(result.kind));
    std::cout << ',';
    std::cout << "\"destination\":";
    WriteString(std::cout, result.destination.value);
    std::cout << ',';
    std::cout << "\"current_scene\":";
    WriteString(std::cout, currentScene);
    std::cout << ',';
    std::cout << "\"route\":" << state.route << ',';
    std::cout << "\"scene\":" << state.scene << ',';
    std::cout << "\"choice_result\":" << state.choiceResult << ',';
    std::cout << "\"callback34\":" << state.callback34 << ',';
    std::cout << "\"feeling_applied\":" << (state.feelingApplied ? "true" : "false") << ',';
    std::cout << "\"ending_id\":" << result.endingId << ',';
    std::cout << "\"endings\":";
    WriteIntArray(std::cout, state.endingRegistrations);
    std::cout << ',';
    std::cout << "\"callbacks\":";
    WriteStringArray(std::cout, result.callbacks);
    std::cout << ',';
    std::cout << "\"session\":";
    WriteIntMap(std::cout, state.sessionVariables);
    std::cout << ',';
    std::cout << "\"global\":";
    WriteIntMap(std::cout, state.globalVariables);
    std::cout << "}\n";
}

SchoolDaysRouter::NextResult ResolveExpected(
    SchoolDaysRouter& router,
    int expectedTransition,
    int stepIndex
) {
    const auto result = router.ResolveNext();
    if (result.transitionId != expectedTransition) {
        std::ostringstream error;
        error << "transition mismatch expected=t" << expectedTransition
              << " actual=t" << result.transitionId;
        Fail(stepIndex, error.str());
    }
    return result;
}

} // namespace

int main() {
    SchoolDaysRouter router;
    std::string line;
    int stepIndex = 0;

    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "RESET") {
            router.Reset();
            stepIndex = 0;
            continue;
        }

        if (command == "INJECT") {
            int route = -1;
            int scene = -1;
            int choiceResult = -2;
            int callback34 = 0;
            int feelingApplied = 0;
            if (!(input >> route >> scene >> choiceResult >> callback34 >> feelingApplied)) {
                Fail(stepIndex, "malformed INJECT line: " + line);
            }
#ifdef KOTONOHA_ROUTER_TEST_ACCESS
            auto& state = router.MutableStateForTest();
            state.route = route;
            state.scene = scene;
            state.choiceResult = choiceResult;
            state.callback34 = callback34;
            state.feelingApplied = feelingApplied != 0;
            state.endingRegistrations.clear();
            state.sessionVariables["ROUTE"] = route;
            state.sessionVariables["SCENE"] = scene;
#else
            Fail(stepIndex, "INJECT unavailable without KOTONOHA_ROUTER_TEST_ACCESS");
#endif
            continue;
        }

        if (command == "SESSION") {
            std::string name;
            int value = 0;
            if (!(input >> name >> value)) {
                Fail(stepIndex, "malformed SESSION line: " + line);
            }
#ifdef KOTONOHA_ROUTER_TEST_ACCESS
            router.MutableStateForTest().sessionVariables[name] = value;
#else
            Fail(stepIndex, "SESSION unavailable without KOTONOHA_ROUTER_TEST_ACCESS");
#endif
            continue;
        }

        if (command == "GLOBAL") {
            std::string name;
            int value = 0;
            if (!(input >> name >> value)) {
                Fail(stepIndex, "malformed GLOBAL line: " + line);
            }
#ifdef KOTONOHA_ROUTER_TEST_ACCESS
            router.MutableStateForTest().globalVariables[name] = value;
#else
            Fail(stepIndex, "GLOBAL unavailable without KOTONOHA_ROUTER_TEST_ACCESS");
#endif
            continue;
        }

        if (command == "RESOLVE") {
            int expectedTransition = -1;
            if (!(input >> expectedTransition)) {
                Fail(stepIndex, "malformed RESOLVE line: " + line);
            }
            const auto result = ResolveExpected(router, expectedTransition, stepIndex);
            EmitResult(router, result, stepIndex);
            ++stepIndex;
            continue;
        }

        if (command != "STEP") {
            Fail(stepIndex, "unknown command: " + command);
        }

        int expectedRoute = -1;
        int expectedScene = -1;
        int choice = -99;
        int expectedTransition = -1;
        if (!(input >> expectedRoute >> expectedScene >> choice >> expectedTransition)) {
            Fail(stepIndex, "malformed STEP line: " + line);
        }

        const auto& before = router.State();
        if (before.route != expectedRoute || before.scene != expectedScene) {
            std::ostringstream error;
            error << "pre-state mismatch expected=" << expectedRoute << '/' << expectedScene
                  << " actual=" << before.route << '/' << before.scene;
            Fail(stepIndex, error.str());
        }

        if (choice != -99 && !router.AcceptChoice(choice)) {
            Fail(stepIndex, "AcceptChoice rejected value " + std::to_string(choice));
        }

        const auto result = ResolveExpected(router, expectedTransition, stepIndex);
        EmitResult(router, result, stepIndex);
        ++stepIndex;
    }

    return 0;
}
