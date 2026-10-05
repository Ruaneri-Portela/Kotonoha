#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_container_v0_1.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    return module


def main() -> int:
    mod = load_module("ktrf_binary_smoke", MODULE_PATH)

    strs, _ = mod.encode_strs(["Kotonoha", "KTRF", "overflow.school-days-hq"])
    data = mod.build_container(
        [
            mod.Section(b"META", b"binary-v0.1-smoke"),
            mod.Section(b"STRS", strs, item_count=len(mod.decode_strs(strs))),
        ]
    )

    out = ROOT / "build" / "ktrf" / "binary-v0.1-smoke.ktnroute"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)

    parsed = mod.parse_container(data, known_section_types=[b"META", b"STRS"])
    print(f"magic={data[:4].decode('ascii')}")
    print(f"bytes={len(data)}")
    print("sections=" + ",".join(e.type_code.decode("ascii") for e in parsed.entries))
    print(f"output={out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
