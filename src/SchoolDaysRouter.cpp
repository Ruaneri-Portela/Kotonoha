#include <Kotonoha/SchoolDaysRouter.hpp>

#include <array>
#include <cctype>
#include <utility>

namespace Kotonoha {
namespace {
	// Route 0 table indices from the routing contract. Vector positions are never
	// used to select a narrative successor.
	constexpr std::array<const char*, 21> kRoute0Scenes = {
		"00/00-00-A00", "00/00-00-A01", "00/00-00-A02", "00/00-00-A03",
		"00/00-00-A04", "00/00-00-A05", "00/00-00-A06", "00/00-00-A07",
		"00/00-00-B00", "00/00-00-B01", "00/00-00-C00", "00/00-00-D00",
		"00/00-00-F00", "00/00-00-G00", "00/00-00-H00", "00/00-00-H01",
		"00/00-00-H02", "00/00-00-I00", "00/00-00-J00", "00/00-00-K00",
		"00/00-00-L00"
	};
}

SceneKey SceneKey::FromScriptPath(const std::string& path) {
	const size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos || slash < 2) return {};
	const std::string directory = path.substr(0, slash);
	const size_t parentSlash = directory.find_last_of("/\\");
	const std::string group = directory.substr(parentSlash == std::string::npos ? 0 : parentSlash + 1);
	if (group.size() != 2 || !std::isdigit(static_cast<unsigned char>(group[0])) ||
		!std::isdigit(static_cast<unsigned char>(group[1]))) return {};
	const std::string filename = path.substr(slash + 1);
	constexpr const char* extension = ".ENG.ORS";
	if (filename.size() <= 8 || filename.substr(filename.size() - 8) != extension ||
		filename.substr(0, 2) != group || filename[2] != '-') return {};
	return { group + "/" + filename.substr(0, filename.size() - 8) };
}

SchoolDaysRouter::SchoolDaysRouter(std::function<void(const std::string&)> logger)
	: log(std::move(logger)) { Reset(); }

void SchoolDaysRouter::Reset() {
	state = SchoolDaysRouteState{};
}

SceneKey SchoolDaysRouter::CurrentScene() const {
	if (state.route != 0 || state.scene < 0 ||
		state.scene >= static_cast<int>(kRoute0Scenes.size())) return {};
	return { kRoute0Scenes[state.scene] };
}

bool SchoolDaysRouter::HasChoice() const {
	return state.route == 0 && (state.scene == 3 || state.scene == 14);
}

void SchoolDaysRouter::WriteSessionVariable(const std::string& name, int value) {
	state.sessionVariables[name] = value;
	if (log) log(name + "=" + std::to_string(value));
}

void SchoolDaysRouter::AddFeeling(const std::string& name, int delta) {
	const int value = state.sessionVariables[name] + delta;
	state.sessionVariables[name] = value;
	if (log) log("feeling " + name + " += " + std::to_string(delta) + " -> " + std::to_string(value));
}

bool SchoolDaysRouter::AcceptChoice(int result) {
	if (!HasChoice() || result < -1 || result > 1 || (state.scene == 14 && result == 1))
		return false;
	if (state.choiceResult != -2) return state.choiceResult == result;
	state.choiceResult = result;
	if (log) log("scene=" + CurrentScene().value + " choice=" + std::to_string(result));
	if (!state.feelingApplied) {
		if (state.scene == 3 && result == 0) AddFeeling("002", 5);
		if (state.scene == 3 && result == 1) AddFeeling("001", 5);
		if (state.scene == 14 && result == 0) AddFeeling("002", 4);
		state.feelingApplied = true;
	}
	return true;
}

SchoolDaysRouter::NextResult SchoolDaysRouter::ResolveNext() {
	const SceneKey source = CurrentScene();
	if (source.value.empty() || (HasChoice() && state.choiceResult == -2)) {
		if (log) log("next blocked scene=" + source.value + " choice=" + std::to_string(state.choiceResult));
		return { NextKind::Unresolved, {} };
	}

	int target = -1;
	switch (state.scene) {
	case 0: target = 1; break;
	case 1: target = 2; break;
	case 2: target = 3; break;
	case 3:
		target = state.choiceResult == 0 ? 4 : state.choiceResult == 1 ? 5 : 6;
		break;
	case 4: WriteSessionVariable("BS0000B00", 4); target = 8; break;
	case 5: target = 7; break;
	case 6: WriteSessionVariable("BS0000B00", 6); target = 8; break;
	case 7: WriteSessionVariable("BS0000B00", 7); target = 8; break;
	case 8: target = 9; break;
	case 9: target = 10; break;
	case 10: target = 11; break;
	case 11: target = 12; break;
	case 12: target = 13; break;
	case 13: target = 14; break;
	case 14:
		if (state.choiceResult == 0) target = 15;
		else { WriteSessionVariable("BS0000H02", 14); target = 16; }
		break;
	case 15: WriteSessionVariable("BS0000H02", 15); target = 16; break;
	case 16: target = 17; break;
	case 17: target = 18; break;
	case 18: target = 19; break;
	case 19: target = 20; break;
	case 20: {
		const SceneKey exit{ "01/01-00-A00" };
		if (log) log("Route 0 finished; next " + source.value + " -> " + exit.value);
		state.route = 1;
		state.scene = 0;
		state.choiceResult = -2;
		state.feelingApplied = false;
		return { NextKind::Route0Exit, exit };
	}
	default: break;
	}
	if (target < 0) return { NextKind::Unresolved, {} };
	state.scene = target;
	state.choiceResult = -2;
	state.feelingApplied = false;
	const SceneKey destination = CurrentScene();
	if (log) {
		log("next " + source.value + " -> " + destination.value);
		if (HasChoice()) log("scene=" + destination.value + " choice=pending(-2)");
	}
	return { NextKind::Advanced, destination };
}

} // namespace Kotonoha
