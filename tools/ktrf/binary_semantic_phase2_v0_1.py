#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 2.

This layer extends phase 1 with the first referenciable entity catalogs:

    RSRC, HOOK, ENDG

Catalog record order is canonical by raw UTF-8 stable semantic ID. The physical
table index is the zero-based record position after canonical sorting. Stable IDs
remain encoded in every record so decoding never depends on source JSON order.
"""

from __future__ import annotations

import copy
import math
import struct
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

import binary_semantic_phase1_v0_1 as phase1  # noqa: E402


container = phase1.container

RSRC_STRUCT = struct.Struct("<6I")
HOOK_STRUCT = struct.Struct("<4I")
ENDG_STRUCT = struct.Struct("<IIBBHIQ")

RSRC_RECORD_SIZE = RSRC_STRUCT.size
HOOK_RECORD_SIZE = HOOK_STRUCT.size
ENDG_RECORD_SIZE = ENDG_STRUCT.size

RSRC_REQUIRED = 0x00000001
RSRC_HAS_REQUIRED = 0x00000002
RSRC_FLAGS_KNOWN = RSRC_REQUIRED | RSRC_HAS_REQUIRED

ENDG_CODE_ABSENT = 0
ENDG_CODE_NULL = 1
ENDG_CODE_BOOL = 2
ENDG_CODE_INT64 = 3
ENDG_CODE_FLOAT64 = 4
ENDG_CODE_STRING = 5

PHASE2_SECTION_TYPES = phase1.PHASE1_SECTION_TYPES + (b"RSRC", b"HOOK", b"ENDG")


class KtrfPhase2Error(phase1.KtrfPhase1Error):
    pass


def _require_phase2_string(value: Any, label: str) -> str:
    try:
        return phase1._require_string(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc


def _require_phase2_mapping(value: Any, label: str) -> Mapping[str, Any]:
    try:
        return phase1._require_mapping(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc


def _require_phase2_list(value: Any, label: str) -> list[Any]:
    try:
        return phase1._require_list(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc


def _index(strings: Mapping[str, int], value: str, label: str) -> int:
    try:
        return phase1._index(strings, value, label)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc


def _string(strings: Sequence[str], index: int, label: str) -> str:
    try:
        return phase1._string(strings, index, label)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc


def _sorted_catalog(document: Mapping[str, Any], key: str, label: str) -> list[Mapping[str, Any]]:
    rows = [
        _require_phase2_mapping(row, label)
        for row in _require_phase2_list(document.get(key), key)
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc

    seen: set[str] = set()
    for row in rows:
        record_id = _require_phase2_string(row.get("id"), f"{label}.id")
        if record_id in seen:
            raise KtrfPhase2Error(f"duplicate {label} id {record_id!r}")
        seen.add(record_id)
    return rows


def catalog_index_maps(document: Mapping[str, Any]) -> dict[str, dict[str, int]]:
    """Return the canonical zero-based binary index assigned to each catalog ID."""
    specs = (
        ("resource_locators", "ResourceLocator", "RSRC"),
        ("external_hooks", "ExternalHook", "HOOK"),
        ("endings", "Ending", "ENDG"),
    )
    result: dict[str, dict[str, int]] = {}
    for key, label, section_name in specs:
        rows = _sorted_catalog(document, key, label)
        result[section_name] = {
            _require_phase2_string(row.get("id"), f"{label}.id"): index
            for index, row in enumerate(rows)
        }
    return result


def _validate_resource_row(row: Mapping[str, Any]) -> None:
    _require_phase2_string(row.get("id"), "ResourceLocator.id")
    _require_phase2_string(row.get("scheme"), "ResourceLocator.scheme")
    if not isinstance(row.get("value"), str):
        raise KtrfPhase2Error(
            "binary phase 2 only defines string ResourceLocator.value encoding"
        )
    _require_phase2_string(row.get("value"), "ResourceLocator.value")
    if "media_type" in row:
        _require_phase2_string(row.get("media_type"), "ResourceLocator.media_type")
    if "required" in row and type(row.get("required")) is not bool:
        raise KtrfPhase2Error("ResourceLocator.required must be boolean when present")


def _validate_hook_row(row: Mapping[str, Any]) -> None:
    _require_phase2_string(row.get("id"), "ExternalHook.id")
    _require_phase2_string(row.get("symbol"), "ExternalHook.symbol")
    _require_phase2_string(row.get("contract"), "ExternalHook.contract")
    if "arguments_schema" in row:
        raise KtrfPhase2Error(
            "binary phase 2 does not define ExternalHook.arguments_schema encoding"
        )


def _validate_ending_code(value: Any) -> None:
    if value is None or type(value) is bool or isinstance(value, str):
        if isinstance(value, str):
            _require_phase2_string(value, "Ending.code")
        return
    if type(value) is int:
        if not (-0x8000000000000000 <= value <= 0x7FFFFFFFFFFFFFFF):
            raise KtrfPhase2Error("Ending.code integer is out of signed int64 range")
        return
    if type(value) is float:
        if not math.isfinite(value):
            raise KtrfPhase2Error("Ending.code float must be finite")
        return
    raise KtrfPhase2Error(
        "binary phase 2 only defines null/bool/int64/float64/string Ending.code values"
    )


def _validate_ending_row(row: Mapping[str, Any]) -> None:
    _require_phase2_string(row.get("id"), "Ending.id")
    if "label" in row:
        _require_phase2_string(row.get("label"), "Ending.label")
    if "code" in row:
        _validate_ending_code(row.get("code"))


def collect_phase2_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase1.collect_phase1_strings(document))
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc

    for row in _sorted_catalog(document, "resource_locators", "ResourceLocator"):
        _validate_resource_row(row)
        result.add(row["id"])
        result.add(row["scheme"])
        result.add(row["value"])
        if "media_type" in row:
            result.add(row["media_type"])

    for row in _sorted_catalog(document, "external_hooks", "ExternalHook"):
        _validate_hook_row(row)
        result.add(row["id"])
        result.add(row["symbol"])
        result.add(row["contract"])

    for row in _sorted_catalog(document, "endings", "Ending"):
        _validate_ending_row(row)
        result.add(row["id"])
        if "label" in row:
            result.add(row["label"])
        if isinstance(row.get("code"), str):
            result.add(row["code"])

    return result


def encode_resources(
    document: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[bytes, int]:
    rows = _sorted_catalog(document, "resource_locators", "ResourceLocator")
    payload = bytearray()

    for row in rows:
        _validate_resource_row(row)
        media_type_i = container.NULL_INDEX
        if "media_type" in row:
            media_type_i = _index(
                strings, row["media_type"], "ResourceLocator.media_type"
            )

        flags = 0
        if "required" in row:
            flags |= RSRC_HAS_REQUIRED
            if row["required"]:
                flags |= RSRC_REQUIRED

        payload.extend(
            RSRC_STRUCT.pack(
                _index(strings, row["id"], "ResourceLocator.id"),
                _index(strings, row["scheme"], "ResourceLocator.scheme"),
                _index(strings, row["value"], "ResourceLocator.value"),
                media_type_i,
                flags,
                0,
            )
        )

    return bytes(payload), len(rows)


def decode_resources(
    payload: bytes, strings: Sequence[str], count: int
) -> list[dict[str, Any]]:
    if len(payload) != count * RSRC_RECORD_SIZE:
        raise KtrfPhase2Error("RSRC payload size/item_count mismatch")

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    for index in range(count):
        (
            id_i,
            scheme_i,
            value_i,
            media_type_i,
            flags,
            reserved,
        ) = RSRC_STRUCT.unpack_from(payload, index * RSRC_RECORD_SIZE)

        if reserved:
            raise KtrfPhase2Error("RSRC reserved field must be zero")
        if flags & ~RSRC_FLAGS_KNOWN:
            raise KtrfPhase2Error(
                f"RSRC record {index} has unknown flags 0x{flags:08X}"
            )
        if flags & RSRC_REQUIRED and not flags & RSRC_HAS_REQUIRED:
            raise KtrfPhase2Error("RSRC REQUIRED bit requires HAS_REQUIRED")

        record_id = _string(strings, id_i, "RSRC.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase2Error("RSRC records are not in canonical id order")
        previous = key

        row: dict[str, Any] = {
            "id": record_id,
            "scheme": _string(strings, scheme_i, "RSRC.scheme"),
            "value": _string(strings, value_i, "RSRC.value"),
        }
        if media_type_i != container.NULL_INDEX:
            row["media_type"] = _string(strings, media_type_i, "RSRC.media_type")
        if flags & RSRC_HAS_REQUIRED:
            row["required"] = bool(flags & RSRC_REQUIRED)
        result.append(row)

    return result


def encode_hooks(
    document: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[bytes, int]:
    rows = _sorted_catalog(document, "external_hooks", "ExternalHook")
    payload = bytearray()

    for row in rows:
        _validate_hook_row(row)
        payload.extend(
            HOOK_STRUCT.pack(
                _index(strings, row["id"], "ExternalHook.id"),
                _index(strings, row["symbol"], "ExternalHook.symbol"),
                _index(strings, row["contract"], "ExternalHook.contract"),
                0,
            )
        )

    return bytes(payload), len(rows)


def decode_hooks(
    payload: bytes, strings: Sequence[str], count: int
) -> list[dict[str, str]]:
    if len(payload) != count * HOOK_RECORD_SIZE:
        raise KtrfPhase2Error("HOOK payload size/item_count mismatch")

    result: list[dict[str, str]] = []
    previous: bytes | None = None
    for index in range(count):
        id_i, symbol_i, contract_i, reserved = HOOK_STRUCT.unpack_from(
            payload, index * HOOK_RECORD_SIZE
        )
        if reserved:
            raise KtrfPhase2Error("HOOK reserved field must be zero")

        record_id = _string(strings, id_i, "HOOK.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase2Error("HOOK records are not in canonical id order")
        previous = key

        result.append(
            {
                "id": record_id,
                "symbol": _string(strings, symbol_i, "HOOK.symbol"),
                "contract": _string(strings, contract_i, "HOOK.contract"),
            }
        )
    return result


def _encode_ending_code(
    row: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[int, int, int]:
    if "code" not in row:
        return ENDG_CODE_ABSENT, 0, 0

    value = row.get("code")
    _validate_ending_code(value)
    if value is None:
        return ENDG_CODE_NULL, 0, 0
    if type(value) is bool:
        return ENDG_CODE_BOOL, 0, 1 if value else 0
    if type(value) is int:
        return ENDG_CODE_INT64, 0, value & 0xFFFFFFFFFFFFFFFF
    if type(value) is float:
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
        return ENDG_CODE_FLOAT64, 0, bits
    if isinstance(value, str):
        return ENDG_CODE_STRING, _index(strings, value, "Ending.code"), 0

    raise AssertionError("unreachable Ending.code kind")


def _decode_ending_code(
    kind: int, payload_a: int, payload_b: int, strings: Sequence[str]
) -> tuple[bool, Any]:
    if kind == ENDG_CODE_ABSENT:
        if payload_a or payload_b:
            raise KtrfPhase2Error("invalid absent ENDG.code payload")
        return False, None
    if kind == ENDG_CODE_NULL:
        if payload_a or payload_b:
            raise KtrfPhase2Error("invalid null ENDG.code payload")
        return True, None
    if kind == ENDG_CODE_BOOL:
        if payload_a or payload_b not in (0, 1):
            raise KtrfPhase2Error("invalid bool ENDG.code payload")
        return True, bool(payload_b)
    if kind == ENDG_CODE_INT64:
        if payload_a:
            raise KtrfPhase2Error("invalid int64 ENDG.code payload")
        value = payload_b
        if value & (1 << 63):
            value -= 1 << 64
        return True, value
    if kind == ENDG_CODE_FLOAT64:
        if payload_a:
            raise KtrfPhase2Error("invalid float64 ENDG.code payload")
        value = struct.unpack("<d", struct.pack("<Q", payload_b))[0]
        if not math.isfinite(value):
            raise KtrfPhase2Error("decoded ENDG.code float is not finite")
        return True, value
    if kind == ENDG_CODE_STRING:
        if payload_b:
            raise KtrfPhase2Error("invalid string ENDG.code payload")
        return True, _string(strings, payload_a, "ENDG.code")

    raise KtrfPhase2Error(f"unknown ENDG code kind {kind}")


def encode_endings(
    document: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[bytes, int]:
    rows = _sorted_catalog(document, "endings", "Ending")
    payload = bytearray()

    for row in rows:
        _validate_ending_row(row)
        label_i = container.NULL_INDEX
        if "label" in row:
            label_i = _index(strings, row["label"], "Ending.label")
        kind, payload_a, payload_b = _encode_ending_code(row, strings)

        payload.extend(
            ENDG_STRUCT.pack(
                _index(strings, row["id"], "Ending.id"),
                label_i,
                kind,
                0,
                0,
                payload_a,
                payload_b,
            )
        )

    return bytes(payload), len(rows)


def decode_endings(
    payload: bytes, strings: Sequence[str], count: int
) -> list[dict[str, Any]]:
    if len(payload) != count * ENDG_RECORD_SIZE:
        raise KtrfPhase2Error("ENDG payload size/item_count mismatch")

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    for index in range(count):
        (
            id_i,
            label_i,
            kind,
            flags,
            reserved,
            payload_a,
            payload_b,
        ) = ENDG_STRUCT.unpack_from(payload, index * ENDG_RECORD_SIZE)

        if flags or reserved:
            raise KtrfPhase2Error("ENDG flags/reserved fields must be zero in v0.1")

        record_id = _string(strings, id_i, "ENDG.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase2Error("ENDG records are not in canonical id order")
        previous = key

        row: dict[str, Any] = {"id": record_id}
        if label_i != container.NULL_INDEX:
            row["label"] = _string(strings, label_i, "ENDG.label")

        has_code, code = _decode_ending_code(kind, payload_a, payload_b, strings)
        if has_code:
            row["code"] = code
        result.append(row)

    return result


def build_phase2_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase2_strings(document)
    strs_payload, string_map = container.encode_strs(all_strings)

    try:
        meta_payload = phase1.encode_meta(document, string_map)
        nspc_payload, nspc_count = phase1.encode_namespaces(document, string_map)
        feat_payload, feat_count = phase1.encode_features(document, string_map)
        entr_payload, entr_count = phase1.encode_entry_points(document, string_map)
        vars_payload, vars_count = phase1.encode_variables(document, string_map)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc

    rsrc_payload, rsrc_count = encode_resources(document, string_map)
    hook_payload, hook_count = encode_hooks(document, string_map)
    endg_payload, endg_count = encode_endings(document, string_map)

    return [
        container.Section(b"META", meta_payload, item_count=1),
        container.Section(
            b"STRS",
            strs_payload,
            item_count=len(container.decode_strs(strs_payload)),
        ),
        container.Section(b"NSPC", nspc_payload, item_count=nspc_count),
        container.Section(b"FEAT", feat_payload, item_count=feat_count),
        container.Section(b"ENTR", entr_payload, item_count=entr_count),
        container.Section(b"VARS", vars_payload, item_count=vars_count),
        container.Section(b"RSRC", rsrc_payload, item_count=rsrc_count),
        container.Section(b"HOOK", hook_payload, item_count=hook_count),
        container.Section(b"ENDG", endg_payload, item_count=endg_count),
    ]


def build_phase2_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase2_sections(document))


def _entry_by_type(
    parsed: container.ParsedContainer,
) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase2_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE2_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE2_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase2Error(
            "missing required phase-2 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase2Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase2Error("META item_count must be 1")

    try:
        result = phase1.decode_meta(parsed.sections[b"META"], strings)
        result["features"] = phase1.decode_features(
            parsed.sections[b"FEAT"], strings, entries[b"FEAT"].item_count
        )
        result["namespaces"] = phase1.decode_namespaces(
            parsed.sections[b"NSPC"], strings, entries[b"NSPC"].item_count
        )
        result["entry_points"] = phase1.decode_entry_points(
            parsed.sections[b"ENTR"], strings, entries[b"ENTR"].item_count
        )
        result["variables"] = phase1.decode_variables(
            parsed.sections[b"VARS"], strings, entries[b"VARS"].item_count
        )
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc

    result["resource_locators"] = decode_resources(
        parsed.sections[b"RSRC"], strings, entries[b"RSRC"].item_count
    )
    result["external_hooks"] = decode_hooks(
        parsed.sections[b"HOOK"], strings, entries[b"HOOK"].item_count
    )
    result["endings"] = decode_endings(
        parsed.sections[b"ENDG"], strings, entries[b"ENDG"].item_count
    )
    return result


def _clean_resource_projection(row: Mapping[str, Any]) -> dict[str, Any]:
    _validate_resource_row(row)
    result: dict[str, Any] = {
        "id": row["id"],
        "scheme": row["scheme"],
        "value": row["value"],
    }
    if "media_type" in row:
        result["media_type"] = row["media_type"]
    if "required" in row:
        result["required"] = row["required"]
    return result


def _clean_hook_projection(row: Mapping[str, Any]) -> dict[str, str]:
    _validate_hook_row(row)
    return {
        "id": row["id"],
        "symbol": row["symbol"],
        "contract": row["contract"],
    }


def _clean_ending_projection(row: Mapping[str, Any]) -> dict[str, Any]:
    _validate_ending_row(row)
    result: dict[str, Any] = {"id": row["id"]}
    if "code" in row:
        result["code"] = copy.deepcopy(row["code"])
    if "label" in row:
        result["label"] = row["label"]
    return result


def canonical_phase2_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    """Return the semantic subset represented by phase-2 tables in canonical order."""
    try:
        projection = phase1.canonical_phase1_projection(document)
    except phase1.KtrfPhase1Error as exc:
        raise KtrfPhase2Error(str(exc)) from exc

    projection["resource_locators"] = [
        _clean_resource_projection(row)
        for row in _sorted_catalog(document, "resource_locators", "ResourceLocator")
    ]
    projection["external_hooks"] = [
        _clean_hook_projection(row)
        for row in _sorted_catalog(document, "external_hooks", "ExternalHook")
    ]
    projection["endings"] = [
        _clean_ending_projection(row)
        for row in _sorted_catalog(document, "endings", "Ending")
    ]
    return projection
