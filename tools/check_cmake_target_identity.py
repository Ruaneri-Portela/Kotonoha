#!/usr/bin/env python3
"""Check canonical first-party CMake targets and catch static-link cycles.

Dependency packages may provide imported targets with namespaces. This checker
only audits CMake declarations owned by Kotonoha, never third-party sources.
No CMake installation, game assets or SDL libraries are needed.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMAND = re.compile(
    r"(?m)^[ \t]*(add_library|add_executable|kotonoha_add_app|"
    r"target_link_libraries|add_test)[ \t]*\("
)
CANONICAL = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+-]*$")
PROJECT_ALIAS = re.compile(
    r"\bKotonoha::(?:Engine|Provider[A-Za-z0-9_-]*|"
    r"Vm[A-Za-z0-9_-]*|Modules::)"
)
PUBLIC_KEYWORDS = {"PUBLIC", "PRIVATE", "INTERFACE", "LINK_PUBLIC", "LINK_PRIVATE",
                   "debug", "optimized", "general"}


def commands(data: str):
    """Yield CMake commands (including multiline lists and generator expressions)."""
    for match in COMMAND.finditer(data):
        # Skip matches inside line comments (the regexp is line anchored).
        start = match.end()
        depth = 1
        quoted = False
        escaped = False
        pos = start
        while pos < len(data) and depth:
            ch = data[pos]
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quoted = not quoted
            elif not quoted:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
            pos += 1
        if depth == 0:
            yield match.group(1), data[start:pos - 1], data.count("\n", 0, match.start()) + 1


def audit(sources: dict[str, str]) -> list[str]:
    errors: list[str] = []
    targets: dict[str, str] = {}
    links: list[tuple[str, str, str, int]] = []
    test_names: dict[str, str] = {}

    for path, source in sorted(sources.items()):
        if PROJECT_ALIAS.search(source):
            # Show an actionable location, not just the filename.
            for i, line in enumerate(source.splitlines(), 1):
                if PROJECT_ALIAS.search(line) and not line.lstrip().startswith("#"):
                    errors.append(f"{path}:{i}: obsolete project target alias")

        for kind, body, line in commands(source):
            tokens = body.split()
            if not tokens:
                continue
            target = tokens[0]
            if kind in {"add_library", "add_executable", "kotonoha_add_app"}:
                if "ALIAS" in tokens[1:]:
                    errors.append(f"{path}:{line}: compiled-target ALIAS is forbidden")
                if not CANONICAL.fullmatch(target):
                    # Dynamic names are supplied by a single CMake helper.
                    continue
                previous = targets.get(target)
                if previous:
                    errors.append(
                        f"{path}:{line}: target {target!r} defined twice "
                        f"(first seen in {previous})"
                    )
                else:
                    targets[target] = f"{path}:{line}"
            elif kind == "target_link_libraries":
                if not CANONICAL.fullmatch(target):
                    continue
                for dependency in tokens[1:]:
                    if dependency in PUBLIC_KEYWORDS or not CANONICAL.fullmatch(dependency):
                        continue
                    links.append((target, dependency, path, line))
            elif kind == "add_test":
                if len(tokens) > 2 and tokens[0] == "NAME":
                    name = tokens[1]
                    # CTest permits distinct scenarios to invoke the same
                    # executable. It does not create more compiled targets.
                    if CANONICAL.fullmatch(name):
                        old = test_names.get(name)
                        if old:
                            errors.append(
                                f"{path}:{line}: duplicate CTest case {name!r} "
                                f"(first seen in {old})"
                            )
                        else:
                            test_names[name] = f"{path}:{line}"

    graph: dict[str, set[str]] = defaultdict(set)
    for target, dependency, path, line in links:
        if target not in targets or dependency not in targets:
            continue
        if target == dependency:
            errors.append(f"{path}:{line}: {target} links itself")
        graph[target].add(dependency)

    states: dict[str, int] = {}
    stack: list[str] = []
    reported: set[tuple[str, ...]] = set()

    def visit(node: str) -> None:
        states[node] = 1
        stack.append(node)
        for dep in sorted(graph.get(node, ())):
            if states.get(dep) == 0 or dep not in states:
                visit(dep)
            elif states[dep] == 1:
                cycle = tuple(stack[stack.index(dep):] + [dep])
                if cycle not in reported:
                    reported.add(cycle)
                    errors.append("static target dependency cycle: " + " -> ".join(cycle))
        stack.pop()
        states[node] = 2

    for target in sorted(targets):
        if target not in states:
            visit(target)

    # Versioned shared library output expands a single compiled target to
    # extra symlink names, breaking the requested one-file-per-target layout.
    core = sources.get("lib/CMakeLists.txt", "")
    sdl = sources.get("buildsystems/cmake/dependencies/KotonohaSDL.cmake", core)
    if re.search(r"(?m)^[ \t]*(?:VERSION|SOVERSION)[ \t]+\$\{PROJECT_VERSION", core + "\n" + sdl):
        errors.append("shared libraries must not use project-versioned filenames")
    if "PROPERTY VERSION)" not in sdl or "PROPERTY SOVERSION)" not in sdl:
        errors.append("KotonohaSDL.cmake: vendored SDL must clear version properties")

    # The DDLC format archive calls the engine Filesystem APIs. Without an
    # explicit target dependency, GNU ld (notably MinGW) may put the engine's
    # DLL import library before DdlcFormats.a and fail to resolve readAll,
    # SDLFileSource and Filesystem::open/exists/list.
    ddlc = sources.get("games/doki-doki-literature-club/CMakeLists.txt")
    if ddlc is not None:
        has_engine_dependency = any(
            kind == "target_link_libraries"
            and body.split()[:1] == ["DdlcFormats"]
            and "KotonohaEngine" in body.split()
            for kind, body, _ in commands(ddlc)
        )
        if not has_engine_dependency:
            errors.append("DDLC DdlcFormats must directly link KotonohaEngine "
                          "to preserve import-library link order on MinGW")

    return errors


def self_test() -> None:
    def ok(snippet: str, should_fail: bool) -> None:
        fixture = {"lib/CMakeLists.txt": (
            "set_property(TARGET sdl PROPERTY VERSION)\n"
            "set_property(TARGET sdl PROPERTY SOVERSION)\n"
            + snippet
        )}
        found = audit(fixture)
        if bool(found) != should_fail:
            raise AssertionError(f"unexpected audit: {found!r} for {snippet!r}")

    ok("add_library(foo STATIC foo.cpp)\nadd_library(bar STATIC bar.cpp)\n"
       "target_link_libraries(foo PRIVATE bar)\n", False)
    ok("add_library(foo STATIC a.cpp)\nadd_library(foo STATIC b.cpp)\n", True)
    ok("add_library(foo STATIC a.cpp)\nadd_library(Kotonoha::Foo ALIAS foo)\n", True)
    ok("add_library(foo STATIC a.cpp)\nadd_library(bar STATIC b.cpp)\n"
       "target_link_libraries(foo PRIVATE bar)\n"
       "target_link_libraries(bar PRIVATE foo)\n", True)
    ok("add_executable(test_tool main.cpp)\n"
       "add_test(NAME test_tool COMMAND test_tool)\n", False)
    fixture_core = ("set_property(TARGET sdl PROPERTY VERSION)\n"
                    "set_property(TARGET sdl PROPERTY SOVERSION)\n")
    fixture_path = "games/doki-doki-literature-club/CMakeLists.txt"
    assert any("DdlcFormats must directly link" in issue for issue in audit({
        "lib/CMakeLists.txt": fixture_core,
        fixture_path: "add_library(DdlcFormats STATIC Archive.cpp)\n",
    }))
    assert not audit({
        "lib/CMakeLists.txt": fixture_core,
        fixture_path: ("add_library(DdlcFormats STATIC Archive.cpp)\n"
                       "target_link_libraries(DdlcFormats PUBLIC KotonohaEngine)\n"),
    })
    # The SDL provider moved out of lib/CMakeLists.txt. The audit must find
    # the helper through its POSIX-style dictionary key, including on Windows.
    sdl_key = "buildsystems/cmake/dependencies/KotonohaSDL.cmake"
    provider = ("set_property(TARGET ${_kotonoha_sdl_shared} PROPERTY VERSION)\n"
                "set_property(TARGET ${_kotonoha_sdl_shared} PROPERTY SOVERSION)\n")
    assert not audit({"lib/CMakeLists.txt": "", sdl_key: provider})
    assert any("vendored SDL must clear version properties" in issue for issue in audit({
        "lib/CMakeLists.txt": fixture_core,
        sdl_key: "set_property(TARGET sdl PROPERTY VERSION)\n",
    }))
    print("[CMake targets] self-tests passed")


def main() -> int:
    if "--self-test" in sys.argv:
        self_test()
    files = {
        file.relative_to(ROOT).as_posix(): file.read_text(encoding="utf-8")
        for file in ROOT.rglob("CMakeLists.txt")
        if "submodules" not in file.parts
        and "out" not in file.parts
        and "build" not in file.parts
    }
    for helper in ("KotonohaSDL.cmake", "KotonohaImGui.cmake"):
        helper_path = ROOT / "buildsystems" / "cmake" / "dependencies" / helper
        if not helper_path.is_file():
            print(f"[CMake targets] missing dependency helper: {helper}", file=sys.stderr)
            return 1
        files[helper_path.relative_to(ROOT).as_posix()] = helper_path.read_text(encoding="utf-8")
    core_path = ROOT / "lib/CMakeLists.txt"
    if not core_path.exists():
        print("[CMake targets] expected lib/CMakeLists.txt is missing", file=sys.stderr)
        return 1
    problems = audit(files)
    for error in problems:
        print("[CMake targets] " + error, file=sys.stderr)
    if problems:
        return 1
    print(f"[CMake targets] {len(files)} CMakeLists verified; "
          f"no first-party aliases, duplicate targets or link cycles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
