#!/usr/bin/env python3
"""Fail the WASM build if its Asyncify JS glue has no matching WASM exports.

The JS loader emitted with -sASYNCIFY=1 calls these WebAssembly functions
when emscripten_sleep unwinds. A missing export causes:
    TypeError: _asyncify_start_unwind is not a function

Read the core WASM export section directly (stdlib only), before packaging.
Usage: python3 tools/check_wasm_asyncify.py path/to/Game.js
       python3 tools/check_wasm_asyncify.py --self-test
"""
from __future__ import annotations

import pathlib
import sys

REQUIRED = frozenset({
    "asyncify_start_unwind",
    "asyncify_stop_unwind",
    "asyncify_start_rewind",
    "asyncify_stop_rewind",
})


class WasmValidationError(ValueError):
    pass


def u32(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    for step in range(5):
        if pos >= len(data):
            raise WasmValidationError("Truncated WASM LEB128 value")
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << (step * 7)
        if not byte & 0x80:
            return value, pos
    raise WasmValidationError("Invalid WASM LEB128 value")


def export_names(data: bytes) -> set[str]:
    if data[:8] != b"\\0asm\\x01\\0\\0\\0":
        raise WasmValidationError("Not a WebAssembly version 1 module")
    at = 8
    while at < len(data):
        section_type = data[at]
        at += 1
        length, at = u32(data, at)
        end = at + length
        if end > len(data):
            raise WasmValidationError("Truncated WASM section")
        if section_type == 7:
            count, cursor = u32(data, at)
            result: set[str] = set()
            for _ in range(count):
                size, cursor = u32(data, cursor)
                if cursor + size + 1 > end:
                    raise WasmValidationError("Truncated WASM export name")
                name = data[cursor:cursor + size].decode("utf-8")
                cursor += size
                kind = data[cursor]
                cursor += 1
                _, cursor = u32(data, cursor)
                if kind == 0:
                    result.add(name)
            if cursor > end:
                raise WasmValidationError("Malformed WASM export section")
            return result
        at = end
    return set()


def validate(js_path: pathlib.Path) -> None:
    if not js_path.is_file():
        raise WasmValidationError(f"Generated JavaScript not found: {js_path}")
    wasm_path = js_path.with_suffix(".wasm")
    if not wasm_path.is_file():
        raise WasmValidationError(f"Generated WASM not found: {wasm_path}")
    js = js_path.read_text(encoding="utf-8")
    if "asyncify_start_unwind" not in js or "asyncify_stop_unwind" not in js:
        raise WasmValidationError(
            f"JavaScript is missing Asyncify glue: {js_path}. "
            "Check the final Emscripten link options.")
    exports = export_names(wasm_path.read_bytes())
    missing = REQUIRED - exports
    if missing:
        raise WasmValidationError(
            f"{wasm_path} lacks the Asyncify exports "
            f"{', '.join(sorted(missing))}. The matching JS would abort "
            "during emscripten_sleep. Rebuild the whole target with "
            "-sASYNCIFY=1, -O2, and clear the previous build directory.")
    print(f"[Kotonoha] Asyncify exports OK: {wasm_path}")


def leb(value: int) -> bytes:
    result = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        result.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(result)


def self_test() -> None:
    def make_wasm(names: set[str]) -> bytes:
        payload = bytearray(leb(len(names)))
        for name in sorted(names):
            raw = name.encode()
            payload += leb(len(raw)) + raw + b"\\x00" + leb(0)
        return b"\\0asm\\x01\\0\\0\\0" + b"\\x07" + leb(len(payload)) + payload

    assert export_names(make_wasm(set(REQUIRED))) == set(REQUIRED)
    assert REQUIRED - export_names(make_wasm({"asyncify_start_unwind"}))
    for invalid in (b"", b"not-wasm", b"\\0asm\\x01\\0\\0\\0\\x07\\x20"):
        try:
            export_names(invalid)
        except WasmValidationError:
            continue
        raise AssertionError("Expected WASM validation failure")
    print("[Kotonoha] WASM export verifier self-test OK")


def main(argv: list[str]) -> int:
    try:
        if argv == ["--self-test"]:
            self_test()
        elif len(argv) == 1:
            validate(pathlib.Path(argv[0]))
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except (WasmValidationError, OSError, UnicodeError) as exc:
        print(f"[Kotonoha] Asyncify validation FAILED: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
