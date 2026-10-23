#!/usr/bin/env python3
"""Compile KTRF binary v0.1 semantic tables through phase 7 / TRAN."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping

import binary_semantic_phase7_v0_1 as phase7


def load_document(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise SystemExit("Routing IR root must be an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile KTRF binary v0.1 semantic tables through TRAN"
    )
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    ir_path = args.ir.resolve()
    output = args.output.resolve()
    document = load_document(ir_path)

    data = phase7.build_phase7_container(document)
    decoded = phase7.parse_phase7_container(data)
    expected = phase7.canonical_phase7_projection(document)
    if decoded != expected:
        raise SystemExit("phase-7 semantic projection round-trip mismatch")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)

    parsed = phase7.container.parse_container(
        data, known_section_types=phase7.PHASE7_SECTION_TYPES
    )
    counts = {
        entry.type_code.decode("ascii"): entry.item_count for entry in parsed.entries
    }
    print("=== KTRF binary v0.1 semantic phase 7 / TRAN ===")
    print(f"input={ir_path}")
    print(f"bytes={len(data)}")
    print(
        "sections="
        + ",".join(entry.type_code.decode("ascii") for entry in parsed.entries)
    )
    print("item_counts=" + json.dumps(counts, sort_keys=True))
    print(f"output={output}")
    print("PHASE7 TRAN SEMANTIC BINARY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
