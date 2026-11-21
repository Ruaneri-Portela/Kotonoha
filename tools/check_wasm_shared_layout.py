#!/usr/bin/env python3
"""Validate a dynamically linked Kotonoha WebAssembly app after em++ linking.

Usage: python3 tools/check_wasm_shared_layout.py \
    outputs/Game/Game.js outputs/libs/libkotonoha-<version>.wasm
"""
from __future__ import annotations

import pathlib
import sys

from check_wasm_asyncify import WasmValidationError, export_names, u32


def custom_sections(data: bytes) -> set[str]:
    if data[:8] != b"\0asm\x01\0\0\0":
        raise WasmValidationError("Invalid shared engine WASM magic/version")
    names: set[str] = set()
    pos = 8
    while pos < len(data):
        code = data[pos]
        pos += 1
        size, pos = u32(data, pos)
        end = pos + size
        if end > len(data):
            raise WasmValidationError("Truncated shared engine WASM section")
        if code == 0:
            count, offset = u32(data, pos)
            if offset + count > end:
                raise WasmValidationError("Malformed WASM custom section name")
            names.add(data[offset:offset + count].decode("utf-8"))
        pos = end
    return names


def validate(js_file: pathlib.Path, side_file: pathlib.Path) -> None:
    if js_file.suffix != ".js" or side_file.suffix != ".wasm":
        raise WasmValidationError("Expected main <Game>.js and shared lib*.wasm")
    main_wasm = js_file.with_suffix(".wasm")
    for path in (js_file, main_wasm, side_file):
        if not path.is_file():
            raise WasmValidationError(f"Missing WASM layout output: {path}")
    if main_wasm.resolve() == side_file.resolve():
        raise WasmValidationError("Game and engine are not separate binaries")
    if side_file.parent == main_wasm.parent:
        raise WasmValidationError(
            "Shared engine must live outside per-game output directory")

    side_bytes = side_file.read_bytes()
    dylink = custom_sections(side_bytes)
    if not ({"dylink", "dylink.0"} & dylink):
        raise WasmValidationError(
            "libkotonoha.wasm has no dylink section; it is not a side module")
    if not export_names(side_bytes):
        raise WasmValidationError("libkotonoha.wasm exports no engine functions")

    js = js_file.read_text(encoding="utf-8")
    if side_file.name not in js:
        raise WasmValidationError(
            "Game JavaScript does not declare the shared engine dependency")
    if not main_wasm.read_bytes().startswith(b"\0asm\x01\0\0\0"):
        raise WasmValidationError("Invalid game main WASM binary")
    print(f"[Kotonoha] Dynamic WASM layout OK: "
          f"{main_wasm.name} + ../libs/{side_file.name}")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        validate(pathlib.Path(argv[0]), pathlib.Path(argv[1]))
    except (OSError, ValueError, UnicodeError, WasmValidationError) as exc:
        print(f"[Kotonoha] Dynamic WASM layout FAILED: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
