#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 5 / CHOI.

Extends phase 4 with canonical Choice records. Choice records are canonically
ordered by stable semantic ID, while option order and referenced Effect order
remain semantic and are preserved exactly.
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

import binary_semantic_phase4_v0_1 as phase4  # noqa: E402

phase3 = phase4.phase3
phase2 = phase4.phase2
phase1 = phase4.phase1
container = phase4.container

CHOI_HEADER_STRUCT = struct.Struct("<12I")
CHOI_RECORD_STRUCT = struct.Struct("<8I")
CHOI_OPTION_STRUCT = struct.Struct("<IIBBHIQIII")
CHOI_TIMEOUT_STRUCT = struct.Struct("<BBHIQIII")
CHOI_EFFECT_REF_STRUCT = struct.Struct("<I")

CHOI_HEADER_SIZE = CHOI_HEADER_STRUCT.size
CHOI_RECORD_SIZE = CHOI_RECORD_STRUCT.size
CHOI_OPTION_RECORD_SIZE = CHOI_OPTION_STRUCT.size
CHOI_TIMEOUT_RECORD_SIZE = CHOI_TIMEOUT_STRUCT.size
CHOI_EFFECT_REF_RECORD_SIZE = CHOI_EFFECT_REF_STRUCT.size

VALUE_NULL = 1
VALUE_BOOL = 2
VALUE_INT64 = 3
VALUE_FLOAT64 = 4
VALUE_STRING = 5

PHASE5_SECTION_TYPES = phase4.PHASE4_SECTION_TYPES + (b"CHOI",)


class KtrfPhase5Error(phase4.KtrfPhase4Error):
    pass


def _error_from(exc: Exception) -> KtrfPhase5Error:
    return KtrfPhase5Error(str(exc))


def _require_string(value: Any, label: str) -> str:
    try:
        return phase1._require_string(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    try:
        return phase1._require_mapping(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc


def _require_list(value: Any, label: str) -> list[Any]:
    try:
        return phase1._require_list(value, label)
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc


def _index(strings: Mapping[str, int], value: str, label: str) -> int:
    try:
        return phase1._index(strings, value, label)
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc


def _string(strings: Sequence[str], index: int, label: str) -> str:
    try:
        return phase1._string(strings, index, label)
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc


def _sorted_choices(document: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    rows = [
        _require_mapping(row, "Choice")
        for row in _require_list(document.get("choices"), "choices")
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc
    seen: set[str] = set()
    for row in rows:
        choice_id = _require_string(row.get("id"), "Choice.id")
        if choice_id in seen:
            raise KtrfPhase5Error(f"duplicate Choice id {choice_id!r}")
        seen.add(choice_id)
    return rows


def choice_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    return {row["id"]: index for index, row in enumerate(_sorted_choices(document))}


def _validate_scalar(value: Any, label: str) -> None:
    if value is None or type(value) is bool or isinstance(value, str):
        if isinstance(value, str):
            _require_string(value, label)
        return
    if type(value) is int:
        if not (-0x8000000000000000 <= value <= 0x7FFFFFFFFFFFFFFF):
            raise KtrfPhase5Error(f"{label} integer is out of signed int64 range")
        return
    if type(value) is float:
        if not math.isfinite(value):
            raise KtrfPhase5Error(f"{label} float must be finite")
        return
    raise KtrfPhase5Error(
        f"binary phase 5 only defines null/bool/int64/float64/string {label} values"
    )


def _encode_scalar(value: Any, strings: Mapping[str, int], label: str) -> tuple[int, int, int]:
    _validate_scalar(value, label)
    if value is None:
        return VALUE_NULL, 0, 0
    if type(value) is bool:
        return VALUE_BOOL, 0, 1 if value else 0
    if type(value) is int:
        return VALUE_INT64, 0, value & 0xFFFFFFFFFFFFFFFF
    if type(value) is float:
        return VALUE_FLOAT64, 0, struct.unpack("<Q", struct.pack("<d", value))[0]
    if isinstance(value, str):
        return VALUE_STRING, _index(strings, value, label), 0
    raise AssertionError("unreachable Choice scalar kind")


def _decode_scalar(kind: int, payload_a: int, payload_b: int, strings: Sequence[str], label: str) -> Any:
    if kind == VALUE_NULL:
        if payload_a or payload_b:
            raise KtrfPhase5Error(f"invalid null {label} payload")
        return None
    if kind == VALUE_BOOL:
        if payload_a or payload_b not in (0, 1):
            raise KtrfPhase5Error(f"invalid bool {label} payload")
        return bool(payload_b)
    if kind == VALUE_INT64:
        if payload_a:
            raise KtrfPhase5Error(f"invalid int64 {label} payload")
        value = payload_b
        return value - (1 << 64) if value & (1 << 63) else value
    if kind == VALUE_FLOAT64:
        if payload_a:
            raise KtrfPhase5Error(f"invalid float64 {label} payload")
        value = struct.unpack("<d", struct.pack("<Q", payload_b))[0]
        if not math.isfinite(value):
            raise KtrfPhase5Error(f"decoded {label} float is not finite")
        return value
    if kind == VALUE_STRING:
        if payload_b:
            raise KtrfPhase5Error(f"invalid string {label} payload")
        return _string(strings, payload_a, label)
    raise KtrfPhase5Error(f"unknown {label} kind {kind}")


def _validate_choices(
    document: Mapping[str, Any], rows: Sequence[Mapping[str, Any]]
) -> tuple[dict[str, int], dict[str, int]]:
    try:
        var_map = phase3.variable_index_map(document)
        effect_map = phase4.effect_index_map(document)
    except (phase3.KtrfPhase3Error, phase4.KtrfPhase4Error) as exc:
        raise _error_from(exc) from exc

    nodes_raw = _require_list(document.get("nodes"), "nodes")
    node_ids = {
        _require_string(_require_mapping(row, "Node").get("id"), "Node.id")
        for row in nodes_raw
    }

    for row in rows:
        choice_id = row["id"]
        node = _require_string(row.get("node"), f"Choice {choice_id}.node")
        if node not in node_ids:
            raise KtrfPhase5Error(f"Choice {choice_id!r} references unknown node {node!r}")
        result_var = _require_string(
            row.get("result_variable"), f"Choice {choice_id}.result_variable"
        )
        if result_var not in var_map:
            raise KtrfPhase5Error(
                f"Choice {choice_id!r} references unknown result variable {result_var!r}"
            )
        _require_string(row.get("routing_policy"), f"Choice {choice_id}.routing_policy")
        options = _require_list(row.get("options"), f"Choice {choice_id}.options")
        if not options:
            raise KtrfPhase5Error(f"Choice {choice_id!r} must contain at least one option")

        seen_options: set[str] = set()
        for option_index, raw_option in enumerate(options):
            option = _require_mapping(raw_option, f"Choice {choice_id} option {option_index}")
            option_id = _require_string(option.get("id"), f"Choice {choice_id} option.id")
            if option_id in seen_options:
                raise KtrfPhase5Error(
                    f"Choice {choice_id!r} has duplicate option id {option_id!r}"
                )
            seen_options.add(option_id)
            if "value" not in option:
                raise KtrfPhase5Error(f"Choice {choice_id!r} option {option_id!r} has no value")
            _validate_scalar(option.get("value"), "Choice option value")
            if "label" in option:
                _require_string(option.get("label"), "Choice option label")
            refs = _require_list(option.get("effects"), "Choice option effects")
            for effect_id in refs:
                ref = _require_string(effect_id, "Choice option effect")
                if ref not in effect_map:
                    raise KtrfPhase5Error(
                        f"Choice {choice_id!r} option {option_id!r} references unknown effect {ref!r}"
                    )

        if "timeout" in row:
            timeout = _require_mapping(row.get("timeout"), f"Choice {choice_id}.timeout")
            if "value" not in timeout:
                raise KtrfPhase5Error(f"Choice {choice_id!r} timeout has no value")
            _validate_scalar(timeout.get("value"), "Choice timeout value")
            refs = _require_list(timeout.get("effects"), "Choice timeout effects")
            for effect_id in refs:
                ref = _require_string(effect_id, "Choice timeout effect")
                if ref not in effect_map:
                    raise KtrfPhase5Error(
                        f"Choice {choice_id!r} timeout references unknown effect {ref!r}"
                    )

    return var_map, effect_map


def collect_phase5_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase4.collect_phase4_strings(document))
    except phase4.KtrfPhase4Error as exc:
        raise _error_from(exc) from exc
    rows = _sorted_choices(document)
    _validate_choices(document, rows)
    for row in rows:
        result.add(row["id"])
        result.add(row["node"])
        result.add(row["routing_policy"])
        for raw_option in row["options"]:
            option = _require_mapping(raw_option, "Choice option")
            result.add(option["id"])
            if "label" in option:
                result.add(option["label"])
            if isinstance(option.get("value"), str):
                result.add(option["value"])
        if "timeout" in row:
            timeout = _require_mapping(row["timeout"], "Choice timeout")
            if isinstance(timeout.get("value"), str):
                result.add(timeout["value"])
    return result


def encode_choices(document: Mapping[str, Any], strings: Mapping[str, int]) -> tuple[bytes, int]:
    rows = _sorted_choices(document)
    var_map, effect_map = _validate_choices(document, rows)

    choice_records = bytearray()
    option_records = bytearray()
    timeout_records = bytearray()
    effect_refs = bytearray()
    option_count = 0
    timeout_count = 0
    effect_ref_count = 0

    def append_effects(refs: Sequence[Any], label: str) -> tuple[int, int]:
        nonlocal effect_ref_count
        start = effect_ref_count
        for raw_ref in refs:
            ref = _require_string(raw_ref, label)
            effect_refs.extend(CHOI_EFFECT_REF_STRUCT.pack(effect_map[ref]))
            effect_ref_count += 1
        return start, len(refs)

    for row in rows:
        option_start = option_count
        for raw_option in row["options"]:
            option = _require_mapping(raw_option, "Choice option")
            kind, payload_a, payload_b = _encode_scalar(
                option["value"], strings, "Choice option value"
            )
            label_i = container.NULL_INDEX
            if "label" in option:
                label_i = _index(strings, option["label"], "Choice option label")
            effect_start, local_effect_count = append_effects(
                option["effects"], "Choice option effect"
            )
            option_records.extend(
                CHOI_OPTION_STRUCT.pack(
                    _index(strings, option["id"], "Choice option id"),
                    label_i,
                    kind,
                    0,
                    0,
                    payload_a,
                    payload_b,
                    effect_start,
                    local_effect_count,
                    0,
                )
            )
            option_count += 1

        timeout_index = container.NULL_INDEX
        if "timeout" in row:
            timeout = _require_mapping(row["timeout"], "Choice timeout")
            kind, payload_a, payload_b = _encode_scalar(
                timeout["value"], strings, "Choice timeout value"
            )
            effect_start, local_effect_count = append_effects(
                timeout["effects"], "Choice timeout effect"
            )
            timeout_index = timeout_count
            timeout_records.extend(
                CHOI_TIMEOUT_STRUCT.pack(
                    kind,
                    0,
                    0,
                    payload_a,
                    payload_b,
                    effect_start,
                    local_effect_count,
                    0,
                )
            )
            timeout_count += 1

        choice_records.extend(
            CHOI_RECORD_STRUCT.pack(
                _index(strings, row["id"], "Choice.id"),
                _index(strings, row["node"], "Choice.node"),
                var_map[row["result_variable"]],
                _index(strings, row["routing_policy"], "Choice.routing_policy"),
                option_start,
                len(row["options"]),
                timeout_index,
                0,
            )
        )

    choices_offset = CHOI_HEADER_SIZE
    options_offset = choices_offset + len(rows) * CHOI_RECORD_SIZE
    timeouts_offset = options_offset + option_count * CHOI_OPTION_RECORD_SIZE
    effects_offset = timeouts_offset + timeout_count * CHOI_TIMEOUT_RECORD_SIZE
    header = CHOI_HEADER_STRUCT.pack(
        len(rows),
        CHOI_RECORD_SIZE,
        option_count,
        CHOI_OPTION_RECORD_SIZE,
        timeout_count,
        CHOI_TIMEOUT_RECORD_SIZE,
        effect_ref_count,
        CHOI_EFFECT_REF_RECORD_SIZE,
        choices_offset,
        options_offset,
        timeouts_offset,
        effects_offset,
    )
    return (
        header
        + bytes(choice_records)
        + bytes(option_records)
        + bytes(timeout_records)
        + bytes(effect_refs),
        len(rows),
    )


def decode_choices(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    variable_ids: Sequence[str],
    effect_ids: Sequence[str],
) -> list[dict[str, Any]]:
    if len(payload) < CHOI_HEADER_SIZE:
        raise KtrfPhase5Error("CHOI payload is too small")
    (
        choice_count,
        choice_record_size,
        option_count,
        option_record_size,
        timeout_count,
        timeout_record_size,
        effect_ref_count,
        effect_ref_record_size,
        choices_offset,
        options_offset,
        timeouts_offset,
        effects_offset,
    ) = CHOI_HEADER_STRUCT.unpack_from(payload, 0)

    if choice_count != section_item_count:
        raise KtrfPhase5Error("CHOI header count does not match section item_count")
    if choice_record_size != CHOI_RECORD_SIZE or option_record_size != CHOI_OPTION_RECORD_SIZE:
        raise KtrfPhase5Error("unsupported CHOI record size")
    if timeout_record_size != CHOI_TIMEOUT_RECORD_SIZE or effect_ref_record_size != CHOI_EFFECT_REF_RECORD_SIZE:
        raise KtrfPhase5Error("unsupported CHOI pool record size")
    if choices_offset != CHOI_HEADER_SIZE:
        raise KtrfPhase5Error("CHOI choices_offset is not canonical")
    expected_options_offset = choices_offset + choice_count * CHOI_RECORD_SIZE
    expected_timeouts_offset = expected_options_offset + option_count * CHOI_OPTION_RECORD_SIZE
    expected_effects_offset = expected_timeouts_offset + timeout_count * CHOI_TIMEOUT_RECORD_SIZE
    expected_size = expected_effects_offset + effect_ref_count * CHOI_EFFECT_REF_RECORD_SIZE
    if options_offset != expected_options_offset or timeouts_offset != expected_timeouts_offset or effects_offset != expected_effects_offset:
        raise KtrfPhase5Error("CHOI pool offsets are not canonical")
    if len(payload) != expected_size:
        raise KtrfPhase5Error("CHOI payload size/header counts mismatch")

    def decode_effect_slice(start: int, count: int) -> list[str]:
        if start + count > effect_ref_count:
            raise KtrfPhase5Error("CHOI effect slice is out of range")
        result: list[str] = []
        for index in range(start, start + count):
            (effect_index,) = CHOI_EFFECT_REF_STRUCT.unpack_from(
                payload, effects_offset + index * CHOI_EFFECT_REF_RECORD_SIZE
            )
            if effect_index >= len(effect_ids):
                raise KtrfPhase5Error("CHOI effect index out of range")
            result.append(effect_ids[effect_index])
        return result

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    expected_option_start = 0
    expected_timeout_index = 0
    expected_effect_start = 0

    for choice_index in range(choice_count):
        (
            id_i,
            node_i,
            result_var_i,
            routing_policy_i,
            option_start,
            local_option_count,
            timeout_index,
            reserved,
        ) = CHOI_RECORD_STRUCT.unpack_from(
            payload, choices_offset + choice_index * CHOI_RECORD_SIZE
        )
        if reserved:
            raise KtrfPhase5Error("CHOI reserved field must be zero")
        if option_start != expected_option_start or option_start + local_option_count > option_count:
            raise KtrfPhase5Error("CHOI option slices are not canonical/contiguous")
        expected_option_start += local_option_count
        choice_id = _string(strings, id_i, "CHOI.id")
        key = choice_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase5Error("CHOI records are not in canonical id order")
        previous = key
        if result_var_i >= len(variable_ids):
            raise KtrfPhase5Error("CHOI result_variable index out of range")

        options: list[dict[str, Any]] = []
        seen_option_ids: set[str] = set()
        for local_index in range(local_option_count):
            option_index = option_start + local_index
            (
                option_id_i,
                label_i,
                value_kind,
                flags,
                reserved0,
                payload_a,
                payload_b,
                effect_start,
                local_effect_count,
                reserved1,
            ) = CHOI_OPTION_STRUCT.unpack_from(
                payload, options_offset + option_index * CHOI_OPTION_RECORD_SIZE
            )
            if flags or reserved0 or reserved1:
                raise KtrfPhase5Error("CHOI option flags/reserved fields must be zero")
            if effect_start != expected_effect_start:
                raise KtrfPhase5Error("CHOI effect slices are not canonical/contiguous")
            effects = decode_effect_slice(effect_start, local_effect_count)
            expected_effect_start += local_effect_count
            option_id = _string(strings, option_id_i, "CHOI option id")
            if option_id in seen_option_ids:
                raise KtrfPhase5Error(f"duplicate CHOI option id {option_id!r}")
            seen_option_ids.add(option_id)
            option: dict[str, Any] = {
                "id": option_id,
                "value": _decode_scalar(value_kind, payload_a, payload_b, strings, "CHOI option value"),
                "effects": effects,
            }
            if label_i != container.NULL_INDEX:
                option["label"] = _string(strings, label_i, "CHOI option label")
            options.append(option)

        row: dict[str, Any] = {
            "id": choice_id,
            "node": _string(strings, node_i, "CHOI.node"),
            "result_variable": variable_ids[result_var_i],
            "routing_policy": _string(strings, routing_policy_i, "CHOI.routing_policy"),
            "options": options,
        }

        if timeout_index != container.NULL_INDEX:
            if timeout_index != expected_timeout_index or timeout_index >= timeout_count:
                raise KtrfPhase5Error("CHOI timeout indices are not canonical")
            (
                value_kind,
                flags,
                reserved0,
                payload_a,
                payload_b,
                effect_start,
                local_effect_count,
                reserved1,
            ) = CHOI_TIMEOUT_STRUCT.unpack_from(
                payload, timeouts_offset + timeout_index * CHOI_TIMEOUT_RECORD_SIZE
            )
            if flags or reserved0 or reserved1:
                raise KtrfPhase5Error("CHOI timeout flags/reserved fields must be zero")
            if effect_start != expected_effect_start:
                raise KtrfPhase5Error("CHOI timeout effect slice is not canonical")
            effects = decode_effect_slice(effect_start, local_effect_count)
            expected_effect_start += local_effect_count
            row["timeout"] = {
                "value": _decode_scalar(value_kind, payload_a, payload_b, strings, "CHOI timeout value"),
                "effects": effects,
            }
            expected_timeout_index += 1

        result.append(row)

    if expected_option_start != option_count:
        raise KtrfPhase5Error("CHOI option records are not fully owned by choices")
    if expected_timeout_index != timeout_count:
        raise KtrfPhase5Error("CHOI timeout records are not fully owned by choices")
    if expected_effect_start != effect_ref_count:
        raise KtrfPhase5Error("CHOI effect refs are not fully owned by options/timeouts")
    return result


def build_phase5_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase5_strings(document)
    strs_payload, string_map = container.encode_strs(all_strings)
    try:
        meta_payload = phase1.encode_meta(document, string_map)
        nspc_payload, nspc_count = phase1.encode_namespaces(document, string_map)
        feat_payload, feat_count = phase1.encode_features(document, string_map)
        entr_payload, entr_count = phase1.encode_entry_points(document, string_map)
        vars_payload, vars_count = phase1.encode_variables(document, string_map)
        rsrc_payload, rsrc_count = phase2.encode_resources(document, string_map)
        hook_payload, hook_count = phase2.encode_hooks(document, string_map)
        endg_payload, endg_count = phase2.encode_endings(document, string_map)
        expr_payload, expr_count = phase3.encode_expressions(document, string_map)
        efft_payload, efft_count = phase4.encode_effects(document, string_map)
    except (
        phase1.KtrfPhase1Error,
        phase2.KtrfPhase2Error,
        phase3.KtrfPhase3Error,
        phase4.KtrfPhase4Error,
    ) as exc:
        raise _error_from(exc) from exc
    choi_payload, choi_count = encode_choices(document, string_map)
    return [
        container.Section(b"META", meta_payload, item_count=1),
        container.Section(b"STRS", strs_payload, item_count=len(container.decode_strs(strs_payload))),
        container.Section(b"NSPC", nspc_payload, item_count=nspc_count),
        container.Section(b"FEAT", feat_payload, item_count=feat_count),
        container.Section(b"ENTR", entr_payload, item_count=entr_count),
        container.Section(b"VARS", vars_payload, item_count=vars_count),
        container.Section(b"RSRC", rsrc_payload, item_count=rsrc_count),
        container.Section(b"HOOK", hook_payload, item_count=hook_count),
        container.Section(b"ENDG", endg_payload, item_count=endg_count),
        container.Section(b"EXPR", expr_payload, item_count=expr_count),
        container.Section(b"EFFT", efft_payload, item_count=efft_count),
        container.Section(b"CHOI", choi_payload, item_count=choi_count),
    ]


def build_phase5_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase5_sections(document))


def _entry_by_type(parsed: container.ParsedContainer) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase5_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE5_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE5_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase5Error(
            "missing required phase-5 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )
    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase5Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase5Error("META item_count must be 1")

    try:
        result = phase1.decode_meta(parsed.sections[b"META"], strings)
        result["features"] = phase1.decode_features(parsed.sections[b"FEAT"], strings, entries[b"FEAT"].item_count)
        result["namespaces"] = phase1.decode_namespaces(parsed.sections[b"NSPC"], strings, entries[b"NSPC"].item_count)
        result["entry_points"] = phase1.decode_entry_points(parsed.sections[b"ENTR"], strings, entries[b"ENTR"].item_count)
        result["variables"] = phase1.decode_variables(parsed.sections[b"VARS"], strings, entries[b"VARS"].item_count)
        result["resource_locators"] = phase2.decode_resources(parsed.sections[b"RSRC"], strings, entries[b"RSRC"].item_count)
        result["external_hooks"] = phase2.decode_hooks(parsed.sections[b"HOOK"], strings, entries[b"HOOK"].item_count)
        result["endings"] = phase2.decode_endings(parsed.sections[b"ENDG"], strings, entries[b"ENDG"].item_count)
    except (phase1.KtrfPhase1Error, phase2.KtrfPhase2Error) as exc:
        raise _error_from(exc) from exc

    entity_ids = {
        phase3.ENTITY_RESOURCE: [row["id"] for row in result["resource_locators"]],
        phase3.ENTITY_HOOK: [row["id"] for row in result["external_hooks"]],
        phase3.ENTITY_ENDING: [row["id"] for row in result["endings"]],
    }
    variable_ids = [row["id"] for row in result["variables"]]
    try:
        result["expressions"] = phase3.decode_expressions(
            parsed.sections[b"EXPR"], strings, entries[b"EXPR"].item_count, variable_ids, entity_ids
        )
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc
    expression_ids = [row["id"] for row in result["expressions"]]
    try:
        result["effects"] = phase4.decode_effects(
            parsed.sections[b"EFFT"], strings, entries[b"EFFT"].item_count,
            variable_ids, expression_ids, entity_ids,
        )
    except phase4.KtrfPhase4Error as exc:
        raise _error_from(exc) from exc
    effect_ids = [row["id"] for row in result["effects"]]
    result["choices"] = decode_choices(
        parsed.sections[b"CHOI"], strings, entries[b"CHOI"].item_count,
        variable_ids, effect_ids,
    )
    return result


def _clean_option(option: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": option["id"],
        "value": copy.deepcopy(option["value"]),
        "effects": list(option["effects"]),
    }
    if "label" in option:
        result["label"] = option["label"]
    return result


def _clean_choice(row: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": row["id"],
        "node": row["node"],
        "result_variable": row["result_variable"],
        "routing_policy": row["routing_policy"],
        "options": [_clean_option(_require_mapping(option, "Choice option")) for option in row["options"]],
    }
    if "timeout" in row:
        timeout = _require_mapping(row["timeout"], "Choice timeout")
        result["timeout"] = {
            "value": copy.deepcopy(timeout["value"]),
            "effects": list(timeout["effects"]),
        }
    return result


def canonical_phase5_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    try:
        projection = phase4.canonical_phase4_projection(document)
    except phase4.KtrfPhase4Error as exc:
        raise _error_from(exc) from exc
    rows = _sorted_choices(document)
    _validate_choices(document, rows)
    projection["choices"] = [_clean_choice(row) for row in rows]
    return projection
