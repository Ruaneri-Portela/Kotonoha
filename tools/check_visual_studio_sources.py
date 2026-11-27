#!/usr/bin/env python3
"""Valida a solucao Visual Studio independente (Python, multiplataforma).

Nao exige MSBuild instalado: verifica a estrutura checked-in (.sln/.vcxproj),
a cobertura das fontes e o alinhamento com o ProjectManifest.json.
"""
from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VS = ROOT / "buildsystems" / "visualstudio"
MANIFEST = VS / "ProjectManifest.json"
SOLUTION = VS / "Kotonoha.sln"


def require(cond: bool, message: str) -> None:
    if not cond:
        raise ValueError(message)


def load_manifest() -> dict:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(data["project"] == "buildsystems/visualstudio/Kotonoha.sln", "Wrong VS project path")
    require(data["platforms"] == ["Win32", "x64", "ARM64"], "Missing Win32/x64/ARM64 platforms")
    require(data["configurations"] == ["Debug", "Release"], "Missing Debug/Release configurations")
    require(data["vcpkg_triplets"] == {
        "Win32": "x86-windows", "x64": "x64-windows", "ARM64": "arm64-windows"
    }, "Wrong vcpkg triplet mapping")
    require(data["preparation"]["script"] == "tools/PrepareVisualStudio.ps1", "Wrong preparation script")
    require((ROOT / "tools" / "PrepareVisualStudio.ps1").is_file(), "Missing PrepareVisualStudio.ps1")
    require(data["vcpkg_features"] == ["sdl-vcpkg", "imgui-vcpkg"], "Wrong vcpkg features")
    require(data["vcpkg_install_roots"] == {
        "x86": "buildsystems/visualstudio/vcpkg/x86",
        "x64": "buildsystems/visualstudio/vcpkg/x64",
        "arm64": "buildsystems/visualstudio/vcpkg/arm64",
    }, "Wrong per-architecture vcpkg install roots")
    return data


def glob_sources(pattern: str) -> set[Path]:
    """Expands an MSBuild Include pattern relative to the repository root."""
    rel = pattern.replace("$(RepoRoot)", "").replace("\\", "/").lstrip("/")
    if not rel or ":/// " in rel:
        return set()
    return {p.resolve() for p in ROOT.glob(rel) if p.is_file()}


def project_sources(name: str) -> tuple[set[Path], str]:
    path = VS / f"{name}.vcxproj"
    require(path.is_file(), f"Missing project: {name}.vcxproj")
    root = ET.parse(path).getroot()
    sources: set[Path] = set()
    groups = []
    for elem in root.iter():
        if elem.tag.endswith("ClCompile") and "Include" in elem.attrib:
            groups.append(elem.attrib["Include"])
            excluded: set[Path] = set()
            for pattern in elem.attrib.get("Exclude", "").split(";"):
                if pattern:
                    excluded |= glob_sources(pattern)
            sources |= glob_sources(elem.attrib["Include"]) - excluded
    require(sources, f"No sources resolved for {name}: {groups}")
    return sources, path.read_text(encoding="utf-8")


def validate() -> dict:
    manifest = load_manifest()
    sln = SOLUTION.read_text(encoding="utf-8")
    targets = manifest["targets"]
    names = [t["name"] for t in targets]
    require(len(names) == len(set(names)), "Duplicate project names")
    guids = [t["guid"] for t in targets]
    require(len(guids) == len(set(guids)), "Duplicate project GUIDs")
    require(len(targets) == 74, f"Expected 74 projects, got {len(targets)}")
    require(all(t["name"] != "KotonohaImGui" for t in targets),
            "Dear ImGui must come from vcpkg, not a local project")
    require(sln.count("Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\")") == len(targets),
            "Solution/project count mismatch")
    for target in targets:
        require(target["guid"] in sln, f"{target['name']}: GUID absent from solution")
        require(f'"{target["name"]}.vcxproj"' in sln, f"{target['name']}: absent from solution")
        config_lines = sum(1 for cfg in manifest["configurations"]
                           for plat in manifest["platforms"]
                           if f"{target['guid']}.{cfg}|{plat}.ActiveCfg" in sln)
        require(config_lines == 6, f"{target['name']}: incomplete configuration matrix")
        project_sources(target["name"])

    props = (VS / "Directory.Build.props").read_text(encoding="utf-8")
    require("$(RepoRoot)" in props, "Directory.Build.props: RepoRoot missing")
    targets_xml = (VS / "Directory.Build.targets").read_text(encoding="utf-8")
    require("KotonohaCopyVcpkgRuntime" in targets_xml, "Directory.Build.targets: DLL deployment missing")
    for triplet in manifest["vcpkg_triplets"].values():
        require(triplet in props, f"props: missing triplet {triplet}")
    require("$(MSBuildThisFileDirectory)vcpkg\\$(KotonohaVcpkgArch)" in props,
            "props: vcpkg roots are not separated per architecture")
    require("GeneratedIncludeDirs.props" in props, "props: missing generated include import")
    include_props = (VS / "GeneratedIncludeDirs.props").read_text(encoding="utf-8")
    require("submodules" not in include_props,
            "SDL/imgui must come from vcpkg, not submodules")
    include_tree = ET.fromstring(include_props)
    for item in include_tree.iter():
        if not item.tag.endswith("KotonohaGeneratedIncludeDir"):
            continue
        for directory in item.attrib["Include"].split(";"):
            require(directory.startswith("$(RepoRoot)"),
                    f"Generated include directory must be repository-relative: {directory}")
            relative = directory[len("$(RepoRoot)"):].replace("\\", "/")
            require(not Path(relative).is_absolute() and ".." not in Path(relative).parts,
                    f"Invalid generated include directory: {directory}")
            require((ROOT / relative).is_dir(),
                    f"Generated include directory does not exist: {directory}")
    for root in ("lib\\include", "modules\\vms\\n2system\\include",
                 "modules\\formats\\image\\cz\\include", "modules\\providers\\adv\\include",
                 "games\\school-days\\src", "launcher\\core"):
        require(root in include_props, f"GeneratedIncludeDirs.props: missing {root}")
    require("$(KotonohaVcpkgRoot)lib\\*.lib" in props, "props: missing vcpkg library linking")
    require((VS / "generated" / "Kotonoha" / "core" / "BuildVersion.hpp").is_file(),
            "Missing generated BuildVersion.hpp")
    require((ROOT / "tools" / "GenerateVisualStudioExports.ps1").is_file(),
            "Missing WINDOWS_EXPORT_ALL_SYMBOLS generator")
    for name in ("KotonohaEngine", "KotonohaLauncherCore"):
        target = next(t for t in targets if t["name"] == name)
        require(target["type"] == "DynamicLibrary", f"{name} must be a dynamic library")
        project = (VS / f"{name}.vcxproj").read_text(encoding="utf-8")
        require("<ConfigurationType>DynamicLibrary</ConfigurationType>" in project,
                f"{name}: project is not a DLL")
    for name in ("KotonohaEngine", "KotonohaLauncherCore"):
        project = (VS / f"{name}.vcxproj").read_text(encoding="utf-8")
        require("<ModuleDefinitionFile>$(IntDir)" in project,
                f"{name}: WINDOWS_EXPORT_ALL_SYMBOLS export list missing")
        require("GenerateVisualStudioExports.ps1" in project,
                f"{name}: export generator not invoked")
    core_project = (VS / "KotonohaLauncherCore.vcxproj").read_text(encoding="utf-8")
    require("KOTONOHA_LAUNCHER_CORE_BUILD=1" in core_project,
            "LauncherCore C ABI must be exported")
    require("KOTONOHA_LAUNCH_LUCA_SYSTEM=1" in core_project and
            "KOTONOHA_LAUNCH_SCHOOL_DAYS=1" in core_project,
            "LauncherCore must link all in-process game runtimes")

    engine_sources, engine_xml = project_sources("KotonohaEngine")
    disabled = {"MetalFXUpscaler.cpp", "MPVCompatibleUpscaler.cpp"}
    for src in (ROOT / "lib" / "src").rglob("*.cpp"):
        if src.name in disabled:
            require(src.name in engine_xml and "Exclude=" in engine_xml,
                    f"Engine must exclude disabled backend {src.name}")
            continue
        require(src.resolve() in engine_sources, f"Engine source missing: {src.relative_to(ROOT)}")

    for group, tnames in manifest["modules"].items():
        for name in tnames:
            target = next(t for t in targets if t["name"] == name)
            folder = re.match(r"^(VM|Format|Provider)_(.+)$", name)
            require(folder is not None, f"Cannot infer module path for {name}")
            sources, xml = project_sources(name)
            roots = {
                "VM": "modules/vms", "Formats": "modules/formats",
                "Providers": "modules/providers",
            }
            base = ROOT / roots[group] / target.get("path", "")
            # Modules are identified by the include glob in the project file.
            rels = [re.fullmatch(r'\$\(RepoRoot\)(.+?)\\\*\.cpp', inc)
                    for inc in re.findall(r'<ClCompile Include="([^"]+)"', xml)]
            roots_found = [m.group(1).replace("\\", "/") for m in rels if m]
            require(roots_found, f"{name}: no module source glob")
            for root_rel in roots_found:
                for src in (ROOT / root_rel).glob("*.cpp"):
                    if name == "VM_RenPy" and src.name == "PythonDummy.cpp":
                        continue
                    require(src.resolve() in sources, f"{name}: missing source {src.relative_to(ROOT)}")

    game_map = manifest["games"]
    require(len(game_map) == 16, "Expected 16 game projects")
    for name, slug in game_map.items():
        lib_sources, _ = project_sources(name + "Lib")
        app_sources, app_xml = project_sources(name)
        main = (ROOT / "games" / slug / "src" / "main.cpp").resolve()
        require(main in app_sources and len(app_sources) == 1,
                f"{name}: executable must compile only main.cpp")
        require(main not in lib_sources, f"{name}Lib: main.cpp must not be in the library")
        for src in (ROOT / "games" / slug / "src").rglob("*.cpp"):
            if src.resolve() == main:
                continue
            require(src.resolve() in lib_sources,
                    f"{name}Lib: missing source {src.relative_to(ROOT)}")
        require((name + "Lib.vcxproj") in app_xml,
                f"{name}: executable does not reference its static library")
    launcher, launcher_xml = project_sources("KotonohaLauncherCore")
    require((ROOT / "launcher" / "app" / "main.cpp").resolve() in project_sources("KotonohaLauncher")[0],
            "Launcher main.cpp missing")
    for src in (ROOT / "launcher" / "core").glob("*.cpp"):
        require(src.resolve() in launcher, f"LauncherCore missing {src.name}")
    require(len(launcher) == 3, "LauncherCore must compile only its own core sources")
    for name in game_map:
        require((name + "Lib.vcxproj") in launcher_xml,
                f"LauncherCore does not link {name}Lib")

    return manifest


def main() -> int:
    try:
        manifest = validate()
    except (ValueError, OSError, ET.ParseError, json.JSONDecodeError) as exc:
        print(f"[Kotonoha] Visual Studio validation failed: {exc}", file=sys.stderr)
        return 1
    modules = manifest["modules"]
    print(f"[Kotonoha] OK: {len(manifest['targets'])} VS projects "
          f"({len(manifest['games'])} games, "
          f"{sum(len(v) for v in modules.values())} modules); "
          f"platforms: {'/'.join(manifest['platforms'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
