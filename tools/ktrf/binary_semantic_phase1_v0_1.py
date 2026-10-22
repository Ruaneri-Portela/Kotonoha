#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 1.

This layer serializes the frozen IR identity/foundation tables into the generic
container implemented by binary_container_v0_1.py:

    META, STRS, NSPC, FEAT, ENTR, VARS

It deliberately does not encode graph entities yet.  The output is therefore a
phase-1 conformance artifact, not a complete routable .ktnroute document.
"""

from __future__ import annotations

import base64
import copy
import struct
import sys
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))
import binary_container_v0_1 as container  # noqa: E402


META_STRUCT = struct.Struct("<8I")
PAIR_STRUCT = struct.Struct("<2I")
FEATURE_STRUCT = struct.Struct("<3I")
ENTRY_STRUCT = struct.Struct("<3I")
VARS_HEADER_STRUCT = struct.Struct("<4I")
VARS_RECORD_STRUCT = struct.Struct("<IIIBBHIIQ")

META_RECORD_SIZE = META_STRUCT.size
NSPC_RECORD_SIZE = PAIR_STRUCT.size
FEAT_RECORD_SIZE = FEATURE_STRUCT.size
ENTR_RECORD_SIZE = ENTRY_STRUCT.size
VARS_RECORD_SIZE = VARS_RECORD_STRUCT.size

FEATURE_REQUIRED = 0x00000001
FEATURE_OPTIONAL = 0x00000002
FEATURE_FLAGS_KNOWN = FEATURE_REQUIRED | FEATURE_OPTIONAL

VAR_DEFAULT_BOOL = 1
VAR_DEFAULT_INT32 = 2
VAR_DEFAULT_UINT32 = 3
VAR_DEFAULT_FLOAT32 = 4
VAR_DEFAULT_FLOAT64 = 5
VAR_DEFAULT_STRING = 6
VAR_DEFAULT_BYTES = 7

CORE_VARIABLE_KIND = {
    "ktrf:bool": VAR_DEFAULT_BOOL,
    "ktrf:int32": VAR_DEFAULT_INT32,
    "ktrf:uint32": VAR_DEFAULT_UINT32,
    "ktrf:float32": VAR_DEFAULT_FLOAT32,
    "ktrf:float64": VAR_DEFAULT_FLOAT64,
    "ktrf:string": VAR_DEFAULT_STRING,
    "ktrf:bytes": VAR_DEFAULT_BYTES,
}

PHASE1_SECTION_TYPES = (b"META", b"STRS", b"NSPC", b"FEAT", b"ENTR", b"VARS")


class KtrfPhase1Error(container.KtrfBinaryError):
    pass


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise KtrfPhase1Error(f"{label} must be an object")
    return value


def _require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise KtrfPhase1Error(f"{label} must be an array")
    return value


def _require_string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise KtrfPhase1Error(f"{label} must be a string")
    if "\x00" in value:
        raise KtrfPhase1Error(f"{label} contains forbidden U+0000")
    return value


def _index(strings: Mapping[str, int], value: str, label: str) -> int:
    try:
        return int(strings[value])
    except KeyError as exc:
        raise KtrfPhase1Error(f"{label} is missing from STRS: {value!r}") from exc


def _string(strings: Sequence[str], index: int, label: str) -> str:
    if index >= len(strings):
        raise KtrfPhase1Error(f"{label} STRS index out of range: {index}")
    return strings[index]


def _sorted_records(rows: Iterable[Mapping[str, Any]], key_name: str) -> list[Mapping[str, Any]]:
    values = list(rows)
    try:
        return sorted(values, key=lambda row: _require_string(row.get(key_name), key_name).encode("utf-8"))
    except AttributeError as exc:
        raise KtrfPhase1Error("record is not an object") from exc


def collect_phase1_strings(document: Mapping[str, Any]) -> set[str]:
    result: set[str] = set()

    result.add(_require_string(document.get("format"), "format"))
    result.add(_require_string(document.get("ir_version"), "ir_version"))
    result.add(_require_string(document.get("document_id"), "document_id"))

    profile = _require_mapping(document.get("profile"), "profile")
    result.add(_require_string(profile.get("id"), "profile.id"))
    result.add(_require_string(profile.get("version"), "profile.version"))

    for row in _require_list(document.get("namespaces"), "namespaces"):
        item = _require_mapping(row, "namespace")
        result.add(_require_string(item.get("prefix"), "namespace.prefix"))
        result.add(_require_string(item.get("uri"), "namespace.uri"))

    features = _require_mapping(document.get("features"), "features")
    for class_name in ("required", "optional"):
        for row in _require_list(features.get(class_name), f"features.{class_name}"):
            item = _require_mapping(row, f"features.{class_name}[]")
            result.add(_require_string(item.get("id"), "feature.id"))
            result.add(_require_string(item.get("version"), "feature.version"))

    for row in _require_list(document.get("entry_points"), "entry_points"):
        item = _require_mapping(row, "entry_point")
        result.add(_require_string(item.get("id"), "entry_point.id"))
        result.add(_require_string(item.get("node"), "entry_point.node"))
        result.add(_require_string(item.get("trigger"), "entry_point.trigger"))

    for row in _require_list(document.get("variables"), "variables"):
        item = _require_mapping(row, "variable")
        result.add(_require_string(item.get("id"), "variable.id"))
        result.add(_require_string(item.get("type"), "variable.type"))
        result.add(_require_string(item.get("scope"), "variable.scope"))
        if item.get("type") == "ktrf:string":
            result.add(_require_string(item.get("default"), "variable.default"))

    return result


def encode_meta(document: Mapping[str, Any], strings: Mapping[str, int]) -> bytes:
    profile = _require_mapping(document.get("profile"), "profile")
    values = (
        _index(strings, _require_string(document.get("format"), "format"), "format"),
        _index(strings, _require_string(document.get("ir_version"), "ir_version"), "ir_version"),
        _index(strings, _require_string(document.get("document_id"), "document_id"), "document_id"),
        _index(strings, _require_string(profile.get("id"), "profile.id"), "profile.id"),
        _index(strings, _require_string(profile.get("version"), "profile.version"), "profile.version"),
        0,
        0,
        0,
    )
    return META_STRUCT.pack(*values)


def decode_meta(payload: bytes, strings: Sequence[str]) -> dict[str, Any]:
    if len(payload) != META_RECORD_SIZE:
        raise KtrfPhase1Error(f"META payload must be exactly {META_RECORD_SIZE} bytes")
    format_i, ir_version_i, document_i, profile_i, profile_version_i, r0, r1, r2 = META_STRUCT.unpack(payload)
    if r0 or r1 or r2:
        raise KtrfPhase1Error("META reserved fields must be zero")
    return {
        "format": _string(strings, format_i, "META.format"),
        "ir_version": _string(strings, ir_version_i, "META.ir_version"),
        "document_id": _string(strings, document_i, "META.document_id"),
        "profile": {
            "id": _string(strings, profile_i, "META.profile.id"),
            "version": _string(strings, profile_version_i, "META.profile.version"),
        },
    }


def encode_namespaces(document: Mapping[str, Any], strings: Mapping[str, int]) -> tuple[bytes, int]:
    rows = [_require_mapping(row, "namespace") for row in _require_list(document.get("namespaces"), "namespaces")]
    rows = _sorted_records(rows, "prefix")
    payload = bytearray()
    seen: set[str] = set()
    for row in rows:
        prefix = _require_string(row.get("prefix"), "namespace.prefix")
        if prefix in seen:
            raise KtrfPhase1Error(f"duplicate namespace prefix {prefix!r}")
        seen.add(prefix)
        uri = _require_string(row.get("uri"), "namespace.uri")
        payload.extend(PAIR_STRUCT.pack(_index(strings, prefix, "namespace.prefix"), _index(strings, uri, "namespace.uri")))
    return bytes(payload), len(rows)


def decode_namespaces(payload: bytes, strings: Sequence[str], count: int) -> list[dict[str, str]]:
    if len(payload) != count * NSPC_RECORD_SIZE:
        raise KtrfPhase1Error("NSPC payload size/item_count mismatch")
    result: list[dict[str, str]] = []
    previous: bytes | None = None
    for index in range(count):
        prefix_i, uri_i = PAIR_STRUCT.unpack_from(payload, index * NSPC_RECORD_SIZE)
        prefix = _string(strings, prefix_i, "NSPC.prefix")
        uri = _string(strings, uri_i, "NSPC.uri")
        key = prefix.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase1Error("NSPC records are not in canonical prefix order")
        previous = key
        result.append({"prefix": prefix, "uri": uri})
    return result


def encode_features(document: Mapping[str, Any], strings: Mapping[str, int]) -> tuple[bytes, int]:
    features = _require_mapping(document.get("features"), "features")
    merged: list[tuple[int, Mapping[str, Any]]] = []
    for class_name, flag in (("required", FEATURE_REQUIRED), ("optional", FEATURE_OPTIONAL)):
        rows = [_require_mapping(row, f"features.{class_name}[]") for row in _require_list(features.get(class_name), f"features.{class_name}")]
        for row in rows:
            merged.append((flag, row))
    merged.sort(key=lambda pair: (0 if pair[0] == FEATURE_REQUIRED else 1, _require_string(pair[1].get("id"), "feature.id").encode("utf-8"), _require_string(pair[1].get("version"), "feature.version").encode("utf-8")))

    payload = bytearray()
    seen: set[tuple[int, str]] = set()
    for flag, row in merged:
        feature_id = _require_string(row.get("id"), "feature.id")
        version = _require_string(row.get("version"), "feature.version")
        key = (flag, feature_id)
        if key in seen:
            raise KtrfPhase1Error(f"duplicate feature {feature_id!r}")
        seen.add(key)
        payload.extend(FEATURE_STRUCT.pack(_index(strings, feature_id, "feature.id"), _index(strings, version, "feature.version"), flag))
    return bytes(payload), len(merged)


def decode_features(payload: bytes, strings: Sequence[str], count: int) -> dict[str, list[dict[str, str]]]:
    if len(payload) != count * FEAT_RECORD_SIZE:
        raise KtrfPhase1Error("FEAT payload size/item_count mismatch")
    result = {"required": [], "optional": []}
    previous_key: tuple[int, bytes, bytes] | None = None
    seen: set[tuple[int, str]] = set()
    for index in range(count):
        id_i, version_i, flags = FEATURE_STRUCT.unpack_from(payload, index * FEAT_RECORD_SIZE)
        if flags not in (FEATURE_REQUIRED, FEATURE_OPTIONAL) or flags & ~FEATURE_FLAGS_KNOWN:
            raise KtrfPhase1Error(f"FEAT record {index} has invalid flags 0x{flags:08X}")
        feature_id = _string(strings, id_i, "FEAT.id")
        version = _string(strings, version_i, "FEAT.version")
        class_rank = 0 if flags == FEATURE_REQUIRED else 1
        key = (class_rank, feature_id.encode("utf-8"), version.encode("utf-8"))
        if previous_key is not None and key <= previous_key:
            raise KtrfPhase1Error("FEAT records are not in canonical order")
        previous_key = key
        duplicate_key = (flags, feature_id)
        if duplicate_key in seen:
            raise KtrfPhase1Error(f"duplicate FEAT id {feature_id!r}")
        seen.add(duplicate_key)
        target = "required" if flags == FEATURE_REQUIRED else "optional"
        result[target].append({"id": feature_id, "version": version})
    return result


def encode_entry_points(document: Mapping[str, Any], strings: Mapping[str, int]) -> tuple[bytes, int]:
    rows = [_require_mapping(row, "entry_point") for row in _require_list(document.get("entry_points"), "entry_points")]
    rows = _sorted_records(rows, "id")
    payload = bytearray()
    seen: set[str] = set()
    for row in rows:
        record_id = _require_string(row.get("id"), "entry_point.id")
        if record_id in seen:
            raise KtrfPhase1Error(f"duplicate EntryPoint id {record_id!r}")
        seen.add(record_id)
        node = _require_string(row.get("node"), "entry_point.node")
        trigger = _require_string(row.get("trigger"), "entry_point.trigger")
        payload.extend(ENTRY_STRUCT.pack(_index(strings, record_id, "entry_point.id"), _index(strings, node, "entry_point.node"), _index(strings, trigger, "entry_point.trigger")))
    return bytes(payload), len(rows)


def decode_entry_points(payload: bytes, strings: Sequence[str], count: int) -> list[dict[str, str]]:
    if len(payload) != count * ENTR_RECORD_SIZE:
        raise KtrfPhase1Error("ENTR payload size/item_count mismatch")
    result: list[dict[str, str]] = []
    previous: bytes | None = None
    for index in range(count):
        id_i, node_i, trigger_i = ENTRY_STRUCT.unpack_from(payload, index * ENTR_RECORD_SIZE)
        record_id = _string(strings, id_i, "ENTR.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase1Error("ENTR records are not in canonical id order")
        previous = key
        result.append({
            "id": record_id,
            "node": _string(strings, node_i, "ENTR.node"),
            "trigger": _string(strings, trigger_i, "ENTR.trigger"),
        })
    return result


def _encode_variable_default(type_name: str, value: Any, strings: Mapping[str, int], blob: bytearray) -> tuple[int, int, int, int]:
    kind = CORE_VARIABLE_KIND.get(type_name)
    if kind is None:
        raise KtrfPhase1Error(f"binary phase 1 does not define defaults for variable type {type_name!r}")

    if kind == VAR_DEFAULT_BOOL:
        if type(value) is not bool:
            raise KtrfPhase1Error("ktrf:bool default must be boolean")
        return kind, 0, 0, 1 if value else 0

    if kind == VAR_DEFAULT_INT32:
        if type(value) is not int or not (-0x80000000 <= value <= 0x7FFFFFFF):
            raise KtrfPhase1Error("ktrf:int32 default is out of range")
        return kind, 0, 0, value & 0xFFFFFFFF

    if kind == VAR_DEFAULT_UINT32:
        if type(value) is not int or not (0 <= value <= 0xFFFFFFFF):
            raise KtrfPhase1Error("ktrf:uint32 default is out of range")
        return kind, 0, 0, value

    if kind == VAR_DEFAULT_FLOAT32:
        if type(value) not in (int, float) or type(value) is bool:
            raise KtrfPhase1Error("ktrf:float32 default must be numeric")
        raw = struct.unpack("<I", struct.pack("<f", float(value)))[0]
        return kind, 0, 0, raw

    if kind == VAR_DEFAULT_FLOAT64:
        if type(value) not in (int, float) or type(value) is bool:
            raise KtrfPhase1Error("ktrf:float64 default must be numeric")
        raw = struct.unpack("<Q", struct.pack("<d", float(value)))[0]
        return kind, 0, 0, raw

    if kind == VAR_DEFAULT_STRING:
        text = _require_string(value, "variable.default")
        return kind, _index(strings, text, "variable.default"), 0, 0

    if kind == VAR_DEFAULT_BYTES:
        text = _require_string(value, "variable.default")
        try:
            decoded = base64.b64decode(text.encode("ascii"), validate=True)
        except Exception as exc:
            raise KtrfPhase1Error("ktrf:bytes default must be canonical base64 text") from exc
        canonical = base64.b64encode(decoded).decode("ascii")
        if canonical != text:
            raise KtrfPhase1Error("ktrf:bytes default must use canonical base64 encoding")
        offset = len(blob)
        blob.extend(decoded)
        return kind, offset, len(decoded), 0

    raise AssertionError("unreachable variable default kind")


def encode_variables(document: Mapping[str, Any], strings: Mapping[str, int]) -> tuple[bytes, int]:
    rows = [_require_mapping(row, "variable") for row in _require_list(document.get("variables"), "variables")]
    rows = _sorted_records(rows, "id")
    records = bytearray()
    blob = bytearray()
    seen: set[str] = set()

    for row in rows:
        record_id = _require_string(row.get("id"), "variable.id")
        if record_id in seen:
            raise KtrfPhase1Error(f"duplicate Variable id {record_id!r}")
        seen.add(record_id)
        type_name = _require_string(row.get("type"), "variable.type")
        scope = _require_string(row.get("scope"), "variable.scope")
        kind, payload_a, payload_b, payload_c = _encode_variable_default(type_name, row.get("default"), strings, blob)
        records.extend(VARS_RECORD_STRUCT.pack(
            _index(strings, record_id, "variable.id"),
            _index(strings, type_name, "variable.type"),
            _index(strings, scope, "variable.scope"),
            kind,
            0,
            0,
            payload_a,
            payload_b,
            payload_c,
        ))

    blob_offset = VARS_HEADER_STRUCT.size + len(records)
    header = VARS_HEADER_STRUCT.pack(len(rows), VARS_RECORD_SIZE, blob_offset, len(blob))
    return header + bytes(records) + bytes(blob), len(rows)


def _decode_variable_default(type_name: str, kind: int, payload_a: int, payload_b: int, payload_c: int, strings: Sequence[str], blob: bytes) -> Any:
    expected_kind = CORE_VARIABLE_KIND.get(type_name)
    if expected_kind is None:
        raise KtrfPhase1Error(f"unsupported VARS type {type_name!r}")
    if kind != expected_kind:
        raise KtrfPhase1Error(f"VARS type/default-kind mismatch for {type_name!r}: {kind} != {expected_kind}")

    if kind == VAR_DEFAULT_BOOL:
        if payload_a or payload_b or payload_c not in (0, 1):
            raise KtrfPhase1Error("invalid bool default payload")
        return bool(payload_c)
    if kind == VAR_DEFAULT_INT32:
        if payload_a or payload_b or payload_c > 0xFFFFFFFF:
            raise KtrfPhase1Error("invalid int32 default payload")
        value = payload_c & 0xFFFFFFFF
        return value - 0x100000000 if value & 0x80000000 else value
    if kind == VAR_DEFAULT_UINT32:
        if payload_a or payload_b or payload_c > 0xFFFFFFFF:
            raise KtrfPhase1Error("invalid uint32 default payload")
        return int(payload_c)
    if kind == VAR_DEFAULT_FLOAT32:
        if payload_a or payload_b or payload_c > 0xFFFFFFFF:
            raise KtrfPhase1Error("invalid float32 default payload")
        return struct.unpack("<f", struct.pack("<I", payload_c))[0]
    if kind == VAR_DEFAULT_FLOAT64:
        if payload_a or payload_b:
            raise KtrfPhase1Error("invalid float64 default payload")
        return struct.unpack("<d", struct.pack("<Q", payload_c))[0]
    if kind == VAR_DEFAULT_STRING:
        if payload_b or payload_c:
            raise KtrfPhase1Error("invalid string default payload")
        return _string(strings, payload_a, "VARS.default-string")
    if kind == VAR_DEFAULT_BYTES:
        if payload_c:
            raise KtrfPhase1Error("invalid bytes default payload")
        end = payload_a + payload_b
        if end > len(blob):
            raise KtrfPhase1Error("bytes default points outside VARS blob")
        return base64.b64encode(blob[payload_a:end]).decode("ascii")
    raise KtrfPhase1Error(f"unknown VARS default kind {kind}")


def decode_variables(payload: bytes, strings: Sequence[str], section_item_count: int) -> list[dict[str, Any]]:
    if len(payload) < VARS_HEADER_STRUCT.size:
        raise KtrfPhase1Error("VARS payload is too small")
    count, record_size, blob_offset, blob_size = VARS_HEADER_STRUCT.unpack_from(payload, 0)
    if count != section_item_count:
        raise KtrfPhase1Error("VARS header count does not match section item_count")
    if record_size != VARS_RECORD_SIZE:
        raise KtrfPhase1Error(f"unsupported VARS record size {record_size}")
    expected_blob_offset = VARS_HEADER_STRUCT.size + count * record_size
    if blob_offset != expected_blob_offset:
        raise KtrfPhase1Error("VARS blob_offset is not canonical")
    if blob_offset + blob_size != len(payload):
        raise KtrfPhase1Error("VARS blob size does not match payload")
    blob = payload[blob_offset:]

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    for index in range(count):
        offset = VARS_HEADER_STRUCT.size + index * record_size
        id_i, type_i, scope_i, kind, flags, reserved, payload_a, payload_b, payload_c = VARS_RECORD_STRUCT.unpack_from(payload, offset)
        if flags or reserved:
            raise KtrfPhase1Error("VARS flags/reserved fields must be zero in v0.1")
        record_id = _string(strings, id_i, "VARS.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase1Error("VARS records are not in canonical id order")
        previous = key
        type_name = _string(strings, type_i, "VARS.type")
        scope = _string(strings, scope_i, "VARS.scope")
        default = _decode_variable_default(type_name, kind, payload_a, payload_b, payload_c, strings, blob)
        result.append({"id": record_id, "type": type_name, "scope": scope, "default": default})
    return result


def build_phase1_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase1_strings(document)
    strs_payload, string_map = container.encode_strs(all_strings)
    meta_payload = encode_meta(document, string_map)
    nspc_payload, nspc_count = encode_namespaces(document, string_map)
    feat_payload, feat_count = encode_features(document, string_map)
    entr_payload, entr_count = encode_entry_points(document, string_map)
    vars_payload, vars_count = encode_variables(document, string_map)

    return [
        container.Section(b"META", meta_payload, item_count=1),
        container.Section(b"STRS", strs_payload, item_count=len(container.decode_strs(strs_payload))),
        container.Section(b"NSPC", nspc_payload, item_count=nspc_count),
        container.Section(b"FEAT", feat_payload, item_count=feat_count),
        container.Section(b"ENTR", entr_payload, item_count=entr_count),
        container.Section(b"VARS", vars_payload, item_count=vars_count),
    ]


def build_phase1_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase1_sections(document))


def _entry_by_type(parsed: container.ParsedContainer) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase1_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE1_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE1_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase1Error("missing required phase-1 sections: " + ",".join(code.decode("ascii") for code in missing))

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase1Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase1Error("META item_count must be 1")

    result = decode_meta(parsed.sections[b"META"], strings)
    result["features"] = decode_features(parsed.sections[b"FEAT"], strings, entries[b"FEAT"].item_count)
    result["namespaces"] = decode_namespaces(parsed.sections[b"NSPC"], strings, entries[b"NSPC"].item_count)
    result["entry_points"] = decode_entry_points(parsed.sections[b"ENTR"], strings, entries[b"ENTR"].item_count)
    result["variables"] = decode_variables(parsed.sections[b"VARS"], strings, entries[b"VARS"].item_count)
    return result


def canonical_phase1_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    """Return the semantic subset represented by phase-1 tables in canonical order."""
    clone = copy.deepcopy(dict(document))
    projection = {
        "format": clone["format"],
        "ir_version": clone["ir_version"],
        "document_id": clone["document_id"],
        "profile": {
            "id": clone["profile"]["id"],
            "version": clone["profile"]["version"],
        },
        "features": {
            "required": sorted(clone["features"]["required"], key=lambda row: (row["id"].encode("utf-8"), row["version"].encode("utf-8"))),
            "optional": sorted(clone["features"]["optional"], key=lambda row: (row["id"].encode("utf-8"), row["version"].encode("utf-8"))),
        },
        "namespaces": sorted(clone["namespaces"], key=lambda row: row["prefix"].encode("utf-8")),
        "entry_points": sorted(clone["entry_points"], key=lambda row: row["id"].encode("utf-8")),
        "variables": sorted(clone["variables"], key=lambda row: row["id"].encode("utf-8")),
    }
    for row in projection["entry_points"]:
        row.pop("metadata", None)
        row.pop("extensions", None)
    for row in projection["variables"]:
        row.pop("metadata", None)
        row.pop("extensions", None)
    return projection
