#!/usr/bin/env python3
"""Final core compiler/validator helpers for KTRF binary v0.1.

The production core is the phase-7 semantic mapping with every required core
section from META through TRAN. EXTN and DBUG remain optional and are not
emitted by this core compiler.

Validation is deliberately redundant:
- generic container integrity (header/content SHA-256, section CRC32, bounds);
- exact canonical core section order;
- semantic decode against the phase-7 model;
- canonical decode -> encode byte identity;
- semantic item-count agreement.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Mapping

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

import binary_semantic_phase7_v0_1 as phase7  # noqa: E402

container = phase7.container
CORE_SECTION_TYPES = phase7.PHASE7_SECTION_TYPES
MANIFEST_FORMAT = "ktrf-binary-manifest"
MANIFEST_VERSION = "0.1"


class KtrfFullError(phase7.KtrfPhase7Error):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def semantic_item_counts(decoded: Mapping[str, Any]) -> dict[str, int]:
    features = decoded.get("features")
    if not isinstance(features, Mapping):
        raise KtrfFullError("decoded features must be an object")
    required = features.get("required")
    optional = features.get("optional")
    if not isinstance(required, list) or not isinstance(optional, list):
        raise KtrfFullError("decoded features.required/optional must be arrays")

    fields = {
        "NSPC": "namespaces",
        "ENTR": "entry_points",
        "VARS": "variables",
        "RSRC": "resource_locators",
        "HOOK": "external_hooks",
        "ENDG": "endings",
        "EXPR": "expressions",
        "EFFT": "effects",
        "CHOI": "choices",
        "NODE": "nodes",
        "TRAN": "transitions",
    }
    counts: dict[str, int] = {"META": 1, "FEAT": len(required) + len(optional)}
    for section, field in fields.items():
        value = decoded.get(field)
        if not isinstance(value, list):
            raise KtrfFullError(f"decoded {field} must be an array")
        counts[section] = len(value)
    return counts


def _directory_counts(parsed: container.ParsedContainer) -> dict[str, int]:
    return {
        entry.type_code.decode("ascii"): entry.item_count
        for entry in parsed.entries
    }


def validate_artifact(
    data: bytes,
) -> tuple[
    container.ParsedContainer,
    dict[str, Any],
    dict[str, int],
]:
    """Validate one core-only v0.1 artifact and prove canonical rebuild identity."""

    try:
        parsed = container.parse_container(
            data,
            known_section_types=CORE_SECTION_TYPES,
        )
    except container.KtrfBinaryError as exc:
        raise KtrfFullError(str(exc)) from exc

    actual_order = tuple(entry.type_code for entry in parsed.entries)
    if actual_order != CORE_SECTION_TYPES:
        got = ",".join(code.decode("ascii") for code in actual_order)
        expected = ",".join(code.decode("ascii") for code in CORE_SECTION_TYPES)
        raise KtrfFullError(
            f"core section order/set mismatch: got {got}; expected {expected}"
        )

    try:
        decoded = phase7.parse_phase7_container(data)
    except phase7.KtrfPhase7Error as exc:
        raise KtrfFullError(str(exc)) from exc

    expected_counts = semantic_item_counts(decoded)
    directory_counts = _directory_counts(parsed)
    for section, expected in expected_counts.items():
        actual = directory_counts.get(section)
        if actual != expected:
            raise KtrfFullError(
                f"{section} item_count mismatch: directory={actual}, semantic={expected}"
            )

    try:
        rebuilt = phase7.build_phase7_container(decoded)
    except phase7.KtrfPhase7Error as exc:
        raise KtrfFullError(f"decoded artifact cannot be rebuilt: {exc}") from exc
    if rebuilt != data:
        raise KtrfFullError(
            "canonical decode -> encode is not byte-identical to the input artifact"
        )

    return parsed, decoded, directory_counts


def compile_document(
    document: Mapping[str, Any],
) -> tuple[
    bytes,
    container.ParsedContainer,
    dict[str, Any],
    dict[str, int],
]:
    """Compile a canonical core artifact and run all final in-memory gates."""

    try:
        first = phase7.build_phase7_container(document)
        second = phase7.build_phase7_container(document)
    except phase7.KtrfPhase7Error as exc:
        raise KtrfFullError(str(exc)) from exc

    if first != second:
        raise KtrfFullError("repeated build from identical IR is not deterministic")

    try:
        expected = phase7.canonical_phase7_projection(document)
        decoded_direct = phase7.parse_phase7_container(first)
    except phase7.KtrfPhase7Error as exc:
        raise KtrfFullError(str(exc)) from exc
    if decoded_direct != expected:
        raise KtrfFullError("full semantic projection round-trip mismatch")

    parsed, decoded, counts = validate_artifact(first)
    if decoded != expected:
        raise KtrfFullError("independent full artifact validation changed semantics")

    return first, parsed, decoded, counts


def build_manifest(
    *,
    data: bytes,
    parsed: container.ParsedContainer,
    counts: Mapping[str, int],
    artifact_name: str,
    source_ir_name: str | None = None,
    source_ir_sha256: str | None = None,
) -> dict[str, Any]:
    sections: list[dict[str, Any]] = []
    for entry in parsed.entries:
        sections.append(
            {
                "type": entry.type_code.decode("ascii"),
                "flags": entry.flags,
                "offset": entry.offset,
                "stored_size": entry.stored_size,
                "decoded_size": entry.decoded_size,
                "item_count": entry.item_count,
                "alignment": entry.alignment,
                "crc32": f"{entry.crc32:08x}",
            }
        )

    manifest: dict[str, Any] = {
        "format": MANIFEST_FORMAT,
        "version": MANIFEST_VERSION,
        "binary_format": {
            "major": parsed.format_major,
            "minor": parsed.format_minor,
        },
        "artifact": artifact_name,
        "bytes": len(data),
        "sha256": sha256_bytes(data),
        "content_sha256": parsed.content_sha256.hex(),
        "section_order": [
            entry.type_code.decode("ascii") for entry in parsed.entries
        ],
        "item_counts": dict(counts),
        "sections": sections,
        "optional_sections_emitted": [],
    }
    if source_ir_name is not None or source_ir_sha256 is not None:
        manifest["source_ir"] = {
            "name": source_ir_name,
            "sha256": source_ir_sha256,
        }
    return manifest


def verify_manifest(
    data: bytes,
    parsed: container.ParsedContainer,
    counts: Mapping[str, int],
    manifest: Mapping[str, Any],
) -> None:
    if manifest.get("format") != MANIFEST_FORMAT:
        raise KtrfFullError("manifest format mismatch")
    if manifest.get("version") != MANIFEST_VERSION:
        raise KtrfFullError("manifest version mismatch")
    if manifest.get("bytes") != len(data):
        raise KtrfFullError("manifest byte length mismatch")
    if manifest.get("sha256") != sha256_bytes(data):
        raise KtrfFullError("manifest artifact SHA-256 mismatch")
    if manifest.get("content_sha256") != parsed.content_sha256.hex():
        raise KtrfFullError("manifest content SHA-256 mismatch")

    actual_order = [entry.type_code.decode("ascii") for entry in parsed.entries]
    if manifest.get("section_order") != actual_order:
        raise KtrfFullError("manifest section order mismatch")
    if manifest.get("item_counts") != dict(counts):
        raise KtrfFullError("manifest item_counts mismatch")

    raw_sections = manifest.get("sections")
    if not isinstance(raw_sections, list) or len(raw_sections) != len(parsed.entries):
        raise KtrfFullError("manifest sections array mismatch")

    for raw, entry in zip(raw_sections, parsed.entries):
        if not isinstance(raw, Mapping):
            raise KtrfFullError("manifest section entry must be an object")
        expected = {
            "type": entry.type_code.decode("ascii"),
            "flags": entry.flags,
            "offset": entry.offset,
            "stored_size": entry.stored_size,
            "decoded_size": entry.decoded_size,
            "item_count": entry.item_count,
            "alignment": entry.alignment,
            "crc32": f"{entry.crc32:08x}",
        }
        if dict(raw) != expected:
            raise KtrfFullError(
                f"manifest section metadata mismatch for {expected['type']}"
            )


def dump_manifest(path: Path, manifest: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def load_manifest(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise KtrfFullError("manifest root must be an object")
    return value
