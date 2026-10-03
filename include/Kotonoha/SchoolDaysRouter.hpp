#pragma once

#include <functional>
#include <map>
#include <string>

namespace Kotonoha {

struct SceneKey {
	std::string value;

	bool operator<(const SceneKey& other) const { return value < other.value; }
	bool operator==(const SceneKey& other) const { return value == other.value; }

	// Only a script in a numbered route directory with a matching ORS basename
	// is eligible for the School Days scene index.
	static SceneKey FromScriptPath(const std::string& path);
};

struct SchoolDaysRouteState {
	int route = 0;
	int scene = 0;
	int choiceResult = -2;
	std::map<std::string, int> sessionVariables{ { "001", 0 }, { "002", 0 } };
	std::map<std::string, int> globalVariables;
	bool feelingApplied = false;
};

class SchoolDaysRouter {
public:
	enum class NextKind { Advanced, Route0Exit, Unresolved };
	struct NextResult {
		NextKind kind;
		SceneKey destination;
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
	void WriteSessionVariable(const std::string& name, int value);
	void AddFeeling(const std::string& name, int delta);
};

} // namespace Kotonoha
