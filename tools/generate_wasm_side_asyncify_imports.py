#!/usr/bin/env python3
"""Generate Asyncify imports for an Emscripten MAIN_MODULE.

A main module cannot infer that an imported function from a separate engine
side module may suspend via emscripten_sleep/HTTP Fetch. Passing the side
module's callable exports as ASYNCIFY_IMPORTS ensures the game-to-engine
boundary is instrumented. This is conservative; prefer correctness over
smallest binary size until per-engine suspend points are profiled.

Usage: python3 tools/generate_wasm_side_asyncify_imports.py \
    outputs/libs/libkotonoha-version.wasm build/engine-async-imports.json
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile

from check_wasm_asyncify import WasmValidationError, export_names


def generate(side: Path, destination: Path) -> int:
    names = export_names(side.read_bytes())
    # Exclude the module's own unwind machinery and ordinary wasm glue.
    exported = sorted(n for n in names if n and
                      not n.startswith("asyncify_") and
                      not n.startswith("__asyncify_"))
    if not exported:
        raise WasmValidationError(
            f"No exported engine functions in {side}; shared WASM link is invalid")
    # A function may suspend from an imported engine method, even if its
    # signature was never referenced by the native app source directly.
    imported = sorted(set(exported) | {"emscripten_sleep"})
    destination.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(imported, separators=(",", ":")) + "\n"
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=destination.parent,
        prefix=destination.name + ".", suffix=".tmp", delete=False
    ) as stream:
        stream.write(payload)
        temporary = Path(stream.name)
    try:
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"[Kotonoha] Asyncify: {len(imported)} shared-engine imports in {destination}")
    return len(imported)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        raise SystemExit(2)
    try:
        generate(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, UnicodeError, WasmValidationError) as exc:
        print(f"[Kotonoha] Cannot generate shared WASM Asyncify imports: {exc}",
              file=sys.stderr)
        raise SystemExit(1)
