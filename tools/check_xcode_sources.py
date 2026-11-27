#!/usr/bin/env python3
"""Check Xcode navigator groups and macOS/iOS source membership against Git.

Run from any directory: python3 tools/check_xcode_sources.py
No Xcode installation or third-party Python modules are required.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj/project.pbxproj"
ID = r"[0-9A-F]{24}"


def section(project: str, name: str) -> str:
    return project.split(f"/* Begin {name} section */", 1)[1].split(
        f"/* End {name} section */", 1
    )[0]


def objects(project: str, name: str) -> dict[str, str]:
    expression = rf"(?ms)^\s*({ID})(?: /\* [^*]+ \*/)? = \{{(.*?)\}};"
    return dict(re.findall(expression, section(project, name)))


def ids_in(field: str, body: str) -> set[str]:
    match = re.search(rf"\b{field} = \((.*?)\);", body, re.S)
    return set(re.findall(ID, match.group(1))) if match else set()


def main() -> int:
    errors: list[str] = []
    project = PROJECT.read_text(encoding="utf-8")
    # Xcode reports an unreadable project if a nested buildSettings dictionary
    # lacks '};'. Checking only PBX file IDs never catches this regression.
    if sys.platform == "darwin":
        lint = subprocess.run(
            ["plutil", "-lint", "--", str(PROJECT)],
            capture_output=True, text=True,
        )
        if lint.returncode:
            errors.append("Invalid Xcode project syntax: "
                          + (lint.stderr.strip() or lint.stdout.strip()))
    if re.search(r"(?m)^(?:<<<<<<< |=======\s*$|>>>>>>> )", project):
        errors.append("Unresolved source-control conflict in project.pbxproj")
    ids = re.findall(rf"(?m)^\s*({ID}) /\* [^*]+ \*/ = \{{", project)
    if len(ids) != len(set(ids)):
        errors.append("Duplicate PBX object IDs in project.pbxproj")
    for name in ("D7D700000000000000000006",
                 "D7D700000000000000000007"):
        # The launcher iOS Debug/Release configurations must have their own
        # closed buildSettings dictionary and a name outside that dictionary.
        match = re.search(
            rf"(?ms)^\s*{name} /\* (Debug|Release) \*/ = \{{"
            rf"(.*?)^\t\t\}};", project,
        )
        if not match or not re.search(
            r"buildSettings = \{[\s\S]*?\n\s*\};\n\s*name = (Debug|Release);",
            match.group(2),
        ):
            errors.append(f"Malformed iOS launcher build configuration: {name}")
    groups = objects(project, "PBXGroup")
    file_refs = objects(project, "PBXFileReference")
    build_refs = objects(project, "PBXBuildFile")
    phases = objects(project, "PBXSourcesBuildPhase")
    targets = objects(project, "PBXNativeTarget")

    owners: dict[str, list[str]] = {}
    for group_id, body in groups.items():
        for member in ids_in("children", body):
            owners.setdefault(member, []).append(group_id)
    for file_id in file_refs:
        if len(owners.get(file_id, [])) != 1:
            errors.append(f"File reference {file_id} has {len(owners.get(file_id, []))} navigator parents")
    root = "42F30634C2CFADA428E6AA3C"
    for item in ids_in("children", groups[root]):
        if item in file_refs:
            errors.append(f"Source/reference left in Xcode root: {item}")

    file_paths: dict[str, Path] = {}
    for ref_id, body in file_refs.items():
        path = re.search(r'\bpath = (?:"([^"]+)"|([^;]+));', body)
        if not path or "sourceTree = SOURCE_ROOT" not in body:
            continue
        raw = path.group(1) or path.group(2)
        file_paths[ref_id] = (PROJECT.parent.parent / raw).resolve()

    build_to_ref = {}
    for build_id, body in build_refs.items():
        match = re.search(rf"\bfileRef = ({ID})", body)
        if match:
            build_to_ref[build_id] = match.group(1)
            if match.group(1) not in file_refs:
                errors.append(f"Build file {build_id} has missing file reference")

    def target_sources(name: str) -> set[Path]:
        for body in targets.values():
            if re.search(rf'\bname = "?{re.escape(name)}"?;', body):
                phase_ids = ids_in("buildPhases", body)
                included = set()
                for pid in phase_ids:
                    if pid in phases:
                        included.update(
                            file_paths.get(build_to_ref.get(bid, ""))
                            for bid in ids_in("files", phases[pid])
                        )
                return included
        errors.append(f"Missing Xcode target: {name}")
        return set()

    mac = target_sources("KotonohaEngine-macOS")
    ios = target_sources("KotonohaEngine-iOS")
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "lib/src"],
        capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    engine_sources = {
        (ROOT / relative).resolve()
        for relative in tracked
        if relative.endswith((".cpp", ".mm"))
        and not relative.startswith("lib/src/integrations/")  # Optional game-linked targets
    }
    for source in sorted(engine_sources):
        if source not in mac:
            errors.append(f"Engine macOS missing: {source.relative_to(ROOT)}")
        if source not in ios:
            errors.append(f"Engine iOS missing: {source.relative_to(ROOT)}")

    for name, game in (
        ("DokiDoki", "doki-doki-literature-club"),
        ("KatawaShoujo", "katawa-shoujo"),
        ("MEO", "meo"),
        ("PonscripterGame", "ponscripter"),
        ("N2SystemGame", "n2system"),
    ):
        app_main = (ROOT / "games" / game / "src/main.cpp").resolve()
        for platform in ("macOS", "iOS"):
            if app_main not in target_sources(f"{name}-{platform}"):
                errors.append(f"{name}-{platform} does not compile its own main.cpp")

    for platform in ("macOS", "iOS"):
        for name in ("DokiDoki", "KatawaShoujo"):
            target = target_sources(f"{name}-{platform}")
            for rel in (
                "modules/vms/renpy/src/Python.cpp",
                "modules/vms/renpy/src/Vm.cpp",
                "modules/providers/rpa/src/DdlcAssets.cpp",
            ):
                if (ROOT / rel).resolve() not in target:
                    errors.append(f"{name}-{platform} missing shared RenPy source: {rel}")

    # Check that this is a native PBXNativeTarget, not an aggregate
    # target invoking CMake, and that it compiles one copy of each game source.
    launcher = target_sources("KotonohaLauncher-macOS")
    launcher_main = (ROOT / "launcher/src/main.cpp").resolve()
    if launcher_main not in launcher:
        errors.append("Native launcher does not compile its own main.cpp")
    if "PBXAggregateTarget" in project or "BuildUnifiedLauncher.sh" in project:
        errors.append("Obsolete CMake aggregate launcher still appears in Xcode")
    for name in ("GameFactories.cpp", "UnifiedLauncher.cpp"):
        if (ROOT / "launcher/src" / name).resolve() not in launcher:
            errors.append(f"Native launcher missing {name}")
    for filename in ("lib/src/media/SurfaceTexture.cpp",
                     "lib/src/filesystem/AtomicFile.cpp"):
        path = (ROOT / filename).resolve()
        if path not in mac or path not in ios:
            errors.append(f"Engine is missing {filename} on Apple platforms")
    for platform in ("macOS", "iOS"):
        school = target_sources(f"SchoolDays-{platform}")
        if (ROOT / "games/school-days/src/SchoolDaysAtomicFile.cpp").resolve() not in school:
            errors.append(f"SchoolDays-{platform} missing SchoolDaysAtomicFile.cpp")

    # A native multi-game target must include each platform game's library
    # sources without compiling their independent SDL entrypoints.
    all_sources = set()
    for name in (
        "Amagami", "ZeroNoTsukaima", "SnowRadish", "SummerRadish",
        "DokiDoki", "KatawaShoujo", "MEO", "SchoolDays", "RealLive",
        "Kirikiri", "LucaSystem", "Pmail", "N2SystemGame",
        "PonscripterGame",
    ):
        for source in target_sources(f"{name}-macOS"):
            if source is not None and source.name != "main.cpp":
                all_sources.add(source)
    for source in sorted(all_sources):
        if source not in launcher:
            errors.append(f"Native launcher missing game source: {source.relative_to(ROOT)}")
    for source in launcher:
        if source is not None and source.name == "main.cpp" and source != launcher_main:
            errors.append(f"Native launcher has another game's main.cpp: {source}")
    if any(source is None for source in launcher):
        errors.append("Native launcher contains unresolved source file references")

    # All native target configurations should inherit the shared header roots.
    common_config = ROOT / "buildsystems/xcode/Config/Common.xcconfig"
    common_text = common_config.read_text(encoding="utf-8")
    config_lists = objects(project, "XCConfigurationList")
    configurations = objects(project, "XCBuildConfiguration")
    source_roots = set()
    for source in all_sources | engine_sources:
        relative = source.relative_to(ROOT)
        parts = relative.parts
        if len(parts) >= 4 and parts[0] == "modules":
            source_roots.add("/".join(parts[:3]))
        if len(parts) >= 3 and parts[0] == "games":
            source_roots.add("/".join(parts[:2]))
    for root_dir in sorted(source_roots):
        for suffix in ("include", "src"):
            token = "$(KOTONOHA_ROOT)/" + root_dir + "/" + suffix
            if token not in common_text:
                errors.append(f"Common.xcconfig is missing header root: {token}")

    configs_by_id = {}
    for config_id, body in configurations.items():
        match = re.search(rf"baseConfigurationReference = ({ID})", body)
        if match:
            configs_by_id[config_id] = match.group(1)
    configs_by_list = {
        list_id: ids_in("buildConfigurations", body)
        for list_id, body in config_lists.items()
    }
    for target in targets.values():
        name_match = re.search(r'\bname = "?([^";]+)"?;', target)
        list_match = re.search(rf"buildConfigurationList = ({ID})", target)
        if not name_match or not list_match:
            continue
        for config_id in configs_by_list.get(list_match.group(1), set()):
            file_id = configs_by_id.get(config_id)
            if not file_id or file_id not in file_paths:
                errors.append(f"Missing xcconfig for {name_match.group(1)}: {config_id}")
                continue
            config_file = file_paths[file_id]
            if not config_file.exists():
                errors.append(f"Missing config file: {config_file}")
                continue
            contents = config_file.read_text(encoding="utf-8")
            if (config_file.name != "Common.xcconfig"
                    and "#include" not in contents):
                errors.append(f"Target config does not include shared settings: {config_file}")


    if errors:
        for issue in errors:
            print("ERROR:", issue, file=sys.stderr)
        return 1
    print(f"OK: {len(engine_sources)} engine sources in both Apple targets")
    print(f"OK: {len(file_refs)} file references have exactly one group")
    print("OK: DDLC, Katawa, MEO, Ponscripter, N2System and launcher targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
