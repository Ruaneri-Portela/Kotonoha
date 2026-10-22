#!/usr/bin/env python3
"""Compile KTRF binary v0.1 semantic tables through phase 5 / CHOI."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping

import binary_semantic_phase5_v0_1 as phase5


def load_document(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise SystemExit("Routing IR root must be an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile KTRF binary v0.1 semantic tables through CHOI"
    )
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    ir_path = args.ir.resolve()
    output = args.output.resolve()
    document = load_document(ir_path)

    data = phase5.build_phase5_container(document)
    decoded = phase5.parse_phase5_container(data)
    expected = phase5.canonical_phase5_projection(document)
    if decoded != expected:
        raise SystemExit("phase-5 semantic projection round-trip mismatch")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)

    parsed = phase5.container.parse_container(
        data, known_section_types=phase5.PHASE5_SECTION_TYPES
    )
    counts = {
        entry.type_code.decode("ascii"): entry.item_count for entry in parsed.entries
    }
    print("=== KTRF binary v0.1 semantic phase 5 / CHOI ===")
    print(f"input={ir_path}")
    print(f"bytes={len(data)}")
    print("sections=" + ",".join(entry.type_code.decode("ascii") for entry in parsed.entries))
    print("item_counts=" + json.dumps(counts, sort_keys=True))
    print(f"output={output}")
    print("PHASE5 CHOI SEMANTIC BINARY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
