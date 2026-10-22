#!/usr/bin/env python3
"""Compile a production-candidate KTRF binary v0.1 core artifact."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping

import binary_full_v0_1 as full


def load_document(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise SystemExit("Routing IR root must be an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile complete KTRF binary v0.1 core (META through TRAN)"
    )
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        help="manifest path (default: <output>.manifest.json)",
    )
    args = parser.parse_args()

    ir_path = args.ir.resolve()
    output = args.output.resolve()
    manifest_path = (
        args.manifest.resolve()
        if args.manifest is not None
        else Path(str(output) + ".manifest.json")
    )

    document = load_document(ir_path)
    data, parsed, _decoded, counts = full.compile_document(document)

    manifest = full.build_manifest(
        data=data,
        parsed=parsed,
        counts=counts,
        artifact_name=output.name,
        source_ir_name=ir_path.name,
        source_ir_sha256=full.sha256_file(ir_path),
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    full.dump_manifest(manifest_path, manifest)

    print("=== KTRF binary v0.1 / complete core compiler ===")
    print(f"input={ir_path}")
    print(f"bytes={len(data)}")
    print(f"sha256={manifest['sha256']}")
    print(f"content_sha256={manifest['content_sha256']}")
    print("sections=" + ",".join(manifest["section_order"]))
    print("item_counts=" + json.dumps(counts, sort_keys=True))
    print(f"output={output}")
    print(f"manifest={manifest_path}")
    print("KTRF FULL CORE COMPILE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
