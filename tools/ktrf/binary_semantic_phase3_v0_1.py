#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 3.

This layer extends phase 2 with EXPR, the first graph-adjacent table.

Expression records are canonically ordered by stable semantic ID. Argument order
is semantic and is therefore preserved exactly. Variable, expression and
supported entity references are lowered to canonical u32 table indices while
the decoder reconstructs the original stable IDs.
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

import binary_semantic_phase2_v0_1 as phase2  # noqa: E402


phase1 = phase2.phase1
container = phase2.container

EXPR_HEADER_STRUCT = struct.Struct("<6I")
EXPR_RECORD_STRUCT = struct.Struct("<8I")
EXPR_ARG_STRUCT = struct.Struct("<BBBBIIQI")

EXPR_HEADER_SIZE = EXPR_HEADER_STRUCT.size
EXPR_RECORD_SIZE = EXPR_RECORD_STRUCT.size
EXPR_ARG_RECORD_SIZE = EXPR_ARG_STRUCT.size

ARG_KIND_LITERAL = 1
ARG_KIND_VARIABLE = 2
ARG_KIND_EXPRESSION = 3
ARG_KIND_ENTITY = 4

LITERAL_NONE = 0
LITERAL_NULL = 1
LITERAL_BOOL = 2
LITERAL_INT64 = 3
LITERAL_FLOAT64 = 4
LITERAL_STRING = 5

ENTITY_NONE = 0
ENTITY_RESOURCE = 1
ENTITY_HOOK = 2
ENTITY_ENDING = 3

ENTITY_NAME_TO_KIND = {
    "resource": ENTITY_RESOURCE,
    "hook": ENTITY_HOOK,
    "ending": ENTITY_ENDING,
}
ENTITY_KIND_TO_NAME = {value: key for key, value in ENTITY_NAME_TO_KIND.items()}
ENTITY_KIND_TO_SECTION = {
    ENTITY_RESOURCE: "RSRC",
    ENTITY_HOOK: "HOOK",
    ENTITY_ENDING: "ENDG",
}
ENTITY_KIND_TO_FIELD = {
    ENTITY_RESOURCE: "resource_locators",
    ENTITY_HOOK: "external_hooks",
    ENTITY_ENDING: "endings",
}

PHASE3_SECTION_TYPES = phase2.PHASE2_SECTION_TYPES + (b"EXPR",)


class KtrfPhase3Error(phase2.KtrfPhase2Error):
    pass


def _error_from(exc: Exception) -> KtrfPhase3Error:
    return KtrfPhase3Error(str(exc))


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


def _sorted_rows_by_id(
    document: Mapping[str, Any], key: str, label: str
) -> list[Mapping[str, Any]]:
    rows = [_require_mapping(row, label) for row in _require_list(document.get(key), key)]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc

    seen: set[str] = set()
    for row in rows:
        record_id = _require_string(row.get("id"), f"{label}.id")
        if record_id in seen:
            raise KtrfPhase3Error(f"duplicate {label} id {record_id!r}")
        seen.add(record_id)
    return rows


def variable_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    rows = _sorted_rows_by_id(document, "variables", "Variable")
    return {row["id"]: index for index, row in enumerate(rows)}


def expression_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    rows = _sorted_rows_by_id(document, "expressions", "Expression")
    return {row["id"]: index for index, row in enumerate(rows)}


def _validate_literal(value: Any) -> None:
    if value is None or type(value) is bool or isinstance(value, str):
        if isinstance(value, str):
            _require_string(value, "Expression literal string")
        return
    if type(value) is int:
        if not (-0x8000000000000000 <= value <= 0x7FFFFFFFFFFFFFFF):
            raise KtrfPhase3Error("Expression literal integer is out of signed int64 range")
        return
    if type(value) is float:
        if not math.isfinite(value):
            raise KtrfPhase3Error("Expression literal float must be finite")
        return
    raise KtrfPhase3Error(
        "binary phase 3 only defines null/bool/int64/float64/string Expression literals"
    )


def _validate_arg_shape(arg: Mapping[str, Any], label: str) -> None:
    kind = _require_string(arg.get("kind"), f"{label}.kind")

    if kind == "literal":
        if "value" not in arg:
            raise KtrfPhase3Error(f"{label} literal is missing value")
        _validate_literal(arg.get("value"))
        return

    if kind in ("variable", "expression"):
        _require_string(arg.get("ref"), f"{label}.ref")
        return

    if kind == "entity":
        entity = _require_string(arg.get("entity"), f"{label}.entity")
        _require_string(arg.get("ref"), f"{label}.ref")
        if entity not in ENTITY_NAME_TO_KIND:
            raise KtrfPhase3Error(
                f"binary phase 3 does not define entity reference kind {entity!r}"
            )
        return

    raise KtrfPhase3Error(f"unsupported Expression argument kind {kind!r}")


def _assert_expression_acyclic(rows: Sequence[Mapping[str, Any]]) -> None:
    ids = {row["id"] for row in rows}
    deps: dict[str, list[str]] = {}
    for row in rows:
        refs: list[str] = []
        for arg_index, raw_arg in enumerate(row["args"]):
            arg = _require_mapping(raw_arg, f"Expression {row['id']} arg {arg_index}")
            if arg.get("kind") == "expression":
                ref = _require_string(arg.get("ref"), "Expression argument ref")
                if ref not in ids:
                    raise KtrfPhase3Error(
                        f"Expression {row['id']!r} references unknown expression {ref!r}"
                    )
                refs.append(ref)
        deps[row["id"]] = refs

    state: dict[str, int] = {}
    stack: list[str] = []

    def visit(node: str) -> None:
        mark = state.get(node, 0)
        if mark == 2:
            return
        if mark == 1:
            try:
                start = stack.index(node)
            except ValueError:
                start = 0
            cycle = stack[start:] + [node]
            raise KtrfPhase3Error(
                "Expression reference cycle is not allowed: " + " -> ".join(cycle)
            )
        state[node] = 1
        stack.append(node)
        for dep in deps[node]:
            visit(dep)
        stack.pop()
        state[node] = 2

    for row in rows:
        visit(row["id"])


def _validate_expression_semantics(
    document: Mapping[str, Any], rows: Sequence[Mapping[str, Any]]
) -> tuple[dict[str, int], dict[str, int], dict[str, dict[str, int]]]:
    var_map = variable_index_map(document)
    expr_map = {row["id"]: index for index, row in enumerate(rows)}
    try:
        catalogs = phase2.catalog_index_maps(document)
    except phase2.KtrfPhase2Error as exc:
        raise _error_from(exc) from exc

    for row in rows:
        _require_string(row.get("op"), f"Expression {row['id']}.op")
        if "result_type" in row:
            _require_string(row.get("result_type"), f"Expression {row['id']}.result_type")
        args = _require_list(row.get("args"), f"Expression {row['id']}.args")

        for arg_index, raw_arg in enumerate(args):
            arg = _require_mapping(raw_arg, f"Expression {row['id']} arg {arg_index}")
            _validate_arg_shape(arg, f"Expression {row['id']} arg {arg_index}")
            kind = arg["kind"]
            if kind == "variable":
                ref = arg["ref"]
                if ref not in var_map:
                    raise KtrfPhase3Error(
                        f"Expression {row['id']!r} references unknown variable {ref!r}"
                    )
            elif kind == "expression":
                ref = arg["ref"]
                if ref not in expr_map:
                    raise KtrfPhase3Error(
                        f"Expression {row['id']!r} references unknown expression {ref!r}"
                    )
            elif kind == "entity":
                entity_kind = ENTITY_NAME_TO_KIND[arg["entity"]]
                section = ENTITY_KIND_TO_SECTION[entity_kind]
                ref = arg["ref"]
                if ref not in catalogs[section]:
                    raise KtrfPhase3Error(
                        f"Expression {row['id']!r} references unknown "
                        f"{arg['entity']} entity {ref!r}"
                    )

    _assert_expression_acyclic(rows)
    return var_map, expr_map, catalogs


def collect_phase3_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase2.collect_phase2_strings(document))
    except phase2.KtrfPhase2Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_rows_by_id(document, "expressions", "Expression")
    _validate_expression_semantics(document, rows)

    for row in rows:
        result.add(row["id"])
        result.add(row["op"])
        if "result_type" in row:
            result.add(row["result_type"])
        for raw_arg in row["args"]:
            arg = _require_mapping(raw_arg, "Expression argument")
            if arg["kind"] == "literal" and isinstance(arg.get("value"), str):
                result.add(arg["value"])

    return result


def _encode_literal(
    value: Any, strings: Mapping[str, int]
) -> tuple[int, int, int]:
    _validate_literal(value)
    if value is None:
        return LITERAL_NULL, 0, 0
    if type(value) is bool:
        return LITERAL_BOOL, 0, 1 if value else 0
    if type(value) is int:
        return LITERAL_INT64, 0, value & 0xFFFFFFFFFFFFFFFF
    if type(value) is float:
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
        return LITERAL_FLOAT64, 0, bits
    if isinstance(value, str):
        return LITERAL_STRING, _index(strings, value, "Expression literal string"), 0
    raise AssertionError("unreachable Expression literal kind")


def _decode_literal(
    literal_kind: int, payload_a: int, payload_b: int, strings: Sequence[str]
) -> Any:
    if literal_kind == LITERAL_NULL:
        if payload_a or payload_b:
            raise KtrfPhase3Error("invalid null EXPR literal payload")
        return None
    if literal_kind == LITERAL_BOOL:
        if payload_a or payload_b not in (0, 1):
            raise KtrfPhase3Error("invalid bool EXPR literal payload")
        return bool(payload_b)
    if literal_kind == LITERAL_INT64:
        if payload_a:
            raise KtrfPhase3Error("invalid int64 EXPR literal payload")
        value = payload_b
        if value & (1 << 63):
            value -= 1 << 64
        return value
    if literal_kind == LITERAL_FLOAT64:
        if payload_a:
            raise KtrfPhase3Error("invalid float64 EXPR literal payload")
        value = struct.unpack("<d", struct.pack("<Q", payload_b))[0]
        if not math.isfinite(value):
            raise KtrfPhase3Error("decoded EXPR literal float is not finite")
        return value
    if literal_kind == LITERAL_STRING:
        if payload_b:
            raise KtrfPhase3Error("invalid string EXPR literal payload")
        return _string(strings, payload_a, "EXPR literal string")
    raise KtrfPhase3Error(f"unknown EXPR literal kind {literal_kind}")


def _encode_arg(
    arg: Mapping[str, Any],
    strings: Mapping[str, int],
    var_map: Mapping[str, int],
    expr_map: Mapping[str, int],
    catalogs: Mapping[str, Mapping[str, int]],
) -> bytes:
    kind = arg["kind"]

    if kind == "literal":
        literal_kind, payload_a, payload_b = _encode_literal(arg.get("value"), strings)
        return EXPR_ARG_STRUCT.pack(
            ARG_KIND_LITERAL,
            literal_kind,
            ENTITY_NONE,
            0,
            payload_a,
            0,
            payload_b,
            0,
        )

    if kind == "variable":
        return EXPR_ARG_STRUCT.pack(
            ARG_KIND_VARIABLE,
            LITERAL_NONE,
            ENTITY_NONE,
            0,
            var_map[arg["ref"]],
            0,
            0,
            0,
        )

    if kind == "expression":
        return EXPR_ARG_STRUCT.pack(
            ARG_KIND_EXPRESSION,
            LITERAL_NONE,
            ENTITY_NONE,
            0,
            expr_map[arg["ref"]],
            0,
            0,
            0,
        )

    if kind == "entity":
        entity_kind = ENTITY_NAME_TO_KIND[arg["entity"]]
        section = ENTITY_KIND_TO_SECTION[entity_kind]
        return EXPR_ARG_STRUCT.pack(
            ARG_KIND_ENTITY,
            LITERAL_NONE,
            entity_kind,
            0,
            catalogs[section][arg["ref"]],
            0,
            0,
            0,
        )

    raise AssertionError("unreachable Expression argument kind")


def encode_expressions(
    document: Mapping[str, Any], strings: Mapping[str, int]
) -> tuple[bytes, int]:
    rows = _sorted_rows_by_id(document, "expressions", "Expression")
    var_map, expr_map, catalogs = _validate_expression_semantics(document, rows)

    records = bytearray()
    arguments = bytearray()
    argument_count = 0

    for row in rows:
        args = row["args"]
        arg_start = argument_count
        for raw_arg in args:
            arg = _require_mapping(raw_arg, "Expression argument")
            arguments.extend(_encode_arg(arg, strings, var_map, expr_map, catalogs))
            argument_count += 1

        result_type_i = container.NULL_INDEX
        if "result_type" in row:
            result_type_i = _index(
                strings, row["result_type"], f"Expression {row['id']}.result_type"
            )

        records.extend(
            EXPR_RECORD_STRUCT.pack(
                _index(strings, row["id"], "Expression.id"),
                _index(strings, row["op"], "Expression.op"),
                result_type_i,
                arg_start,
                len(args),
                0,
                0,
                0,
            )
        )

    expressions_offset = EXPR_HEADER_SIZE
    arguments_offset = expressions_offset + len(rows) * EXPR_RECORD_SIZE
    header = EXPR_HEADER_STRUCT.pack(
        len(rows),
        EXPR_RECORD_SIZE,
        argument_count,
        EXPR_ARG_RECORD_SIZE,
        expressions_offset,
        arguments_offset,
    )
    return header + bytes(records) + bytes(arguments), len(rows)


def _decode_arg(
    raw: bytes,
    offset: int,
    strings: Sequence[str],
    variable_ids: Sequence[str],
    expression_ids: Sequence[str],
    entity_ids: Mapping[int, Sequence[str]],
) -> dict[str, Any]:
    (
        kind,
        literal_kind,
        entity_kind,
        flags,
        payload_a,
        payload_aux,
        payload_b,
        reserved,
    ) = EXPR_ARG_STRUCT.unpack_from(raw, offset)

    if flags or payload_aux or reserved:
        raise KtrfPhase3Error("EXPR argument flags/reserved fields must be zero")

    if kind == ARG_KIND_LITERAL:
        if entity_kind != ENTITY_NONE:
            raise KtrfPhase3Error("literal EXPR argument has nonzero entity kind")
        if literal_kind == LITERAL_NONE:
            raise KtrfPhase3Error("literal EXPR argument has no literal kind")
        return {
            "kind": "literal",
            "value": _decode_literal(literal_kind, payload_a, payload_b, strings),
        }

    if literal_kind != LITERAL_NONE:
        raise KtrfPhase3Error("non-literal EXPR argument has literal kind")
    if payload_b:
        raise KtrfPhase3Error("non-literal EXPR argument has nonzero payload_b")

    if kind == ARG_KIND_VARIABLE:
        if entity_kind != ENTITY_NONE:
            raise KtrfPhase3Error("variable EXPR argument has entity kind")
        if payload_a >= len(variable_ids):
            raise KtrfPhase3Error("EXPR variable index out of range")
        return {"kind": "variable", "ref": variable_ids[payload_a]}

    if kind == ARG_KIND_EXPRESSION:
        if entity_kind != ENTITY_NONE:
            raise KtrfPhase3Error("expression EXPR argument has entity kind")
        if payload_a >= len(expression_ids):
            raise KtrfPhase3Error("EXPR expression index out of range")
        return {"kind": "expression", "ref": expression_ids[payload_a]}

    if kind == ARG_KIND_ENTITY:
        if entity_kind not in ENTITY_KIND_TO_NAME:
            raise KtrfPhase3Error(f"unknown EXPR entity kind {entity_kind}")
        ids = entity_ids[entity_kind]
        if payload_a >= len(ids):
            raise KtrfPhase3Error("EXPR entity index out of range")
        return {
            "kind": "entity",
            "entity": ENTITY_KIND_TO_NAME[entity_kind],
            "ref": ids[payload_a],
        }

    raise KtrfPhase3Error(f"unknown EXPR argument kind {kind}")


def decode_expressions(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    variable_ids: Sequence[str],
    entity_ids: Mapping[int, Sequence[str]],
) -> list[dict[str, Any]]:
    if len(payload) < EXPR_HEADER_SIZE:
        raise KtrfPhase3Error("EXPR payload is too small")

    (
        expression_count,
        expression_record_size,
        argument_count,
        argument_record_size,
        expressions_offset,
        arguments_offset,
    ) = EXPR_HEADER_STRUCT.unpack_from(payload, 0)

    if expression_count != section_item_count:
        raise KtrfPhase3Error("EXPR header count does not match section item_count")
    if expression_record_size != EXPR_RECORD_SIZE:
        raise KtrfPhase3Error(
            f"unsupported EXPR record size {expression_record_size}"
        )
    if argument_record_size != EXPR_ARG_RECORD_SIZE:
        raise KtrfPhase3Error(
            f"unsupported EXPR argument record size {argument_record_size}"
        )
    if expressions_offset != EXPR_HEADER_SIZE:
        raise KtrfPhase3Error("EXPR expressions_offset is not canonical")

    expected_arguments_offset = EXPR_HEADER_SIZE + expression_count * EXPR_RECORD_SIZE
    if arguments_offset != expected_arguments_offset:
        raise KtrfPhase3Error("EXPR arguments_offset is not canonical")

    expected_size = arguments_offset + argument_count * EXPR_ARG_RECORD_SIZE
    if len(payload) != expected_size:
        raise KtrfPhase3Error("EXPR payload size/header counts mismatch")

    record_meta: list[tuple[str, str, int, int, int]] = []
    expression_ids: list[str] = []
    previous: bytes | None = None
    expected_arg_start = 0

    for index in range(expression_count):
        offset = expressions_offset + index * EXPR_RECORD_SIZE
        (
            id_i,
            op_i,
            result_type_i,
            arg_start,
            arg_count,
            flags,
            reserved0,
            reserved1,
        ) = EXPR_RECORD_STRUCT.unpack_from(payload, offset)

        if flags or reserved0 or reserved1:
            raise KtrfPhase3Error("EXPR flags/reserved fields must be zero in v0.1")
        if arg_start != expected_arg_start:
            raise KtrfPhase3Error("EXPR argument slices are not canonical/contiguous")
        if arg_start + arg_count > argument_count:
            raise KtrfPhase3Error("EXPR argument slice is out of range")
        expected_arg_start += arg_count

        record_id = _string(strings, id_i, "EXPR.id")
        key = record_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase3Error("EXPR records are not in canonical id order")
        previous = key

        op = _string(strings, op_i, "EXPR.op")
        expression_ids.append(record_id)
        record_meta.append((record_id, op, result_type_i, arg_start, arg_count))

    if expected_arg_start != argument_count:
        raise KtrfPhase3Error("EXPR argument records are not fully owned by expressions")

    result: list[dict[str, Any]] = []
    for record_id, op, result_type_i, arg_start, arg_count in record_meta:
        args: list[dict[str, Any]] = []
        for local_index in range(arg_count):
            arg_index = arg_start + local_index
            arg_offset = arguments_offset + arg_index * EXPR_ARG_RECORD_SIZE
            args.append(
                _decode_arg(
                    payload,
                    arg_offset,
                    strings,
                    variable_ids,
                    expression_ids,
                    entity_ids,
                )
            )

        row: dict[str, Any] = {"id": record_id, "op": op, "args": args}
        if result_type_i != container.NULL_INDEX:
            row["result_type"] = _string(strings, result_type_i, "EXPR.result_type")
        result.append(row)

    _assert_expression_acyclic(result)
    return result


def build_phase3_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase3_strings(document)
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
    except (phase1.KtrfPhase1Error, phase2.KtrfPhase2Error) as exc:
        raise _error_from(exc) from exc

    expr_payload, expr_count = encode_expressions(document, string_map)

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
    ]


def build_phase3_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase3_sections(document))


def _entry_by_type(
    parsed: container.ParsedContainer,
) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase3_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE3_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE3_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase3Error(
            "missing required phase-3 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase3Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase3Error("META item_count must be 1")

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
        ENTITY_RESOURCE: [row["id"] for row in result["resource_locators"]],
        ENTITY_HOOK: [row["id"] for row in result["external_hooks"]],
        ENTITY_ENDING: [row["id"] for row in result["endings"]],
    }
    variable_ids = [row["id"] for row in result["variables"]]

    result["expressions"] = decode_expressions(
        parsed.sections[b"EXPR"],
        strings,
        entries[b"EXPR"].item_count,
        variable_ids,
        entity_ids,
    )
    return result


def _clean_arg_projection(arg: Mapping[str, Any]) -> dict[str, Any]:
    _validate_arg_shape(arg, "Expression argument")
    kind = arg["kind"]
    if kind == "literal":
        return {"kind": "literal", "value": copy.deepcopy(arg.get("value"))}
    if kind in ("variable", "expression"):
        return {"kind": kind, "ref": arg["ref"]}
    if kind == "entity":
        return {"kind": "entity", "entity": arg["entity"], "ref": arg["ref"]}
    raise AssertionError("unreachable Expression argument kind")


def _clean_expression_projection(row: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": row["id"],
        "op": row["op"],
        "args": [
            _clean_arg_projection(_require_mapping(arg, "Expression argument"))
            for arg in row["args"]
        ],
    }
    if "result_type" in row:
        result["result_type"] = row["result_type"]
    return result


def canonical_phase3_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    """Return the semantic subset represented by phase-3 tables in canonical order."""
    try:
        projection = phase2.canonical_phase2_projection(document)
    except phase2.KtrfPhase2Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_rows_by_id(document, "expressions", "Expression")
    _validate_expression_semantics(document, rows)
    projection["expressions"] = [_clean_expression_projection(row) for row in rows]
    return projection
