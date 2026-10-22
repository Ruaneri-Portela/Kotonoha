#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 4 / EFFT.

Extends phase 3 with the canonical Effect catalog.

Effect records are canonically ordered by stable semantic ID. Core effect
arguments are lowered to previously-frozen table indices. Ordered value lists
reuse the same typed valueRef representation established by EXPR.
"""

from __future__ import annotations

import copy
import struct
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

import binary_semantic_phase3_v0_1 as phase3  # noqa: E402


phase2 = phase3.phase2
phase1 = phase3.phase1
container = phase3.container

EFFT_HEADER_STRUCT = struct.Struct("<6I")
EFFT_RECORD_STRUCT = struct.Struct("<IIBBH5I")

EFFT_HEADER_SIZE = EFFT_HEADER_STRUCT.size
EFFT_RECORD_SIZE = EFFT_RECORD_STRUCT.size
EFFT_VALUE_RECORD_SIZE = phase3.EXPR_ARG_RECORD_SIZE

EFFECT_SET = 1
EFFECT_ADD = 2
EFFECT_COPY = 3
EFFECT_REGISTER_ENDING = 4
EFFECT_CALL_HOOK = 5

OP_TO_SHAPE = {
    "ktrf:set": EFFECT_SET,
    "ktrf:add": EFFECT_ADD,
    "ktrf:copy": EFFECT_COPY,
    "ktrf:register-ending": EFFECT_REGISTER_ENDING,
    "ktrf:call-hook": EFFECT_CALL_HOOK,
}
SHAPE_TO_OP = {value: key for key, value in OP_TO_SHAPE.items()}

PHASE4_SECTION_TYPES = phase3.PHASE3_SECTION_TYPES + (b"EFFT",)


class KtrfPhase4Error(phase3.KtrfPhase3Error):
    pass


def _error_from(exc: Exception) -> KtrfPhase4Error:
    return KtrfPhase4Error(str(exc))


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


def _sorted_effects(document: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    rows = [
        _require_mapping(row, "Effect")
        for row in _require_list(document.get("effects"), "effects")
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc

    seen: set[str] = set()
    for row in rows:
        effect_id = _require_string(row.get("id"), "Effect.id")
        if effect_id in seen:
            raise KtrfPhase4Error(f"duplicate Effect id {effect_id!r}")
        seen.add(effect_id)
    return rows


def effect_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    return {row["id"]: index for index, row in enumerate(_sorted_effects(document))}


def _expect_exact_keys(args: Mapping[str, Any], expected: set[str], label: str) -> None:
    actual = set(args.keys())
    if actual != expected:
        raise KtrfPhase4Error(
            f"{label} args must contain exactly {sorted(expected)!r}; got {sorted(actual)!r}"
        )


def _validate_value_ref(
    value: Any,
    label: str,
    var_map: Mapping[str, int],
    expr_map: Mapping[str, int],
    catalogs: Mapping[str, Mapping[str, int]],
) -> Mapping[str, Any]:
    ref = _require_mapping(value, label)
    try:
        phase3._validate_arg_shape(ref, label)
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc

    kind = ref["kind"]
    if kind == "variable":
        if ref["ref"] not in var_map:
            raise KtrfPhase4Error(f"{label} references unknown variable {ref['ref']!r}")
    elif kind == "expression":
        if ref["ref"] not in expr_map:
            raise KtrfPhase4Error(f"{label} references unknown expression {ref['ref']!r}")
    elif kind == "entity":
        entity_kind = phase3.ENTITY_NAME_TO_KIND[ref["entity"]]
        section = phase3.ENTITY_KIND_TO_SECTION[entity_kind]
        if ref["ref"] not in catalogs[section]:
            raise KtrfPhase4Error(
                f"{label} references unknown {ref['entity']} entity {ref['ref']!r}"
            )
    return ref


def _validate_effects(
    document: Mapping[str, Any], rows: Sequence[Mapping[str, Any]]
) -> tuple[dict[str, int], dict[str, int], dict[str, dict[str, int]]]:
    try:
        var_map = phase3.variable_index_map(document)
        expr_map = phase3.expression_index_map(document)
        catalogs = phase2.catalog_index_maps(document)
    except (phase3.KtrfPhase3Error, phase2.KtrfPhase2Error) as exc:
        raise _error_from(exc) from exc

    for row in rows:
        effect_id = row["id"]
        op = _require_string(row.get("op"), f"Effect {effect_id}.op")
        if op not in OP_TO_SHAPE:
            raise KtrfPhase4Error(
                f"binary phase 4 does not define Effect operator {op!r}"
            )
        args = _require_mapping(row.get("args"), f"Effect {effect_id}.args")

        if op in ("ktrf:set", "ktrf:add"):
            _expect_exact_keys(args, {"target", "value"}, f"Effect {effect_id}")
            target = _require_string(args.get("target"), f"Effect {effect_id}.target")
            if target not in var_map:
                raise KtrfPhase4Error(
                    f"Effect {effect_id!r} references unknown target variable {target!r}"
                )
            _validate_value_ref(
                args.get("value"),
                f"Effect {effect_id}.value",
                var_map,
                expr_map,
                catalogs,
            )

        elif op == "ktrf:copy":
            _expect_exact_keys(args, {"target", "source"}, f"Effect {effect_id}")
            target = _require_string(args.get("target"), f"Effect {effect_id}.target")
            source = _require_string(args.get("source"), f"Effect {effect_id}.source")
            if target not in var_map:
                raise KtrfPhase4Error(
                    f"Effect {effect_id!r} references unknown target variable {target!r}"
                )
            if source not in var_map:
                raise KtrfPhase4Error(
                    f"Effect {effect_id!r} references unknown source variable {source!r}"
                )

        elif op == "ktrf:register-ending":
            _expect_exact_keys(args, {"ending"}, f"Effect {effect_id}")
            ending = _require_string(args.get("ending"), f"Effect {effect_id}.ending")
            if ending not in catalogs["ENDG"]:
                raise KtrfPhase4Error(
                    f"Effect {effect_id!r} references unknown ending {ending!r}"
                )

        elif op == "ktrf:call-hook":
            _expect_exact_keys(args, {"hook", "arguments"}, f"Effect {effect_id}")
            hook = _require_string(args.get("hook"), f"Effect {effect_id}.hook")
            if hook not in catalogs["HOOK"]:
                raise KtrfPhase4Error(
                    f"Effect {effect_id!r} references unknown hook {hook!r}"
                )
            arguments = _require_list(
                args.get("arguments"), f"Effect {effect_id}.arguments"
            )
            for arg_index, value in enumerate(arguments):
                _validate_value_ref(
                    value,
                    f"Effect {effect_id}.arguments[{arg_index}]",
                    var_map,
                    expr_map,
                    catalogs,
                )

    return var_map, expr_map, catalogs


def collect_phase4_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase3.collect_phase3_strings(document))
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_effects(document)
    var_map, expr_map, catalogs = _validate_effects(document, rows)
    del var_map, expr_map, catalogs

    for row in rows:
        result.add(row["id"])
        result.add(row["op"])
        args = row["args"]
        values: list[Mapping[str, Any]] = []
        if row["op"] in ("ktrf:set", "ktrf:add"):
            values.append(_require_mapping(args["value"], "Effect value"))
        elif row["op"] == "ktrf:call-hook":
            values.extend(
                _require_mapping(value, "Effect hook argument")
                for value in args["arguments"]
            )
        for value in values:
            if value["kind"] == "literal" and isinstance(value.get("value"), str):
                result.add(value["value"])

    return result


def _encode_value(
    value: Mapping[str, Any],
    strings: Mapping[str, int],
    var_map: Mapping[str, int],
    expr_map: Mapping[str, int],
    catalogs: Mapping[str, Mapping[str, int]],
) -> bytes:
    try:
        return phase3._encode_arg(value, strings, var_map, expr_map, catalogs)
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc


def encode_effects(
    document: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[bytes, int]:
    rows = _sorted_effects(document)
    var_map, expr_map, catalogs = _validate_effects(document, rows)

    records = bytearray()
    values = bytearray()
    value_count = 0

    for row in rows:
        effect_id = row["id"]
        op = row["op"]
        shape = OP_TO_SHAPE[op]
        args = row["args"]
        payload_a = 0
        payload_b = 0
        value_start = value_count
        local_value_count = 0

        if shape in (EFFECT_SET, EFFECT_ADD):
            payload_a = var_map[args["target"]]
            value = _require_mapping(args["value"], f"Effect {effect_id}.value")
            values.extend(_encode_value(value, strings, var_map, expr_map, catalogs))
            value_count += 1
            local_value_count = 1

        elif shape == EFFECT_COPY:
            payload_a = var_map[args["target"]]
            payload_b = var_map[args["source"]]

        elif shape == EFFECT_REGISTER_ENDING:
            payload_a = catalogs["ENDG"][args["ending"]]

        elif shape == EFFECT_CALL_HOOK:
            payload_a = catalogs["HOOK"][args["hook"]]
            for raw_value in args["arguments"]:
                value = _require_mapping(raw_value, f"Effect {effect_id}.hook argument")
                values.extend(_encode_value(value, strings, var_map, expr_map, catalogs))
                value_count += 1
                local_value_count += 1

        else:
            raise AssertionError("unreachable Effect shape")

        records.extend(
            EFFT_RECORD_STRUCT.pack(
                _index(strings, effect_id, "Effect.id"),
                _index(strings, op, "Effect.op"),
                shape,
                0,
                0,
                payload_a,
                payload_b,
                value_start,
                local_value_count,
                0,
            )
        )

    effects_offset = EFFT_HEADER_SIZE
    values_offset = effects_offset + len(rows) * EFFT_RECORD_SIZE
    header = EFFT_HEADER_STRUCT.pack(
        len(rows),
        EFFT_RECORD_SIZE,
        value_count,
        EFFT_VALUE_RECORD_SIZE,
        effects_offset,
        values_offset,
    )
    return header + bytes(records) + bytes(values), len(rows)


def _decode_value(
    payload: bytes,
    offset: int,
    strings: Sequence[str],
    variable_ids: Sequence[str],
    expression_ids: Sequence[str],
    entity_ids: Mapping[int, Sequence[str]],
) -> dict[str, Any]:
    try:
        return phase3._decode_arg(
            payload,
            offset,
            strings,
            variable_ids,
            expression_ids,
            entity_ids,
        )
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc


def decode_effects(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    variable_ids: Sequence[str],
    expression_ids: Sequence[str],
    entity_ids: Mapping[int, Sequence[str]],
) -> list[dict[str, Any]]:
    if len(payload) < EFFT_HEADER_SIZE:
        raise KtrfPhase4Error("EFFT payload is too small")

    (
        effect_count,
        effect_record_size,
        value_count,
        value_record_size,
        effects_offset,
        values_offset,
    ) = EFFT_HEADER_STRUCT.unpack_from(payload, 0)

    if effect_count != section_item_count:
        raise KtrfPhase4Error("EFFT header count does not match section item_count")
    if effect_record_size != EFFT_RECORD_SIZE:
        raise KtrfPhase4Error(f"unsupported EFFT record size {effect_record_size}")
    if value_record_size != EFFT_VALUE_RECORD_SIZE:
        raise KtrfPhase4Error(f"unsupported EFFT value record size {value_record_size}")
    if effects_offset != EFFT_HEADER_SIZE:
        raise KtrfPhase4Error("EFFT effects_offset is not canonical")

    expected_values_offset = EFFT_HEADER_SIZE + effect_count * EFFT_RECORD_SIZE
    if values_offset != expected_values_offset:
        raise KtrfPhase4Error("EFFT values_offset is not canonical")
    expected_size = values_offset + value_count * EFFT_VALUE_RECORD_SIZE
    if len(payload) != expected_size:
        raise KtrfPhase4Error("EFFT payload size/header counts mismatch")

    ending_ids = entity_ids.get(phase3.ENTITY_ENDING, ())
    hook_ids = entity_ids.get(phase3.ENTITY_HOOK, ())

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    expected_value_start = 0

    for index in range(effect_count):
        offset = effects_offset + index * EFFT_RECORD_SIZE
        (
            id_i,
            op_i,
            shape,
            flags,
            reserved,
            payload_a,
            payload_b,
            value_start,
            local_value_count,
            reserved2,
        ) = EFFT_RECORD_STRUCT.unpack_from(payload, offset)

        if flags or reserved or reserved2:
            raise KtrfPhase4Error("EFFT flags/reserved fields must be zero in v0.1")
        if shape not in SHAPE_TO_OP:
            raise KtrfPhase4Error(f"unknown EFFT shape {shape}")
        if value_start != expected_value_start:
            raise KtrfPhase4Error("EFFT value slices are not canonical/contiguous")
        if value_start + local_value_count > value_count:
            raise KtrfPhase4Error("EFFT value slice is out of range")
        expected_value_start += local_value_count

        effect_id = _string(strings, id_i, "EFFT.id")
        key = effect_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase4Error("EFFT records are not in canonical id order")
        previous = key

        op = _string(strings, op_i, "EFFT.op")
        expected_op = SHAPE_TO_OP[shape]
        if op != expected_op:
            raise KtrfPhase4Error(
                f"EFFT shape/op mismatch for {effect_id!r}: {shape} vs {op!r}"
            )

        decoded_values: list[dict[str, Any]] = []
        for local_index in range(local_value_count):
            value_index = value_start + local_index
            value_offset = values_offset + value_index * EFFT_VALUE_RECORD_SIZE
            decoded_values.append(
                _decode_value(
                    payload,
                    value_offset,
                    strings,
                    variable_ids,
                    expression_ids,
                    entity_ids,
                )
            )

        if shape in (EFFECT_SET, EFFECT_ADD):
            if payload_a >= len(variable_ids) or payload_b != 0 or local_value_count != 1:
                raise KtrfPhase4Error("invalid set/add EFFT payload")
            args: dict[str, Any] = {
                "target": variable_ids[payload_a],
                "value": decoded_values[0],
            }

        elif shape == EFFECT_COPY:
            if (
                payload_a >= len(variable_ids)
                or payload_b >= len(variable_ids)
                or local_value_count != 0
            ):
                raise KtrfPhase4Error("invalid copy EFFT payload")
            args = {
                "target": variable_ids[payload_a],
                "source": variable_ids[payload_b],
            }

        elif shape == EFFECT_REGISTER_ENDING:
            if payload_a >= len(ending_ids) or payload_b != 0 or local_value_count != 0:
                raise KtrfPhase4Error("invalid register-ending EFFT payload")
            args = {"ending": ending_ids[payload_a]}

        elif shape == EFFECT_CALL_HOOK:
            if payload_a >= len(hook_ids) or payload_b != 0:
                raise KtrfPhase4Error("invalid call-hook EFFT payload")
            args = {"hook": hook_ids[payload_a], "arguments": decoded_values}

        else:
            raise AssertionError("unreachable Effect shape")

        result.append({"id": effect_id, "op": op, "args": args})

    if expected_value_start != value_count:
        raise KtrfPhase4Error("EFFT value records are not fully owned by effects")
    return result


def build_phase4_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase4_strings(document)
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
    except (
        phase1.KtrfPhase1Error,
        phase2.KtrfPhase2Error,
        phase3.KtrfPhase3Error,
    ) as exc:
        raise _error_from(exc) from exc

    efft_payload, efft_count = encode_effects(document, string_map)

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
        container.Section(b"EXPR", expr_payload, item_count=expr_count),
        container.Section(b"EFFT", efft_payload, item_count=efft_count),
    ]


def build_phase4_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase4_sections(document))


def _entry_by_type(
    parsed: container.ParsedContainer,
) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase4_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE4_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE4_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase4Error(
            "missing required phase-4 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase4Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase4Error("META item_count must be 1")

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
        result["resource_locators"] = phase2.decode_resources(
            parsed.sections[b"RSRC"], strings, entries[b"RSRC"].item_count
        )
        result["external_hooks"] = phase2.decode_hooks(
            parsed.sections[b"HOOK"], strings, entries[b"HOOK"].item_count
        )
        result["endings"] = phase2.decode_endings(
            parsed.sections[b"ENDG"], strings, entries[b"ENDG"].item_count
        )
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
            parsed.sections[b"EXPR"],
            strings,
            entries[b"EXPR"].item_count,
            variable_ids,
            entity_ids,
        )
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc

    expression_ids = [row["id"] for row in result["expressions"]]
    result["effects"] = decode_effects(
        parsed.sections[b"EFFT"],
        strings,
        entries[b"EFFT"].item_count,
        variable_ids,
        expression_ids,
        entity_ids,
    )
    return result


def _clean_value_ref(value: Mapping[str, Any]) -> dict[str, Any]:
    try:
        return phase3._clean_arg_projection(value)
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc


def _clean_effect_projection(row: Mapping[str, Any]) -> dict[str, Any]:
    effect_id = row["id"]
    op = row["op"]
    args = row["args"]

    if op in ("ktrf:set", "ktrf:add"):
        clean_args: dict[str, Any] = {
            "target": args["target"],
            "value": _clean_value_ref(
                _require_mapping(args["value"], f"Effect {effect_id}.value")
            ),
        }
    elif op == "ktrf:copy":
        clean_args = {"target": args["target"], "source": args["source"]}
    elif op == "ktrf:register-ending":
        clean_args = {"ending": args["ending"]}
    elif op == "ktrf:call-hook":
        clean_args = {
            "hook": args["hook"],
            "arguments": [
                _clean_value_ref(_require_mapping(value, "Effect hook argument"))
                for value in args["arguments"]
            ],
        }
    else:
        raise AssertionError("unreachable Effect operator")

    return {"id": effect_id, "op": op, "args": clean_args}


def canonical_phase4_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    """Return the semantic subset represented through EFFT in canonical order."""
    try:
        projection = phase3.canonical_phase3_projection(document)
    except phase3.KtrfPhase3Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_effects(document)
    _validate_effects(document, rows)
    projection["effects"] = [_clean_effect_projection(row) for row in rows]
    return projection
