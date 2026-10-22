#!/usr/bin/env python3
"""KTRF binary v0.1 semantic tables — phase 6 / NODE.

Extends phase 5 with the canonical Node catalog. Node records are canonically
ordered by stable semantic ID, while resource reference order is semantic and
preserved exactly.

With NODE available, ENTR.node and CHOI.node are lowered from STRS references
to canonical NODE table indices. Decoding restores the stable Node IDs.
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

import binary_semantic_phase5_v0_1 as phase5  # noqa: E402

phase4 = phase5.phase4
phase3 = phase5.phase3
phase2 = phase5.phase2
phase1 = phase5.phase1
container = phase5.container

NODE_HEADER_STRUCT = struct.Struct("<6I")
NODE_RECORD_STRUCT = struct.Struct("<8I")
NODE_RESOURCE_REF_STRUCT = struct.Struct("<I")

NODE_HEADER_SIZE = NODE_HEADER_STRUCT.size
NODE_RECORD_SIZE = NODE_RECORD_STRUCT.size
NODE_RESOURCE_REF_RECORD_SIZE = NODE_RESOURCE_REF_STRUCT.size

PHASE6_SECTION_TYPES = phase5.PHASE5_SECTION_TYPES + (b"NODE",)


class KtrfPhase6Error(phase5.KtrfPhase5Error):
    pass


def _error_from(exc: Exception) -> KtrfPhase6Error:
    return KtrfPhase6Error(str(exc))


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


def _sorted_nodes(document: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    rows = [
        _require_mapping(row, "Node")
        for row in _require_list(document.get("nodes"), "nodes")
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc

    seen: set[str] = set()
    for row in rows:
        node_id = _require_string(row.get("id"), "Node.id")
        if node_id in seen:
            raise KtrfPhase6Error(f"duplicate Node id {node_id!r}")
        seen.add(node_id)
    return rows


def node_index_map(document: Mapping[str, Any]) -> dict[str, int]:
    return {row["id"]: index for index, row in enumerate(_sorted_nodes(document))}


def _validate_nodes(
    document: Mapping[str, Any],
    rows: Sequence[Mapping[str, Any]],
) -> dict[str, dict[str, int]]:
    try:
        catalogs = phase2.catalog_index_maps(document)
    except phase2.KtrfPhase2Error as exc:
        raise _error_from(exc) from exc

    resource_map = catalogs["RSRC"]
    for row in rows:
        node_id = row["id"]
        _require_string(row.get("kind"), f"Node {node_id}.kind")
        if "label" in row:
            _require_string(row.get("label"), f"Node {node_id}.label")
        resources = _require_list(row.get("resources"), f"Node {node_id}.resources")
        for raw_ref in resources:
            ref = _require_string(raw_ref, f"Node {node_id}.resource")
            if ref not in resource_map:
                raise KtrfPhase6Error(
                    f"Node {node_id!r} references unknown resource {ref!r}"
                )
    return catalogs


def _validate_node_cross_references(
    document: Mapping[str, Any],
    nodes: Mapping[str, int],
) -> None:
    entry_rows = [
        _require_mapping(row, "EntryPoint")
        for row in _require_list(document.get("entry_points"), "entry_points")
    ]
    for row in entry_rows:
        entry_id = _require_string(row.get("id"), "EntryPoint.id")
        node = _require_string(row.get("node"), f"EntryPoint {entry_id}.node")
        if node not in nodes:
            raise KtrfPhase6Error(
                f"EntryPoint {entry_id!r} references unknown node {node!r}"
            )

    choice_rows = phase5._sorted_choices(document)
    for row in choice_rows:
        choice_id = row["id"]
        node = _require_string(row.get("node"), f"Choice {choice_id}.node")
        if node not in nodes:
            raise KtrfPhase6Error(
                f"Choice {choice_id!r} references unknown node {node!r}"
            )


def collect_phase6_strings(document: Mapping[str, Any]) -> set[str]:
    try:
        result = set(phase5.collect_phase5_strings(document))
    except phase5.KtrfPhase5Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_nodes(document)
    _validate_nodes(document, rows)
    nodes = {row["id"]: index for index, row in enumerate(rows)}
    _validate_node_cross_references(document, nodes)

    for row in rows:
        result.add(row["id"])
        result.add(row["kind"])
        if "label" in row:
            result.add(row["label"])
    return result


def encode_nodes(
    document: Mapping[str, Any],
    strings: Mapping[str, int],
) -> tuple[bytes, int]:
    rows = _sorted_nodes(document)
    catalogs = _validate_nodes(document, rows)
    resource_map = catalogs["RSRC"]

    records = bytearray()
    resource_refs = bytearray()
    resource_ref_count = 0

    for row in rows:
        resources = row["resources"]
        resource_start = resource_ref_count
        for raw_ref in resources:
            ref = _require_string(raw_ref, f"Node {row['id']}.resource")
            resource_refs.extend(NODE_RESOURCE_REF_STRUCT.pack(resource_map[ref]))
            resource_ref_count += 1

        label_i = container.NULL_INDEX
        if "label" in row:
            label_i = _index(strings, row["label"], f"Node {row['id']}.label")

        records.extend(
            NODE_RECORD_STRUCT.pack(
                _index(strings, row["id"], "Node.id"),
                _index(strings, row["kind"], f"Node {row['id']}.kind"),
                label_i,
                resource_start,
                len(resources),
                0,
                0,
                0,
            )
        )

    nodes_offset = NODE_HEADER_SIZE
    resources_offset = nodes_offset + len(rows) * NODE_RECORD_SIZE
    header = NODE_HEADER_STRUCT.pack(
        len(rows),
        NODE_RECORD_SIZE,
        resource_ref_count,
        NODE_RESOURCE_REF_RECORD_SIZE,
        nodes_offset,
        resources_offset,
    )
    return header + bytes(records) + bytes(resource_refs), len(rows)


def decode_nodes(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    resource_ids: Sequence[str],
) -> list[dict[str, Any]]:
    if len(payload) < NODE_HEADER_SIZE:
        raise KtrfPhase6Error("NODE payload is too small")

    (
        node_count,
        node_record_size,
        resource_ref_count,
        resource_ref_record_size,
        nodes_offset,
        resources_offset,
    ) = NODE_HEADER_STRUCT.unpack_from(payload, 0)

    if node_count != section_item_count:
        raise KtrfPhase6Error("NODE header count does not match section item_count")
    if node_record_size != NODE_RECORD_SIZE:
        raise KtrfPhase6Error(f"unsupported NODE record size {node_record_size}")
    if resource_ref_record_size != NODE_RESOURCE_REF_RECORD_SIZE:
        raise KtrfPhase6Error(
            f"unsupported NODE resource-ref record size {resource_ref_record_size}"
        )
    if nodes_offset != NODE_HEADER_SIZE:
        raise KtrfPhase6Error("NODE nodes_offset is not canonical")

    expected_resources_offset = NODE_HEADER_SIZE + node_count * NODE_RECORD_SIZE
    if resources_offset != expected_resources_offset:
        raise KtrfPhase6Error("NODE resources_offset is not canonical")

    expected_size = resources_offset + resource_ref_count * NODE_RESOURCE_REF_RECORD_SIZE
    if len(payload) != expected_size:
        raise KtrfPhase6Error("NODE payload size/header counts mismatch")

    result: list[dict[str, Any]] = []
    previous: bytes | None = None
    expected_resource_start = 0

    for node_index in range(node_count):
        offset = nodes_offset + node_index * NODE_RECORD_SIZE
        (
            id_i,
            kind_i,
            label_i,
            resource_start,
            local_resource_count,
            flags,
            reserved0,
            reserved1,
        ) = NODE_RECORD_STRUCT.unpack_from(payload, offset)

        if flags or reserved0 or reserved1:
            raise KtrfPhase6Error("NODE flags/reserved fields must be zero in v0.1")
        if resource_start != expected_resource_start:
            raise KtrfPhase6Error("NODE resource slices are not canonical/contiguous")
        if resource_start + local_resource_count > resource_ref_count:
            raise KtrfPhase6Error("NODE resource slice is out of range")
        expected_resource_start += local_resource_count

        node_id = _string(strings, id_i, "NODE.id")
        key = node_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase6Error("NODE records are not in canonical id order")
        previous = key

        resources: list[str] = []
        for local_index in range(local_resource_count):
            ref_index = resource_start + local_index
            (resource_index,) = NODE_RESOURCE_REF_STRUCT.unpack_from(
                payload,
                resources_offset + ref_index * NODE_RESOURCE_REF_RECORD_SIZE,
            )
            if resource_index >= len(resource_ids):
                raise KtrfPhase6Error("NODE resource index out of range")
            resources.append(resource_ids[resource_index])

        row: dict[str, Any] = {
            "id": node_id,
            "kind": _string(strings, kind_i, "NODE.kind"),
            "resources": resources,
        }
        if label_i != container.NULL_INDEX:
            row["label"] = _string(strings, label_i, "NODE.label")
        result.append(row)

    if expected_resource_start != resource_ref_count:
        raise KtrfPhase6Error("NODE resource refs are not fully owned by nodes")
    return result


def encode_entry_points_node_indexed(
    document: Mapping[str, Any],
    strings: Mapping[str, int],
    nodes: Mapping[str, int],
) -> tuple[bytes, int]:
    rows = [
        _require_mapping(row, "EntryPoint")
        for row in _require_list(document.get("entry_points"), "entry_points")
    ]
    try:
        rows = phase1._sorted_records(rows, "id")
    except phase1.KtrfPhase1Error as exc:
        raise _error_from(exc) from exc

    payload = bytearray()
    seen: set[str] = set()
    for row in rows:
        entry_id = _require_string(row.get("id"), "EntryPoint.id")
        if entry_id in seen:
            raise KtrfPhase6Error(f"duplicate EntryPoint id {entry_id!r}")
        seen.add(entry_id)
        node = _require_string(row.get("node"), f"EntryPoint {entry_id}.node")
        if node not in nodes:
            raise KtrfPhase6Error(
                f"EntryPoint {entry_id!r} references unknown node {node!r}"
            )
        trigger = _require_string(row.get("trigger"), f"EntryPoint {entry_id}.trigger")
        payload.extend(
            phase1.ENTRY_STRUCT.pack(
                _index(strings, entry_id, "EntryPoint.id"),
                nodes[node],
                _index(strings, trigger, "EntryPoint.trigger"),
            )
        )
    return bytes(payload), len(rows)


def decode_entry_points_node_indexed(
    payload: bytes,
    strings: Sequence[str],
    count: int,
    node_ids: Sequence[str],
) -> list[dict[str, str]]:
    if len(payload) != count * phase1.ENTR_RECORD_SIZE:
        raise KtrfPhase6Error("ENTR payload size/item_count mismatch")

    result: list[dict[str, str]] = []
    previous: bytes | None = None
    for index in range(count):
        id_i, node_i, trigger_i = phase1.ENTRY_STRUCT.unpack_from(
            payload, index * phase1.ENTR_RECORD_SIZE
        )
        entry_id = _string(strings, id_i, "ENTR.id")
        key = entry_id.encode("utf-8")
        if previous is not None and key <= previous:
            raise KtrfPhase6Error("ENTR records are not in canonical id order")
        previous = key
        if node_i >= len(node_ids):
            raise KtrfPhase6Error("ENTR node index out of range")
        result.append(
            {
                "id": entry_id,
                "node": node_ids[node_i],
                "trigger": _string(strings, trigger_i, "ENTR.trigger"),
            }
        )
    return result


def encode_choices_node_indexed(
    document: Mapping[str, Any],
    strings: Mapping[str, int],
    nodes: Mapping[str, int],
) -> tuple[bytes, int]:
    try:
        payload, count = phase5.encode_choices(document, strings)
    except phase5.KtrfPhase5Error as exc:
        raise _error_from(exc) from exc

    rows = phase5._sorted_choices(document)
    mutable = bytearray(payload)
    header = phase5.CHOI_HEADER_STRUCT.unpack_from(mutable, 0)
    choices_offset = header[8]

    for choice_index, row in enumerate(rows):
        node = _require_string(row.get("node"), f"Choice {row['id']}.node")
        if node not in nodes:
            raise KtrfPhase6Error(
                f"Choice {row['id']!r} references unknown node {node!r}"
            )
        record_offset = choices_offset + choice_index * phase5.CHOI_RECORD_SIZE
        struct.pack_into("<I", mutable, record_offset + 4, nodes[node])

    return bytes(mutable), count


def decode_choices_node_indexed(
    payload: bytes,
    strings: Sequence[str],
    section_item_count: int,
    variable_ids: Sequence[str],
    effect_ids: Sequence[str],
    node_ids: Sequence[str],
) -> list[dict[str, Any]]:
    if len(payload) < phase5.CHOI_HEADER_SIZE:
        raise KtrfPhase6Error("CHOI payload is too small")

    mutable = bytearray(payload)
    header = phase5.CHOI_HEADER_STRUCT.unpack_from(mutable, 0)
    choice_count = header[0]
    choices_offset = header[8]
    if choice_count != section_item_count:
        raise KtrfPhase6Error("CHOI header count does not match section item_count")

    string_map = {value: index for index, value in enumerate(strings)}
    for choice_index in range(choice_count):
        record_offset = choices_offset + choice_index * phase5.CHOI_RECORD_SIZE
        (node_index,) = struct.unpack_from("<I", mutable, record_offset + 4)
        if node_index >= len(node_ids):
            raise KtrfPhase6Error("CHOI node index out of range")
        node_id = node_ids[node_index]
        if node_id not in string_map:
            raise KtrfPhase6Error("CHOI node stable ID is missing from STRS")
        struct.pack_into("<I", mutable, record_offset + 4, string_map[node_id])

    try:
        return phase5.decode_choices(
            bytes(mutable),
            strings,
            section_item_count,
            variable_ids,
            effect_ids,
        )
    except phase5.KtrfPhase5Error as exc:
        raise _error_from(exc) from exc


def build_phase6_sections(document: Mapping[str, Any]) -> list[container.Section]:
    all_strings = collect_phase6_strings(document)
    strs_payload, string_map = container.encode_strs(all_strings)

    nodes = node_index_map(document)
    _validate_node_cross_references(document, nodes)

    try:
        meta_payload = phase1.encode_meta(document, string_map)
        nspc_payload, nspc_count = phase1.encode_namespaces(document, string_map)
        feat_payload, feat_count = phase1.encode_features(document, string_map)
        entr_payload, entr_count = encode_entry_points_node_indexed(
            document, string_map, nodes
        )
        vars_payload, vars_count = phase1.encode_variables(document, string_map)
        rsrc_payload, rsrc_count = phase2.encode_resources(document, string_map)
        hook_payload, hook_count = phase2.encode_hooks(document, string_map)
        endg_payload, endg_count = phase2.encode_endings(document, string_map)
        expr_payload, expr_count = phase3.encode_expressions(document, string_map)
        efft_payload, efft_count = phase4.encode_effects(document, string_map)
        choi_payload, choi_count = encode_choices_node_indexed(
            document, string_map, nodes
        )
        node_payload, node_count = encode_nodes(document, string_map)
    except (
        phase1.KtrfPhase1Error,
        phase2.KtrfPhase2Error,
        phase3.KtrfPhase3Error,
        phase4.KtrfPhase4Error,
        phase5.KtrfPhase5Error,
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
    ]


def build_phase6_container(document: Mapping[str, Any]) -> bytes:
    return container.build_container(build_phase6_sections(document))


def _entry_by_type(
    parsed: container.ParsedContainer,
) -> dict[bytes, container.SectionEntry]:
    return {entry.type_code: entry for entry in parsed.entries}


def parse_phase6_container(data: bytes) -> dict[str, Any]:
    parsed = container.parse_container(data, known_section_types=PHASE6_SECTION_TYPES)
    entries = _entry_by_type(parsed)
    missing = [code for code in PHASE6_SECTION_TYPES if code not in parsed.sections]
    if missing:
        raise KtrfPhase6Error(
            "missing required phase-6 sections: "
            + ",".join(code.decode("ascii") for code in missing)
        )

    strings = container.decode_strs(parsed.sections[b"STRS"])
    if entries[b"STRS"].item_count != len(strings):
        raise KtrfPhase6Error("STRS item_count mismatch")
    if entries[b"META"].item_count != 1:
        raise KtrfPhase6Error("META item_count must be 1")

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
    result["nodes"] = decode_nodes(
        parsed.sections[b"NODE"],
        strings,
        entries[b"NODE"].item_count,
        resource_ids,
    )
    node_ids = [row["id"] for row in result["nodes"]]

    result["entry_points"] = decode_entry_points_node_indexed(
        parsed.sections[b"ENTR"],
        strings,
        entries[b"ENTR"].item_count,
        node_ids,
    )

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
    result["choices"] = decode_choices_node_indexed(
        parsed.sections[b"CHOI"],
        strings,
        entries[b"CHOI"].item_count,
        variable_ids,
        effect_ids,
        node_ids,
    )
    return result


def _clean_node(row: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": row["id"],
        "kind": row["kind"],
        "resources": list(row["resources"]),
    }
    if "label" in row:
        result["label"] = row["label"]
    return result


def canonical_phase6_projection(document: Mapping[str, Any]) -> dict[str, Any]:
    try:
        projection = phase5.canonical_phase5_projection(document)
    except phase5.KtrfPhase5Error as exc:
        raise _error_from(exc) from exc

    rows = _sorted_nodes(document)
    _validate_nodes(document, rows)
    nodes = {row["id"]: index for index, row in enumerate(rows)}
    _validate_node_cross_references(document, nodes)
    projection["nodes"] = [_clean_node(row) for row in rows]
    return projection
