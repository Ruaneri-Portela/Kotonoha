#!/usr/bin/env python3
"""Assert the correct split between Emscripten main and side module links.

The main game owns static vcpkg archives; the engine SIDE_MODULE must import
their symbols instead of embedding duplicate independent runtimes.
"""
from pathlib import Path
import re
import shlex
import subprocess
import sys

from wasm_link_dedupe import archive_identity, normalize


def link_command(build, target, flag):
    result = subprocess.run(
        ["ninja", "-C", str(build), "-t", "commands", target],
        check=True, capture_output=True, text=True,
    )
    matches = [
        line.split(" && ", 1)[0]
        for line in result.stdout.splitlines()
        if flag in line and " -o " in line
    ]
    if len(matches) != 1:
        raise RuntimeError(f"{target}: expected one {flag} link, got {len(matches)}")
    return shlex.split(matches[0])


def check(build):
    side = link_command(build, "KotonohaEngine", "-sSIDE_MODULE=1")
    static_archives = [arg for arg in side if arg.endswith(".a")]
    if static_archives:
        raise RuntimeError("SIDE_MODULE embeds static archives: " + ", ".join(static_archives))
    if any(arg.startswith("-l") and arg != "-lm" for arg in side):
        raise RuntimeError("SIDE_MODULE links external libraries: " +
                           ", ".join(arg for arg in side if arg.startswith("-l")))

    main = link_command(build, "Kotonoha", "-sMAIN_MODULE=1")
    normalized = normalize(main)
    required = ("libSDL3.a", "libSDL3_ttf.a", "libimgui.a")
    for name in required:
        if not any(arg.endswith(name) for arg in normalized):
            raise RuntimeError(f"MAIN_MODULE missing required static runtime {name}")
    if not any(arg.endswith("libkotonoha.wasm") for arg in main):
        raise RuntimeError("MAIN_MODULE does not link Kotonoha SIDE_MODULE")
    print("[Kotonoha] side module imports runtime; main owns static vcpkg archives")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: check_wasm_link_architecture.py <build-dir>")
    check(Path(sys.argv[1]))


if __name__ == "__main__":
    main()
