#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 7 / TRAN.

Extends phase 6 with the canonical Transition catalog. Transition records are
canonically ordered by stable semantic ID. Ordered Effect references and trigger
lists remain semantic and are preserved exactly.

All graph references are now physically lowered:
- source/destination -> NODE indices
- predicate -> EXPR index
- effects -> EFFT indices
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

import binary_semantic_phase6_v0_1 as phase6  # noqa: E402

phase5 = phase6.phase5
phase4 = phase6.phase4
phase3 = phase6.phase3
phase2 = phase6.phase2
phase1 = phase6.phase1
container = phase6.container

TRAN_HEADER_STRUCT = struct.Struct("<8I")
TRAN_RECORD_STRUCT = struct.Struct("<12I")
TRAN_EFFECT_REF_STRUCT = struct.Struct("<I")
TRAN_TRIGGER_REF_STRUCT = struct.Struct("<I")

TRAN_HEADER_SIZE = TRAN_HEADER_STRUCT.size
TRAN_RECORD_SIZE = TRAN_RECORD_STRUCT.size
TRAN_EFFECT_REF_RECORD_SIZE = TRAN_EFFECT_REF_STRUCT.size
TRAN_TRIGGER_REF_RECORD_SIZE = TRAN_TRIGGER_REF_STRUCT.size

TRAN_FLAG_TERMINAL = 0x00000001
TRAN_FLAG_HAS_TRIGGERS = 0x00000002
TRAN_FLAGS_KNOWN = TRAN_FLAG_TERMINAL | TRAN_FLAG_HAS_TRIGGERS

PHASE7_SECTION_TYPES = phase6.PHASE6_SECTION_TYPES + (b"TRAN",)


class KtrfPhase7Error(phase6.KtrfPhase6Error):
    pass


def _error_from(exc: Exception) -> KtrfPhase7Error:
    return KtrfPhase7Error(str(exc))


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


def _sorted_transitions(document: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    rows = [
        _require_mapping(row, "Transition")
        for row in _require_list(document.get("transitions"), "transitions")
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc

    seen: set[str] = set()
    for row in rows:
        transition_id = _require_string(row.get("id"), "Transition.id")
        if transition_id in seen:
            raise KtrfPhase7Error(f"duplicate Transition id {transition_id!r}")
        seen.add(transition_id)
    return rows


def transition_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    return {
        row["id"]: index
        for index, row in enumerate(_sorted_transitions(document))
    }


def _validate_priority(value: Any, transition_id: str) -> int:
    if type(value) is not int:
        raise KtrfPhase7Error(
            f"Transition {transition_id!r} priority must be an integer"
        )
    if not (0 <= value <= 0xFFFFFFFF):
        raise KtrfPhase7Error(
            f"Transition {transition_id!r} priority is out of u32 range"
        )
    return value


def _validate_transitions(
    document: Mapping[str, Any],
    rows: Sequence[Mapping[str, Any]],
) -> tuple[dict[str, int], dict[str, int], dict[str, int]]:
    try:
        node_map = phase6.node_index_map(document)
        expr_map = phase3.expression_index_map(document)
        effect_map = phase4.effect_index_map(document)
    except (
        phase6.KtrfPhase6Error,
        phase3.KtrfPhase3Error,
        phase4.KtrfPhase4Error,
    ) as exc:
        raise _error_from(exc) from exc

    for row in rows:
        transition_id = row["id"]
        source = _require_string(
            row.get("source"), f"Transition {transition_id}.source"
        )
        if source not in node_map:
            raise KtrfPhase7Error(
                f"Transition {transition_id!r} references unknown source node {source!r}"
            )

        _validate_priority(row.get("priority"), transition_id)

        terminal = row.get("terminal")
        if type(terminal) is not bool:
            raise KtrfPhase7Error(
                f"Transition {transition_id!r} terminal must be boolean"
            )

        has_destination = "destination" in row
        if terminal and has_destination:
            raise KtrfPhase7Error(
                f"terminal Transition {transition_id!r} must not have destination"
            )
        if not terminal and not has_destination:
            raise KtrfPhase7Error(
                f"nonterminal Transition {transition_id!r} requires destination"
            )
        if has_destination:
            destination = _require_string(
                row.get("destination"), f"Transition {transition_id}.destination"
            )
            if destination not in node_map:
                raise KtrfPhase7Error(
                    f"Transition {transition_id!r} references unknown destination node {destination!r}"
                )

        if "predicate" in row:
            predicate = _require_string(
                row.get("predicate"), f"Transition {transition_id}.predicate"
            )
            if predicate not in expr_map:
                raise KtrfPhase7Error(
                    f"Transition {transition_id!r} references unknown predicate {predicate!r}"
                )

        effects = _require_list(
            row.get("effects"), f"Transition {transition_id}.effects"
        )
        for raw_ref in effects:
            ref = _require_string(raw_ref, f"Transition {transition_id}.effect")
            if ref not in effect_map:
                raise KtrfPhase7Error(
                    f"Transition {transition_id!r} references unknown effect {ref!r}"
                )

        if "triggers" in row:
            triggers = _require_list(
                row.get("triggers"), f"Transition {transition_id}.triggers"
            )
            if not triggers:
                raise KtrfPhase7Error(
                    f"Transition {transition_id!r} triggers must not be empty when present"
                )
            seen_triggers: set[str] = set()
            for raw_trigger in triggers:
                trigger = _require_string(
                    raw_trigger, f"Transition {transition_id}.trigger"
                )
                if trigger in seen_triggers:
                    raise KtrfPhase7Error(
                        f"Transition {transition_id!r} has duplicate trigger {trigger!r}"
                    )
                seen_triggers.add(trigger)

    return node_map, expr_map, effect_map


def collect_phase7_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase6.collect_phase6_strings(document))
    except phase6.KtrfPhase6Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_transitions(document)
    _validate_transitions(document, rows)
    for row in rows:
        result.add(row["id"])
        if "triggers" in row:
            for raw_trigger in row["triggers"]:
                result.add(_require_string(raw_trigger, "Transition.trigger"))
    return result


def encode_transitions(
    document: Mapping[str, Any],
    strings: Mapping[str, int],
) -> tuple[bytes, int]:
    rows = _sorted_transitions(document)
    node_map, expr_map, effect_map = _validate_transitions(document, rows)

    records = bytearray()
    effect_refs = bytearray()
    trigger_refs = bytearray()
    effect_ref_count = 0
    trigger_ref_count = 0

    for row in rows:
        transition_id = row["id"]

        destination_i = container.NULL_INDEX
        if "destination" in row:
            destination_i = node_map[row["destination"]]

        predicate_i = container.NULL_INDEX
        if "predicate" in row:
            predicate_i = expr_map[row["predicate"]]

        effect_start = effect_ref_count
        for raw_ref in row["effects"]:
            ref = _require_string(raw_ref, f"Transition {transition_id}.effect")
            effect_refs.extend(TRAN_EFFECT_REF_STRUCT.pack(effect_map[ref]))
            effect_ref_count += 1

        triggers = row.get("triggers") if "triggers" in row else []
        trigger_start = trigger_ref_count
        for raw_trigger in triggers:
            trigger = _require_string(
                raw_trigger, f"Transition {transition_id}.trigger"
            )
            trigger_refs.extend(
                TRAN_TRIGGER_REF_STRUCT.pack(
                    _index(strings, trigger, f"Transition {transition_id}.trigger")
                )
            )
            trigger_ref_count += 1

        flags = 0
        if row["terminal"]:
            flags |= TRAN_FLAG_TERMINAL
        if "triggers" in row:
            flags |= TRAN_FLAG_HAS_TRIGGERS

        records.extend(
            TRAN_RECORD_STRUCT.pack(
                _index(strings, transition_id, "Transition.id"),
                node_map[row["source"]],
                destination_i,
                row["priority"],
                predicate_i,
                effect_start,
                len(row["effects"]),
                trigger_start,
                len(triggers),
                flags,
                0,
                0,
            )
        )

    transitions_offset = TRAN_HEADER_SIZE
    effects_offset = transitions_offset + len(rows) * TRAN_RECORD_SIZE
    triggers_offset = effects_offset + effect_ref_count * TRAN_EFFECT_REF_RECORD_SIZE

    header = TRAN_HEADER_STRUCT.pack(
        len(rows),
        TRAN_RECORD_SIZE,
        effect_ref_count,
        TRAN_EFFECT_REF_RECORD_SIZE,
        trigger_ref_count,
        TRAN_TRIGGER_REF_RECORD_SIZE,
        effects_offset,
        triggers_offset,
    )
    return header + bytes(records) + bytes(effect_refs) + bytes(trigger_refs), len(rows)


def decode_transitions(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    node_ids: Sequence[str],
    expression_ids: Sequence[str],
    effect_ids: Sequence[str],
) -> list[dict[str, Any]]:
    if len(payload) < TRAN_HEADER_SIZE:
        raise KtrfPhase7Error("TRAN payload is too small")

    (
        transition_count,
        transition_record_size,
        effect_ref_count,
        effect_ref_record_size,
        trigger_ref_count,
        trigger_ref_record_size,
        effects_offset,
        triggers_offset,
    ) = TRAN_HEADER_STRUCT.unpack_from(payload, 0)

    if transition_count != section_item_count:
        raise KtrfPhase7Error("TRAN header count does not match section item_count")
    if transition_record_size != TRAN_RECORD_SIZE:
        raise KtrfPhase7Error(
            f"unsupported TRAN record size {transition_record_size}"
        )
    if effect_ref_record_size != TRAN_EFFECT_REF_RECORD_SIZE:
        raise KtrfPhase7Error(
            f"unsupported TRAN effect-ref record size {effect_ref_record_size}"
        )
    if trigger_ref_record_size != TRAN_TRIGGER_REF_RECORD_SIZE:
        raise KtrfPhase7Error(
            f"unsupported TRAN trigger-ref record size {trigger_ref_record_size}"
        )

    transitions_offset = TRAN_HEADER_SIZE
    expected_effects_offset = transitions_offset + transition_count * TRAN_RECORD_SIZE
    if effects_offset != expected_effects_offset:
        raise KtrfPhase7Error("TRAN effects_offset is not canonical")

    expected_triggers_offset = effects_offset + effect_ref_count * TRAN_EFFECT_REF_RECORD_SIZE
    if triggers_offset != expected_triggers_offset:
        raise KtrfPhase7Error("TRAN triggers_offset is not canonical")

    expected_size = triggers_offset + trigger_ref_count * TRAN_TRIGGER_REF_RECORD_SIZE
    if len(payload) != expected_size:
        raise KtrfPhase7Error("TRAN payload size/header counts mismatch")

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    expected_effect_start = 0
    expected_trigger_start = 0

    for transition_index in range(transition_count):
        offset = transitions_offset + transition_index * TRAN_RECORD_SIZE
        (
            id_i,
            source_i,
            destination_i,
            priority,
            predicate_i,
            effect_start,
            local_effect_count,
            trigger_start,
            local_trigger_count,
            flags,
            reserved0,
            reserved1,
        ) = TRAN_RECORD_STRUCT.unpack_from(payload, offset)

        if flags & ~TRAN_FLAGS_KNOWN:
            raise KtrfPhase7Error(
                f"TRAN record {transition_index} has unknown flags 0x{flags:08X}"
            )
        if reserved0 or reserved1:
            raise KtrfPhase7Error("TRAN reserved fields must be zero in v0.1")
        if effect_start != expected_effect_start:
            raise KtrfPhase7Error("TRAN effect slices are not canonical/contiguous")
        if effect_start + local_effect_count > effect_ref_count:
            raise KtrfPhase7Error("TRAN effect slice is out of range")
        expected_effect_start += local_effect_count

        if trigger_start != expected_trigger_start:
            raise KtrfPhase7Error("TRAN trigger slices are not canonical/contiguous")
        if trigger_start + local_trigger_count > trigger_ref_count:
            raise KtrfPhase7Error("TRAN trigger slice is out of range")
        expected_trigger_start += local_trigger_count

        transition_id = _string(strings, id_i, "TRAN.id")
        key = transition_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase7Error("TRAN records are not in canonical id order")
        previous = key

        if source_i >= len(node_ids):
            raise KtrfPhase7Error("TRAN source node index out of range")

        terminal = bool(flags & TRAN_FLAG_TERMINAL)
        has_triggers = bool(flags & TRAN_FLAG_HAS_TRIGGERS)

        if terminal:
            if destination_i != container.NULL_INDEX:
                raise KtrfPhase7Error("terminal TRAN record must not have destination")
        else:
            if destination_i == container.NULL_INDEX:
                raise KtrfPhase7Error("nonterminal TRAN record requires destination")
            if destination_i >= len(node_ids):
                raise KtrfPhase7Error("TRAN destination node index out of range")

        if predicate_i != container.NULL_INDEX and predicate_i >= len(expression_ids):
            raise KtrfPhase7Error("TRAN predicate expression index out of range")

        effects: list[str] = []
        for local_index in range(local_effect_count):
            ref_index = effect_start + local_index
            (effect_i,) = TRAN_EFFECT_REF_STRUCT.unpack_from(
                payload,
                effects_offset + ref_index * TRAN_EFFECT_REF_RECORD_SIZE,
            )
            if effect_i >= len(effect_ids):
                raise KtrfPhase7Error("TRAN effect index out of range")
            effects.append(effect_ids[effect_i])

        triggers: list[str] = []
        seen_triggers: set[str] = set()
        for local_index in range(local_trigger_count):
            ref_index = trigger_start + local_index
            (trigger_i,) = TRAN_TRIGGER_REF_STRUCT.unpack_from(
                payload,
                triggers_offset + ref_index * TRAN_TRIGGER_REF_RECORD_SIZE,
            )
            trigger = _string(strings, trigger_i, "TRAN.trigger")
            if trigger in seen_triggers:
                raise KtrfPhase7Error(
                    f"TRAN record {transition_id!r} has duplicate trigger {trigger!r}"
                )
            seen_triggers.add(trigger)
            triggers.append(trigger)

        if has_triggers and not triggers:
            raise KtrfPhase7Error("TRAN HAS_TRIGGERS requires at least one trigger")
        if not has_triggers and triggers:
            raise KtrfPhase7Error("TRAN trigger payload requires HAS_TRIGGERS")

        row: dict[str, Any] = {
            "id": transition_id,
            "source": node_ids[source_i],
            "priority": priority,
            "effects": effects,
            "terminal": terminal,
        }
        if destination_i != container.NULL_INDEX:
            row["destination"] = node_ids[destination_i]
        if predicate_i != container.NULL_INDEX:
            row["predicate"] = expression_ids[predicate_i]
        if has_triggers:
            row["triggers"] = triggers
        result.append(row)

    if expected_effect_start != effect_ref_count:
        raise KtrfPhase7Error("TRAN effect refs are not fully owned by transitions")
    if expected_trigger_start != trigger_ref_count:
        raise KtrfPhase7Error("TRAN trigger refs are not fully owned by transitions")
    return result


def build_phase7_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase7_strings(document)
    strs_payload, string_map = container.encode_strs(all_strings)

    nodes = phase6.node_index_map(document)
    phase6._validate_node_cross_references(document, nodes)

    try:
        meta_payload = phase1.encode_meta(document, string_map)
        nspc_payload, nspc_count = phase1.encode_namespaces(document, string_map)
        feat_payload, feat_count = phase1.encode_features(document, string_map)
        entr_payload, entr_count = phase6.encode_entry_points_node_indexed(
            document, string_map, nodes
        )
        vars_payload, vars_count = phase1.encode_variables(document, string_map)
        rsrc_payload, rsrc_count = phase2.encode_resources(document, string_map)
        hook_payload, hook_count = phase2.encode_hooks(document, string_map)
        endg_payload, endg_count = phase2.encode_endings(document, string_map)
        expr_payload, expr_count = phase3.encode_expressions(document, string_map)
        efft_payload, efft_count = phase4.encode_effects(document, string_map)
        choi_payload, choi_count = phase6.encode_choices_node_indexed(
            document, string_map, nodes
        )
        node_payload, node_count = phase6.encode_nodes(document, string_map)
        tran_payload, tran_count = encode_transitions(document, string_map)
    except (
        phase1.KtrfPhase1Error,
        phase2.KtrfPhase2Error,
        phase3.KtrfPhase3Error,
        phase4.KtrfPhase4Error,
        phase5.KtrfPhase5Error,
        phase6.KtrfPhase6Error,
    ) as exc:
        raise _error_from(exc) from exc

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
        container.Section(b"CHOI", choi_payload, item_count=choi_count),
        container.Section(b"NODE", node_payload, item_count=node_count),
        container.Section(b"TRAN", tran_payload, item_count=tran_count),
    ]


def build_phase7_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase7_sections(document))


def _entry_by_type(
    parsed: container.ParsedContainer,
) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase7_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE7_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE7_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase7Error(
            "missing required phase-7 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase7Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase7Error("META item_count must be 1")

    try:
        result = phase1.decode_meta(parsed.sections[b"META"], strings)
        result["features"] = phase1.decode_features(
            parsed.sections[b"FEAT"], strings, entries[b"FEAT"].item_count
        )
        result["namespaces"] = phase1.decode_namespaces(
            parsed.sections[b"NSPC"], strings, entries[b"NSPC"].item_count
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

    resource_ids = [row["id"] for row in result["resource_locators"]]
    try:
        result["nodes"] = phase6.decode_nodes(
            parsed.sections[b"NODE"],
            strings,
            entries[b"NODE"].item_count,
            resource_ids,
        )
    except phase6.KtrfPhase6Error as exc:
        raise _error_from(exc) from exc
    node_ids = [row["id"] for row in result["nodes"]]

    try:
        result["entry_points"] = phase6.decode_entry_points_node_indexed(
            parsed.sections[b"ENTR"],
            strings,
            entries[b"ENTR"].item_count,
            node_ids,
        )
    except phase6.KtrfPhase6Error as exc:
        raise _error_from(exc) from exc

    entity_ids = {
        phase3.ENTITY_RESOURCE: resource_ids,
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

    try:
        result["effects"] = phase4.decode_effects(
            parsed.sections[b"EFFT"],
            strings,
            entries[b"EFFT"].item_count,
            variable_ids,
            expression_ids,
            entity_ids,
        )
    except phase4.KtrfPhase4Error as exc:
        raise _error_from(exc) from exc
    effect_ids = [row["id"] for row in result["effects"]]

    try:
        result["choices"] = phase6.decode_choices_node_indexed(
            parsed.sections[b"CHOI"],
            strings,
            entries[b"CHOI"].item_count,
            variable_ids,
            effect_ids,
            node_ids,
        )
    except phase6.KtrfPhase6Error as exc:
        raise _error_from(exc) from exc

    result["transitions"] = decode_transitions(
        parsed.sections[b"TRAN"],
        strings,
        entries[b"TRAN"].item_count,
        node_ids,
        expression_ids,
        effect_ids,
    )
    return result


def _clean_transition(row: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": row["id"],
        "source": row["source"],
        "priority": row["priority"],
        "effects": list(row["effects"]),
        "terminal": row["terminal"],
    }
    if "destination" in row:
        result["destination"] = row["destination"]
    if "predicate" in row:
        result["predicate"] = row["predicate"]
    if "triggers" in row:
        result["triggers"] = list(row["triggers"])
    return result


def canonical_phase7_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    try:
        projection = phase6.canonical_phase6_projection(document)
    except phase6.KtrfPhase6Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_transitions(document)
    _validate_transitions(document, rows)
    projection["transitions"] = [_clean_transition(row) for row in rows]
    return projection
