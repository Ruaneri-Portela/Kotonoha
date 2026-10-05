#!/usr/bin/env python3
"""Export the frozen School Days HQ routing oracle to KTRF Canonical Routing IR.

The exporter reads only the generated, already-frozen router tables. It does not
read the original game DLL and it does not infer UI/save semantics.

Source oracle:
  tag    school-days-routing-oracle-v1
  commit 614461c2b14951ba117b9d2dedb4983cfa8ae8e6

The output is intentionally human-readable JSON IR, not the future .ktnroute
binary representation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ORACLE_TAG = "school-days-routing-oracle-v1"
ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
PROFILE_ID = "overflow.school-days-hq"
PROFILE_VERSION = "1.0.0"
PROFILE_NAMESPACE = "overflow.sdhq"
PROFILE_NAMESPACE_URI = "urn:kotonoha:profile:overflow:school-days-hq"

EXPECTED = {
    "routes": 55,
    "nodes": 1857,
    "transitions": 2458,
    "conditions": 1464,
    "effects": 6183,
    "feeling_deltas": 302,
    "feeling_resolutions": 772,
    "choices": 287,
    "endings": 22,
    "callback38_nonterminal": 47,
    "callback38_terminal": 23,
}

ROUTING_ONLY_SCENES = {
    "03/03-B2-A00",
    "03/03-KB-E00",
}

KNOWN_DEAD_TRANSITION_IDS = {971, 1243, 1248, 1494, 1824, 1825}

CORE_COMPARE_OP = {
    "Eq": "ktrf:eq",
    "Ne": "ktrf:ne",
    "Gt": "ktrf:gt",
    "Le": "ktrf:le",
}


@dataclass(frozen=True)
class ParsedModel:
    route_offsets: list[int]
    nodes: list[dict[str, Any]]
    conditions: list[dict[str, Any]]
    effects: list[dict[str, Any]]
    transitions: list[dict[str, Any]]
    feeling_deltas: list[dict[str, Any]]
    feeling_resolutions: list[dict[str, Any]]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def array_body(text: str, typename: str, name: str) -> str:
    pattern = rf"static constexpr {re.escape(typename)} {re.escape(name)}\[\]\s*=\s*\{{(.*?)\n\}};"
    match = re.search(pattern, text, re.S)
    if not match:
        raise RuntimeError(f"array not found: {name}")
    return match.group(1)


def unquote(value: str) -> str | None:
    if value == "nullptr":
        return None
    return value[1:-1]


def parse_generated(path: Path) -> ParsedModel:
    text = path.read_text(encoding="utf-8")

    offsets_match = re.search(
        r"static constexpr uint32_t kRouteOffsets\[\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if not offsets_match:
        raise RuntimeError("kRouteOffsets not found")
    route_offsets = [int(x) for x in re.findall(r"\d+", offsets_match.group(1))]

    nodes: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{"([^"]+)",\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\}',
        array_body(text, "NodeData", "kNodes"),
    ):
        nodes.append(
            {
                "scene_key": m.group(1),
                "transition_start": int(m.group(2)),
                "transition_count": int(m.group(3)),
                "choice_mask": int(m.group(4)),
            }
        )

    conditions: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{ConditionKind::(\w+),\s*CompareOp::(\w+),\s*'
        r'(nullptr|"[^"]*"),\s*(nullptr|"[^"]*"),\s*(-?\d+)\}',
        array_body(text, "ConditionData", "kConditions"),
    ):
        conditions.append(
            {
                "kind": m.group(1),
                "op": m.group(2),
                "source_a": unquote(m.group(3)),
                "source_b": unquote(m.group(4)),
                "value": int(m.group(5)),
            }
        )

    effects: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{EffectKind::(\w+),\s*(nullptr|"[^"]*"),\s*'
        r'(nullptr|"[^"]*"),\s*(-?\d+)\}',
        array_body(text, "EffectData", "kEffects"),
    ):
        effects.append(
            {
                "kind": m.group(1),
                "target": unquote(m.group(2)),
                "source": unquote(m.group(3)),
                "value": int(m.group(4)),
            }
        )

    transitions: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{(\d+)u,\s*(-?\d+),\s*(-?\d+),\s*'
        r'(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(true|false)\}',
        array_body(text, "TransitionData", "kTransitions"),
    ):
        transitions.append(
            {
                "id": int(m.group(1)),
                "destination_route": int(m.group(2)),
                "destination_scene": int(m.group(3)),
                "condition_start": int(m.group(4)),
                "condition_count": int(m.group(5)),
                "effect_start": int(m.group(6)),
                "effect_count": int(m.group(7)),
                "terminal": m.group(8) == "true",
            }
        )

    feeling_deltas: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{"([^"]+)",\s*(-?\d+)\}',
        array_body(text, "FeelingDelta", "kFeelingDeltas"),
    ):
        feeling_deltas.append({"variable": m.group(1), "delta": int(m.group(2))})

    feeling_resolutions: list[dict[str, Any]] = []
    for m in re.finditer(
        r'\{(\d+)u,\s*(-?\d+),\s*(\d+)u,\s*(\d+)u\}',
        array_body(text, "FeelingResolution", "kFeelingResolutions"),
    ):
        feeling_resolutions.append(
            {
                "node_index": int(m.group(1)),
                "choice": int(m.group(2)),
                "delta_start": int(m.group(3)),
                "delta_count": int(m.group(4)),
            }
        )

    model = ParsedModel(
        route_offsets=route_offsets,
        nodes=nodes,
        conditions=conditions,
        effects=effects,
        transitions=transitions,
        feeling_deltas=feeling_deltas,
        feeling_resolutions=feeling_resolutions,
    )
    assert_source_counts(model)
    return model


def assert_source_counts(model: ParsedModel) -> None:
    actual = {
        "routes": len(model.route_offsets) - 1,
        "nodes": len(model.nodes),
        "transitions": len(model.transitions),
        "conditions": len(model.conditions),
        "effects": len(model.effects),
        "feeling_deltas": len(model.feeling_deltas),
        "feeling_resolutions": len(model.feeling_resolutions),
    }
    for key, value in actual.items():
        expected = EXPECTED[key]
        if value != expected:
            raise RuntimeError(f"oracle count mismatch for {key}: {value} != {expected}")

    if not model.route_offsets or model.route_offsets[0] != 0:
        raise RuntimeError("invalid route offset table")
    if model.route_offsets[-1] != len(model.nodes):
        raise RuntimeError("last route offset does not match node count")
    if any(a > b for a, b in zip(model.route_offsets, model.route_offsets[1:])):
        raise RuntimeError("route offsets are not monotonic")

    for node in model.nodes:
        if node["transition_start"] + node["transition_count"] > len(model.transitions):
            raise RuntimeError(f"invalid transition slice for node {node['scene_key']}")
    for t in model.transitions:
        if t["condition_start"] + t["condition_count"] > len(model.conditions):
            raise RuntimeError(f"invalid condition slice for t{t['id']}")
        if t["effect_start"] + t["effect_count"] > len(model.effects):
            raise RuntimeError(f"invalid effect slice for t{t['id']}")
    for r in model.feeling_resolutions:
        if r["node_index"] >= len(model.nodes):
            raise RuntimeError("feeling resolution references invalid node")
        if r["delta_start"] + r["delta_count"] > len(model.feeling_deltas):
            raise RuntimeError("feeling resolution references invalid delta slice")


def safe_component(value: str) -> str:
    out: list[str] = []
    for ch in value:
        if re.match(r"[A-Za-z0-9._/-]", ch):
            out.append(ch)
        else:
            out.append(f"_x{ord(ch):04X}_")
    return "".join(out)


def node_id(scene_key: str) -> str:
    return f"sdhq:node:{scene_key}"


def session_var_id(symbol: str) -> str:
    return f"sdhq:var:session:{safe_component(symbol)}"


def global_var_id(symbol: str) -> str:
    return f"sdhq:var:global:{safe_component(symbol)}"


def hook_id(symbol: str) -> str:
    return f"sdhq:hook:{safe_component(symbol)}"


def literal(value: Any) -> dict[str, Any]:
    return {"kind": "literal", "value": value}


def variable(ref: str) -> dict[str, Any]:
    return {"kind": "variable", "ref": ref}


def expression(ref: str) -> dict[str, Any]:
    return {"kind": "expression", "ref": ref}


def node_coordinates(model: ParsedModel) -> tuple[list[int], list[int]]:
    routes = [-1] * len(model.nodes)
    scenes = [-1] * len(model.nodes)
    for route in range(len(model.route_offsets) - 1):
        begin = model.route_offsets[route]
        end = model.route_offsets[route + 1]
        for index in range(begin, end):
            routes[index] = route
            scenes[index] = index - begin
    if any(x < 0 for x in routes) or any(x < 0 for x in scenes):
        raise RuntimeError("failed to assign route/scene coordinate to every node")
    return routes, scenes


def collect_variables(model: ParsedModel) -> tuple[set[str], set[str]]:
    session = {"ROUTE", "SCENE", "000", "001", "002", "003", "004"}
    global_ = {"dword_3A6F40", "dword_3A2294"}

    for c in model.conditions:
        kind = c["kind"]
        if kind == "SessionValue":
            if c["source_a"] is not None:
                session.add(c["source_a"])
        elif kind == "SessionComparison":
            if c["source_a"] is not None:
                session.add(c["source_a"])
            if c["source_b"] is not None:
                session.add(c["source_b"])
        elif kind == "GlobalValue":
            if c["source_a"] is not None:
                global_.add(c["source_a"])
        elif kind == "FlagOr":
            if c["source_a"] is not None:
                session.add(c["source_a"])
            if c["source_b"] is not None:
                session.add(c["source_b"])

    for e in model.effects:
        kind = e["kind"]
        if kind == "SetSessionConst":
            if e["target"] is not None:
                session.add(e["target"])
        elif kind == "SetSessionFromSession":
            if e["target"] is not None:
                session.add(e["target"])
            if e["source"] is not None:
                session.add(e["source"])
        elif kind == "SetGlobalConst":
            if e["target"] is not None:
                global_.add(e["target"])

    for d in model.feeling_deltas:
        session.add(d["variable"])

    return session, global_


def build_condition_expressions(
    model: ParsedModel,
    choice_result_var: str,
    callback34_var: str,
) -> tuple[list[dict[str, Any]], list[str]]:
    expressions: list[dict[str, Any]] = []
    final_ids: list[str] = []

    for index, c in enumerate(model.conditions):
        final_id = f"sdhq:expr:condition:{index}"
        final_ids.append(final_id)
        kind = c["kind"]
        op = c["op"]

        if op not in CORE_COMPARE_OP:
            raise RuntimeError(f"unsupported CompareOp {op!r} at condition {index}")

        metadata = {
            "source_condition_index": index,
            "source_kind": kind,
            "source_compare_op": op,
            "source_a": c["source_a"],
            "source_b": c["source_b"],
            "source_value": c["value"],
        }

        if kind == "FlagOr":
            a = c["source_a"]
            b = c["source_b"]
            if a is None or b is None:
                raise RuntimeError(f"FlagOr condition {index} has null operand")
            if op not in {"Eq", "Ne"} or c["value"] not in {0, 1}:
                raise RuntimeError(
                    f"FlagOr condition {index} cannot be losslessly lowered with core v0.1: {op} {c['value']}"
                )

            a_id = f"sdhq:expr:condition:{index}:a-nonzero"
            b_id = f"sdhq:expr:condition:{index}:b-nonzero"
            or_id = f"sdhq:expr:condition:{index}:or"
            expressions.extend(
                [
                    {
                        "id": a_id,
                        "op": "ktrf:ne",
                        "args": [variable(session_var_id(a)), literal(0)],
                        "result_type": "ktrf:bool",
                        "metadata": {"lowering_helper_for_condition": index},
                    },
                    {
                        "id": b_id,
                        "op": "ktrf:ne",
                        "args": [variable(session_var_id(b)), literal(0)],
                        "result_type": "ktrf:bool",
                        "metadata": {"lowering_helper_for_condition": index},
                    },
                    {
                        "id": or_id,
                        "op": "ktrf:or",
                        "args": [expression(a_id), expression(b_id)],
                        "result_type": "ktrf:bool",
                        "metadata": {"lowering_helper_for_condition": index},
                    },
                ]
            )

            # Original code converts the OR result to integer 0/1 and then
            # compares it. For the observed Eq/Ne against 0/1 this boolean
            # equality is exactly equivalent and avoids inventing casts.
            if op == "Eq":
                expected_bool = c["value"] == 1
            else:
                expected_bool = c["value"] == 0
            expressions.append(
                {
                    "id": final_id,
                    "op": "ktrf:eq",
                    "args": [expression(or_id), literal(expected_bool)],
                    "result_type": "ktrf:bool",
                    "metadata": metadata,
                }
            )
            continue

        if kind == "Choice":
            left = variable(choice_result_var)
            right = literal(c["value"])
        elif kind == "SessionValue":
            if c["source_a"] is None:
                raise RuntimeError(f"SessionValue condition {index} has null source")
            left = variable(session_var_id(c["source_a"]))
            right = literal(c["value"])
        elif kind == "SessionComparison":
            if c["source_a"] is None or c["source_b"] is None:
                raise RuntimeError(f"SessionComparison condition {index} has null source")
            left = variable(session_var_id(c["source_a"]))
            right = variable(session_var_id(c["source_b"]))
        elif kind == "GlobalValue":
            if c["source_a"] is None:
                raise RuntimeError(f"GlobalValue condition {index} has null source")
            left = variable(global_var_id(c["source_a"]))
            right = literal(c["value"])
        elif kind == "Callback34":
            left = variable(callback34_var)
            right = literal(c["value"])
        else:
            raise RuntimeError(f"unsupported ConditionKind {kind!r} at condition {index}")

        expressions.append(
            {
                "id": final_id,
                "op": CORE_COMPARE_OP[op],
                "args": [left, right],
                "result_type": "ktrf:bool",
                "metadata": metadata,
            }
        )

    return expressions, final_ids


def build_document(model: ParsedModel, generated_path: Path) -> dict[str, Any]:
    routes, scenes = node_coordinates(model)

    transition_owner: dict[int, tuple[int, int]] = {}
    for node_index, n in enumerate(model.nodes):
        for local_priority in range(n["transition_count"]):
            transition_index = n["transition_start"] + local_priority
            if transition_index in transition_owner:
                raise RuntimeError(f"transition index {transition_index} is owned by multiple nodes")
            transition_owner[transition_index] = (node_index, local_priority)
    if len(transition_owner) != len(model.transitions):
        raise RuntimeError(
            f"not all transitions are owned by nodes: {len(transition_owner)} / {len(model.transitions)}"
        )

    session_names, global_names = collect_variables(model)
    choice_result_var = "sdhq:var:internal:choice_result"
    callback34_var = "sdhq:var:internal:callback34"

    variables: list[dict[str, Any]] = []
    for name in sorted(session_names):
        variables.append(
            {
                "id": session_var_id(name),
                "type": "ktrf:int32",
                "scope": "ktrf:session",
                "default": 0,
                "metadata": {"source_symbol": name, "source_storage": "session"},
            }
        )
    for name in sorted(global_names):
        variables.append(
            {
                "id": global_var_id(name),
                "type": "ktrf:int32",
                "scope": "ktrf:global",
                "default": 1 if name == "dword_3A2294" else 0,
                "metadata": {"source_symbol": name, "source_storage": "global"},
            }
        )
    variables.extend(
        [
            {
                "id": choice_result_var,
                "type": "ktrf:int32",
                "scope": "ktrf:session",
                "default": -2,
                "metadata": {
                    "source_symbol": "choiceResult",
                    "source_storage": "router-internal",
                    "pending_value": -2,
                    "timeout_value": -1,
                },
            },
            {
                "id": callback34_var,
                "type": "ktrf:int32",
                "scope": "ktrf:session",
                "default": 0,
                "metadata": {
                    "source_symbol": "callback34",
                    "source_storage": "router-internal",
                },
            },
        ]
    )

    resource_locators: list[dict[str, Any]] = []
    nodes: list[dict[str, Any]] = []
    for index, n in enumerate(model.nodes):
        scene_key = n["scene_key"]
        resources: list[str] = []
        kind = "ktrf:dispatcher" if scene_key in ROUTING_ONLY_SCENES else "ktrf:scene"
        if scene_key not in ROUTING_ONLY_SCENES:
            resource_id = f"sdhq:resource:node:{index}"
            resources.append(resource_id)
            resource_locators.append(
                {
                    "id": resource_id,
                    "scheme": "overflow.sdhq:scene-key",
                    "value": scene_key,
                    "media_type": "application/x-overflow-ors",
                    "required": True,
                    "metadata": {
                        "source_scene_key": scene_key,
                        "resolver": "School Days HQ scene-key adapter",
                    },
                }
            )

        nodes.append(
            {
                "id": node_id(scene_key),
                "kind": kind,
                "resources": resources,
                "metadata": {
                    "source_node_index": index,
                    "scene_key": scene_key,
                    "route": routes[index],
                    "scene": scenes[index],
                    "episode": int(scene_key[0:2]),
                    "source_transition_start": n["transition_start"],
                    "source_transition_count": n["transition_count"],
                    "source_choice_mask": n["choice_mask"],
                    "routing_only": scene_key in ROUTING_ONLY_SCENES,
                },
            }
        )

    callback_symbols = sorted(
        {
            e["target"]
            for e in model.effects
            if e["kind"] == "Callback" and e["target"] is not None
        }
    )
    external_hooks = [
        {
            "id": hook_id(symbol),
            "symbol": f"{PROFILE_NAMESPACE}:{symbol}",
            "contract": "overflow.sdhq.callback/1.0.0",
            "metadata": {"source_callback": symbol},
        }
        for symbol in callback_symbols
    ]

    ending_codes = sorted(
        {
            e["value"]
            for e in model.effects
            if e["kind"] == "RegisterEnding"
        }
    )
    if ending_codes != list(range(EXPECTED["endings"])):
        raise RuntimeError(f"unexpected ending catalog: {ending_codes}")
    endings = [
        {
            "id": f"sdhq:ending:{code}",
            "code": code,
            "metadata": {"source_registration_code": code},
        }
        for code in ending_codes
    ]

    expressions, source_condition_expr_ids = build_condition_expressions(
        model,
        choice_result_var,
        callback34_var,
    )

    effects: list[dict[str, Any]] = []
    source_effect_ids: list[str] = []
    for index, e in enumerate(model.effects):
        effect_id = f"sdhq:effect:source:{index}"
        source_effect_ids.append(effect_id)
        kind = e["kind"]
        metadata = {
            "source_effect_index": index,
            "source_kind": kind,
            "source_target": e["target"],
            "source_source": e["source"],
            "source_value": e["value"],
        }

        if kind == "SetSessionConst":
            if e["target"] is None:
                raise RuntimeError(f"SetSessionConst effect {index} has null target")
            out = {
                "id": effect_id,
                "op": "ktrf:set",
                "args": {
                    "target": session_var_id(e["target"]),
                    "value": literal(e["value"]),
                },
                "metadata": metadata,
            }
        elif kind == "SetSessionFromSession":
            if e["target"] is None or e["source"] is None:
                raise RuntimeError(f"SetSessionFromSession effect {index} has null operand")
            out = {
                "id": effect_id,
                "op": "ktrf:copy",
                "args": {
                    "target": session_var_id(e["target"]),
                    "source": session_var_id(e["source"]),
                },
                "metadata": metadata,
            }
        elif kind == "SetGlobalConst":
            if e["target"] is None:
                raise RuntimeError(f"SetGlobalConst effect {index} has null target")
            out = {
                "id": effect_id,
                "op": "ktrf:set",
                "args": {
                    "target": global_var_id(e["target"]),
                    "value": literal(e["value"]),
                },
                "metadata": metadata,
            }
        elif kind == "RegisterEnding":
            out = {
                "id": effect_id,
                "op": "ktrf:register-ending",
                "args": {"ending": f"sdhq:ending:{e['value']}"},
                "metadata": metadata,
            }
        elif kind == "Callback":
            if e["target"] is None:
                raise RuntimeError(f"Callback effect {index} has null target")
            out = {
                "id": effect_id,
                "op": "ktrf:call-hook",
                "args": {"hook": hook_id(e["target"]), "arguments": []},
                "metadata": metadata,
            }
        else:
            raise RuntimeError(f"unsupported EffectKind {kind!r} at effect {index}")
        effects.append(out)

    feeling_effect_ids: list[str] = []
    for index, d in enumerate(model.feeling_deltas):
        effect_id = f"sdhq:effect:feeling:{index}"
        feeling_effect_ids.append(effect_id)
        effects.append(
            {
                "id": effect_id,
                "op": "ktrf:add",
                "args": {
                    "target": session_var_id(d["variable"]),
                    "value": literal(d["delta"]),
                },
                "metadata": {
                    "source_feeling_delta_index": index,
                    "source_variable": d["variable"],
                    "source_delta": d["delta"],
                },
            }
        )

    reset_choice_effect = "sdhq:effect:profile:reset-choice-result"
    effects.append(
        {
            "id": reset_choice_effect,
            "op": "ktrf:set",
            "args": {"target": choice_result_var, "value": literal(-2)},
            "metadata": {
                "profile_synthetic": True,
                "purpose": "mirror SchoolDaysRouter post-transition choiceResult reset",
                "oracle_order": "after source effects, before destination coordinate mirror",
            },
        }
    )

    resolution_by_outcome: dict[tuple[int, int], tuple[int, dict[str, Any]]] = {}
    for resolution_index, r in enumerate(model.feeling_resolutions):
        key = (r["node_index"], r["choice"])
        if key in resolution_by_outcome:
            raise RuntimeError(f"duplicate feeling resolution for node/choice {key}")
        resolution_by_outcome[key] = (resolution_index, r)

    choices: list[dict[str, Any]] = []
    used_resolutions: set[int] = set()

    def resolution_payload(node_index: int, choice_value: int) -> tuple[list[str], dict[str, Any]]:
        found = resolution_by_outcome.get((node_index, choice_value))
        if found is None:
            return [], {"source_feeling_resolution": None}
        resolution_index, r = found
        used_resolutions.add(resolution_index)
        refs = [
            feeling_effect_ids[i]
            for i in range(r["delta_start"], r["delta_start"] + r["delta_count"])
        ]
        return refs, {
            "source_feeling_resolution_index": resolution_index,
            "source_delta_start": r["delta_start"],
            "source_delta_count": r["delta_count"],
        }

    for node_index, n in enumerate(model.nodes):
        mask = n["choice_mask"]
        if mask == 0:
            continue
        options: list[dict[str, Any]] = []
        for option_value in range(8):
            if not (mask & (1 << option_value)):
                continue
            outcome_effects, outcome_meta = resolution_payload(node_index, option_value)
            options.append(
                {
                    "id": f"option-{option_value}",
                    "value": option_value,
                    "effects": outcome_effects,
                    "metadata": outcome_meta,
                }
            )
        timeout_effects, timeout_meta = resolution_payload(node_index, -1)
        choices.append(
            {
                "id": f"sdhq:choice:node:{node_index}",
                "node": node_id(n["scene_key"]),
                "result_variable": choice_result_var,
                "routing_policy": "ktrf:deferred",
                "options": options,
                "timeout": {
                    "value": -1,
                    "effects": timeout_effects,
                    "metadata": timeout_meta,
                },
                "metadata": {
                    "source_node_index": node_index,
                    "source_scene_key": n["scene_key"],
                    "source_choice_mask": mask,
                    "pending_value": -2,
                },
            }
        )

    if len(choices) != EXPECTED["choices"]:
        raise RuntimeError(f"choice count mismatch: {len(choices)} != {EXPECTED['choices']}")
    if len(used_resolutions) != len(model.feeling_resolutions):
        missing = sorted(set(range(len(model.feeling_resolutions))) - used_resolutions)
        raise RuntimeError(
            f"not all feeling resolutions map to exported choice outcomes: "
            f"{len(used_resolutions)} / {len(model.feeling_resolutions)}; first missing={missing[:10]}"
        )

    transitions: list[dict[str, Any]] = []
    callback38_terminal = 0
    callback38_nonterminal = 0
    dead_found: set[int] = set()

    for transition_index, t in enumerate(model.transitions):
        owner = transition_owner.get(transition_index)
        if owner is None:
            raise RuntimeError(f"transition index {transition_index} has no source node")
        source_node_index, priority = owner
        source_node = model.nodes[source_node_index]

        predicate: str | None = None
        condition_ids = [
            source_condition_expr_ids[i]
            for i in range(t["condition_start"], t["condition_start"] + t["condition_count"])
        ]
        if len(condition_ids) == 1:
            predicate = condition_ids[0]
        elif len(condition_ids) > 1:
            predicate = f"sdhq:expr:transition:{t['id']}:all"
            expressions.append(
                {
                    "id": predicate,
                    "op": "ktrf:and",
                    "args": [expression(x) for x in condition_ids],
                    "result_type": "ktrf:bool",
                    "metadata": {
                        "lowering_role": "transition-condition-conjunction",
                        "source_transition_id": t["id"],
                        "source_condition_start": t["condition_start"],
                        "source_condition_count": t["condition_count"],
                    },
                }
            )

        transition_effects = [
            source_effect_ids[i]
            for i in range(t["effect_start"], t["effect_start"] + t["effect_count"])
        ]

        has_callback38 = any(
            model.effects[i]["kind"] == "Callback"
            and model.effects[i]["target"] == "callback_38"
            for i in range(t["effect_start"], t["effect_start"] + t["effect_count"])
        )
        if has_callback38:
            if t["terminal"]:
                callback38_terminal += 1
            else:
                callback38_nonterminal += 1

        # SchoolDaysRouter resets choiceResult after all source effects, for
        # both terminal and non-terminal selected transitions.
        transition_effects.append(reset_choice_effect)

        destination: str | None = None
        synthetic_coordinate_effects: list[str] = []
        if not t["terminal"]:
            route = t["destination_route"]
            scene = t["destination_scene"]
            if not (0 <= route < len(model.route_offsets) - 1):
                raise RuntimeError(f"invalid destination route on t{t['id']}: {route}")
            route_size = model.route_offsets[route + 1] - model.route_offsets[route]
            if not (0 <= scene < route_size):
                raise RuntimeError(f"invalid destination scene on t{t['id']}: {route}/{scene}")
            destination_index = model.route_offsets[route] + scene
            destination = node_id(model.nodes[destination_index]["scene_key"])

            # The executable oracle mirrors destinationRoute/destinationScene
            # into ROUTE/SCENE after source effects and after choice reset.
            route_effect_id = f"sdhq:effect:post-transition:{t['id']}:route"
            scene_effect_id = f"sdhq:effect:post-transition:{t['id']}:scene"
            effects.extend(
                [
                    {
                        "id": route_effect_id,
                        "op": "ktrf:set",
                        "args": {"target": session_var_id("ROUTE"), "value": literal(route)},
                        "metadata": {
                            "profile_synthetic": True,
                            "source_transition_id": t["id"],
                            "purpose": "mirror destinationRoute into ROUTE after source effects",
                        },
                    },
                    {
                        "id": scene_effect_id,
                        "op": "ktrf:set",
                        "args": {"target": session_var_id("SCENE"), "value": literal(scene)},
                        "metadata": {
                            "profile_synthetic": True,
                            "source_transition_id": t["id"],
                            "purpose": "mirror destinationScene into SCENE after source effects",
                        },
                    },
                ]
            )
            synthetic_coordinate_effects = [route_effect_id, scene_effect_id]
            transition_effects.extend(synthetic_coordinate_effects)

        if t["id"] in KNOWN_DEAD_TRANSITION_IDS:
            dead_found.add(t["id"])

        row: dict[str, Any] = {
            "id": f"sdhq:transition:{t['id']}",
            "source": node_id(source_node["scene_key"]),
            "priority": priority,
            "effects": transition_effects,
            "terminal": bool(t["terminal"]),
            "triggers": ["ktrf:next"],
            "metadata": {
                "source_transition_index": transition_index,
                "source_transition_id": t["id"],
                "source_route": routes[source_node_index],
                "source_scene": scenes[source_node_index],
                "source_condition_start": t["condition_start"],
                "source_condition_count": t["condition_count"],
                "source_effect_start": t["effect_start"],
                "source_effect_count": t["effect_count"],
                "known_dead_normal_new_game": t["id"] in KNOWN_DEAD_TRANSITION_IDS,
                "profile_synthetic_coordinate_effects": synthetic_coordinate_effects,
            },
        }
        if predicate is not None:
            row["predicate"] = predicate
        if destination is not None:
            row["destination"] = destination
        transitions.append(row)

    if callback38_nonterminal != EXPECTED["callback38_nonterminal"]:
        raise RuntimeError(
            f"callback_38 nonterminal mismatch: {callback38_nonterminal} != {EXPECTED['callback38_nonterminal']}"
        )
    if callback38_terminal != EXPECTED["callback38_terminal"]:
        raise RuntimeError(
            f"callback_38 terminal mismatch: {callback38_terminal} != {EXPECTED['callback38_terminal']}"
        )
    if dead_found != KNOWN_DEAD_TRANSITION_IDS:
        raise RuntimeError(f"dead transition catalog mismatch: {sorted(dead_found)}")

    document = {
        "format": "ktrf-routing-ir",
        "ir_version": "0.1.0",
        "document_id": "overflow.school-days-hq.normal-new-game.routing",
        "profile": {
            "id": PROFILE_ID,
            "version": PROFILE_VERSION,
            "metadata": {
                "oracle_tag": ORACLE_TAG,
                "oracle_commit": ORACLE_COMMIT,
                "scope": "Normal New Game routing oracle",
            },
        },
        "features": {
            "required": [
                {"id": "ktrf:expression-core", "version": "1.0.0"},
                {"id": "ktrf:effects-core", "version": "1.0.0"},
                {"id": "ktrf:choices-deferred", "version": "1.0.0"},
                {"id": "ktrf:endings", "version": "1.0.0"},
                {"id": "ktrf:external-hooks", "version": "1.0.0"},
                {"id": "ktrf:resource-locators", "version": "1.0.0"},
                {"id": "overflow.sdhq:routing-profile", "version": "1.0.0"},
                {"id": "overflow.sdhq:scene-key-locator", "version": "1.0.0"},
            ],
            "optional": [],
        },
        "namespaces": [
            {"prefix": PROFILE_NAMESPACE, "uri": PROFILE_NAMESPACE_URI}
        ],
        "entry_points": [
            {
                "id": "sdhq:entry:new-game",
                "node": node_id("00/00-00-A00"),
                "trigger": "ktrf:new-game",
                "metadata": {"source_route": 0, "source_scene": 0},
            }
        ],
        "variables": variables,
        "resource_locators": resource_locators,
        "external_hooks": external_hooks,
        "endings": endings,
        "expressions": expressions,
        "effects": effects,
        "choices": choices,
        "nodes": nodes,
        "transitions": transitions,
        "metadata": {
            "source_oracle": {
                "tag": ORACLE_TAG,
                "commit": ORACLE_COMMIT,
                "generated_table": str(generated_path.as_posix()),
                "generated_table_sha256": sha256_file(generated_path),
            },
            "source_model_counts": {
                "routes": len(model.route_offsets) - 1,
                "nodes": len(model.nodes),
                "transitions": len(model.transitions),
                "conditions": len(model.conditions),
                "effects": len(model.effects),
                "feeling_deltas": len(model.feeling_deltas),
                "feeling_resolutions": len(model.feeling_resolutions),
                "choices": len(choices),
                "endings": len(endings),
                "callback38_nonterminal": callback38_nonterminal,
                "callback38_terminal": callback38_terminal,
            },
            "source_route_offsets": model.route_offsets,
            "known_dead_transition_ids": sorted(KNOWN_DEAD_TRANSITION_IDS),
            "lowering_policy": {
                "choice_routing": "deferred",
                "flag_or": "lossless core boolean lowering",
                "choice_result_reset": "explicit synthetic ktrf:set appended after source effects",
                "route_scene_mirror": "explicit synthetic ktrf:set effects appended after choice reset on nonterminal transitions",
                "callback_arguments": "none; current executable oracle preserves callback symbol only",
                "resource_resolution": "profile scene-key locator; physical path is adapter-owned",
            },
        },
        "extensions": [],
    }
    return document


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export frozen School Days HQ routing tables to KTRF Canonical Routing IR v0.1"
    )
    parser.add_argument(
        "--generated",
        type=Path,
        default=Path("src/SchoolDaysRouteData.generated.inc"),
        help="frozen generated router table",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/ktrf/school-days-hq.routing.json"),
        help="output Routing IR JSON",
    )
    args = parser.parse_args()

    generated = args.generated.resolve()
    output = args.output.resolve()
    if not generated.is_file():
        raise SystemExit(f"generated table not found: {generated}")

    model = parse_generated(generated)
    document = build_document(model, generated)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print("=== School Days HQ -> KTRF Routing IR ===")
    print(f"oracle={ORACLE_COMMIT}")
    print(f"source_sha256={document['metadata']['source_oracle']['generated_table_sha256']}")
    print(f"routes={document['metadata']['source_model_counts']['routes']}")
    print(f"nodes={len(document['nodes'])}")
    print(f"transitions={len(document['transitions'])}")
    print(f"choices={len(document['choices'])}")
    print(f"expressions={len(document['expressions'])}")
    print(f"effects={len(document['effects'])}")
    print(f"variables={len(document['variables'])}")
    print(f"resources={len(document['resource_locators'])}")
    print(f"hooks={len(document['external_hooks'])}")
    print(f"endings={len(document['endings'])}")
    print(f"output={output}")
    print("EXPORT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
