#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Kotonoha {

struct SceneKey {
    std::string value;

    bool operator<(const SceneKey& other) const { return value < other.value; }
    bool operator==(const SceneKey& other) const { return value == other.value; }

    static SceneKey FromScriptPath(const std::string& path);
};

struct SchoolDaysRouteState {
    int route = 0;
    int scene = 0;
    int choiceResult = -2;
    std::map<std::string, int> sessionVariables;
    std::map<std::string, int> globalVariables;
    int callback34 = 0;
    bool feelingApplied = false;
    std::vector<int> endingRegistrations;
};

class SchoolDaysRouter {
public:
    enum class NextKind { Advanced, Terminal, Unresolved };
    struct NextResult {
        NextKind kind = NextKind::Unresolved;
        SceneKey destination;
        int transitionId = -1;
        int endingId = -1;
    };

    explicit SchoolDaysRouter(std::function<void(const std::string&)> logger = {});
    void Reset();
    const SchoolDaysRouteState& State() const { return state; }
    SceneKey CurrentScene() const;
    bool HasChoice() const;
    bool AcceptChoice(int result);
    NextResult ResolveNext();

private:
    SchoolDaysRouteState state;
    std::function<void(const std::string&)> log;

    int CurrentNodeIndex() const;
    int ReadSession(const char* name) const;
    int ReadGlobal(const char* name) const;
    void WriteSession(const char* name, int value);
    void WriteGlobal(const char* name, int value);
    void AddFeeling(const char* name, int delta);
};

} // namespace Kotonoha
