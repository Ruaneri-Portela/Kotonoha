#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"GATE5 AUDIT FAIL: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()

    main_cpp = (root / "src" / "Main.cpp").read_text(encoding="utf-8")
    controller_cpp = (root / "src" / "SchoolDaysKtrfAppController.cpp").read_text(encoding="utf-8")
    controller_hpp = (root / "include" / "Kotonoha" / "routing" /
                      "SchoolDaysKtrfAppController.hpp").read_text(encoding="utf-8")
    legacy_cpp = (root / "src" / "Kotonoha.cpp").read_text(encoding="utf-8")

    require('std::strcmp(argv[i], "-K")' in main_cpp,
            "Main.cpp does not recognize explicit -K launch mode")
    require("-K <route.ktnroute> <ors-root>" in main_cpp,
            "KTRF launch contract is not documented in the runtime error")
    require('std::strcmp(argv[i], "-l") == 0' in main_cpp,
            "KTRF mode does not reject preloaded -l gameplays")
    require("ktrfController->Open" in main_cpp,
            "SDL app init does not open the KTRF controller")
    require("? ktrfController->Main(&inRunning)" in main_cpp and
            ": app->Main(&inRunning);" in main_cpp,
            "SDL iterate is not an exclusive KTRF/legacy dispatch")
    require(main_cpp.index("ktrfController.reset();\n\t\tdelete GetApp(appstate);") >= 0,
            "KTRF controller is not destroyed before the legacy engine")

    require("engine->DeleteGameplay(gameplay);" in controller_cpp,
            "controller bypasses Kotonoha::DeleteGameplay ownership cleanup")
    require("new Gameplay(orsPath, context)" in controller_cpp,
            "controller does not construct Gameplay from resolved ORS")
    require('bridge.Advance("ktrf:next"' in controller_cpp,
            "controller does not route through the KTRF gameplay bridge")
    require("bridge.CommitChoice" in controller_cpp,
            "controller does not commit gameplay prompt choices to KTRF")
    require("bridge.HasPendingHandoff()" in controller_cpp,
            "controller does not preserve callback_38/terminal handoffs")
    require("current->scriptPath != expectedScene.orsPath" in controller_cpp,
            "controller does not verify Gameplay/route scene identity")

    require("SchoolDaysRouter" not in controller_cpp,
            "new KTRF controller depends on legacy SchoolDaysRouter")
    require("sceneIndex" not in controller_cpp,
            "new KTRF controller depends on legacy sceneIndex")
    require("SchoolDaysRouter" not in controller_hpp,
            "new KTRF controller header depends on legacy SchoolDaysRouter")

    # The legacy path is intentionally retained for generic/old launch mode.
    # The gate only requires that the KTRF path bypass it completely.
    require("SDL_AppResult Kotonoha::Main(Gameplay** out)" in legacy_cpp,
            "legacy Kotonoha::Main unexpectedly disappeared")

    print("SCHOOL DAYS KTRF ENGINE CUTOVER AUDIT PASS")
    print("launch_mode=-K")
    print("legacy_main_preserved=PASS")
    print("exclusive_runtime_dispatch=PASS")
    print("owner_aware_gameplay_destroy=PASS")
    print("legacy_router_dependency_in_ktrf_controller=0")
    print("legacy_scene_index_dependency_in_ktrf_controller=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
