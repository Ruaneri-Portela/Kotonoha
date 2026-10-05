#!/usr/bin/env python3
"""Independently validate an already-built KTRF binary v0.1 core artifact."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import binary_full_v0_1 as full


def parse_expected(values: list[str]) -> dict[str, int]:
    result: dict[str, int] = {}
    for raw in values:
        if "=" not in raw:
            raise SystemExit(f"--expect must be FOURCC=COUNT, got {raw!r}")
        section, count_text = raw.split("=", 1)
        section = section.strip().upper()
        if len(section) != 4:
            raise SystemExit(f"--expect section must be a FourCC, got {section!r}")
        try:
            count = int(count_text, 10)
        except ValueError as exc:
            raise SystemExit(f"invalid --expect count in {raw!r}") from exc
        if count < 0:
            raise SystemExit(f"--expect count must be non-negative, got {raw!r}")
        if section in result:
            raise SystemExit(f"duplicate --expect section {section}")
        result[section] = count
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate complete KTRF binary v0.1 core artifact"
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--expect",
        action="append",
        default=[],
        metavar="FOURCC=COUNT",
        help="assert a section item_count; may be repeated",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    data = input_path.read_bytes()

    try:
        parsed, _decoded, counts = full.validate_artifact(data)
    except full.KtrfFullError as exc:
        raise SystemExit(f"KTRF FULL VALIDATION FAIL: {exc}") from exc

    expected = parse_expected(args.expect)
    for section, wanted in expected.items():
        actual = counts.get(section)
        if actual is None:
            raise SystemExit(f"KTRF FULL VALIDATION FAIL: missing section {section}")
        if actual != wanted:
            raise SystemExit(
                f"KTRF FULL VALIDATION FAIL: {section} item_count {actual} != {wanted}"
            )

    if args.manifest is not None:
        manifest_path = args.manifest.resolve()
        try:
            manifest = full.load_manifest(manifest_path)
            full.verify_manifest(data, parsed, counts, manifest)
        except full.KtrfFullError as exc:
            raise SystemExit(f"KTRF FULL VALIDATION FAIL: {exc}") from exc
    else:
        manifest_path = None

    print("=== KTRF binary v0.1 / independent full validator ===")
    print(f"input={input_path}")
    print(f"bytes={len(data)}")
    print(f"sha256={full.sha256_bytes(data)}")
    print(f"content_sha256={parsed.content_sha256.hex()}")
    print(
        "sections="
        + ",".join(entry.type_code.decode("ascii") for entry in parsed.entries)
    )
    print("item_counts=" + json.dumps(counts, sort_keys=True))
    if manifest_path is not None:
        print(f"manifest={manifest_path}")
    print("KTRF FULL VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
