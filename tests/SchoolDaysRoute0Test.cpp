#include <Kotonoha/SchoolDaysRouter.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

using Kotonoha::SchoolDaysRouter;

static void Require(bool condition, const std::string& message) {
	if (!condition) throw std::runtime_error(message);
}

static int Variable(const SchoolDaysRouter& router, const std::string& name) {
	const auto& values = router.State().sessionVariables;
	const auto found = values.find(name);
	Require(found != values.end(), "missing variable " + name);
	return found->second;
}

static void RunPath(int first, int second, int feeling001, int feeling002,
	int predecessorB, int predecessorH) {
	SchoolDaysRouter router;
	Require(router.State().route == 0 && router.State().scene == 0 &&
		router.State().choiceResult == -2, "wrong initial state");
	Require(router.State().sessionVariables.count("BS0000B00") == 0 &&
		router.State().sessionVariables.count("BS0000H02") == 0,
		"BS variables must be created on transition");
	Require(Kotonoha::SceneKey::FromScriptPath("assets/00/00-00-A04.ENG.ORS").value ==
		"00/00-00-A04", "scene key path mapping");

	for (int steps = 0; steps < 25; ++steps) {
		const std::string scene = router.CurrentScene().value;
		if (scene == "00/00-00-A03" || scene == "00/00-00-H00") {
			const int choice = scene == "00/00-00-A03" ? first : second;
			Require(router.ResolveNext().kind == SchoolDaysRouter::NextKind::Unresolved,
				"pending choice must block Next");
			Require(router.AcceptChoice(choice), "choice not accepted");
			Require(router.AcceptChoice(choice), "repeated choice not idempotent");
			Require(!router.AcceptChoice(choice == 0 ? -1 : 0),
				"resolved choice changed");
		}
		const auto next = router.ResolveNext();
		if (scene == "00/00-00-L00") {
			Require(next.kind == SchoolDaysRouter::NextKind::Route0Exit &&
				next.destination.value == "01/01-00-A00", "Route 0 exit");
			Require(router.State().route == 1 && router.State().scene == 0,
				"exit route/scene state");
			Require(Variable(router, "001") == feeling001, "001 mismatch");
			Require(Variable(router, "002") == feeling002, "002 mismatch");
			Require(Variable(router, "BS0000B00") == predecessorB, "BS0000B00 mismatch");
			Require(Variable(router, "BS0000H02") == predecessorH, "BS0000H02 mismatch");
			std::cout << "PASS A03=" << first << " H00=" << second
				<< " 001=" << Variable(router, "001")
				<< " 002=" << Variable(router, "002")
				<< " BS0000B00=" << Variable(router, "BS0000B00")
				<< " BS0000H02=" << Variable(router, "BS0000H02") << '\n';
			return;
		}
		Require(next.kind == SchoolDaysRouter::NextKind::Advanced,
			"normal transition failed at " + scene);
	}
	throw std::runtime_error("Route 0 did not exit within 25 scenes");
}

int main() {
	try {
		RunPath(0, 0, 0, 9, 4, 15);
		RunPath(0, -1, 0, 5, 4, 14);
		RunPath(1, 0, 5, 4, 7, 15);
		RunPath(1, -1, 5, 0, 7, 14);
		RunPath(-1, 0, 0, 4, 6, 15);
		RunPath(-1, -1, 0, 0, 6, 14);
	} catch (const std::exception& error) {
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
	return 0;
}
