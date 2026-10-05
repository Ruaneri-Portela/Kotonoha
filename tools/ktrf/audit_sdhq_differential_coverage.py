#!/usr/bin/env python3
"""Audit coverage of the School Days 22-ending differential corpus.

This tool does not execute the router. It measures how much of the frozen
2,458-transition KTRF School Days executable model is exercised by the
already-certified 22 ending witnesses used by diff_sdhq_witnesses.py.

The goal is to make the remaining differential gap explicit before building
broader state-injection/exhaustive probes.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any, Mapping


EXPECTED_TRANSITIONS = 2458
EXPECTED_NODES = 1857
EXPECTED_CHOICES = 287
EXPECTED_TERMINALS = 23
EXPECTED_CALLBACK38_NONTERMINAL = 47
EXPECTED_CALLBACK38_TERMINAL = 23
ROUTING_ONLY_SCENES = {"03/03-B2-A00", "03/03-KB-E00"}


def parse_witness_steps(path: Path) -> tuple[int, list[tuple[int, int, int, int]]]:
    text = path.read_text(encoding="utf-8")
    array_re = re.compile(
        r"static\s+const\s+Step\s+(kEnding\d+)\[\]\s*=\s*\{(.*?)\n\};",
        re.S,
    )
    step_re = re.compile(
        r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}"
    )
    arrays = list(array_re.finditer(text))
    if len(arrays) != 22:
        raise RuntimeError(f"expected 22 witness arrays, found {len(arrays)}")

    steps: list[tuple[int, int, int, int]] = []
    for array in arrays:
        rows = [tuple(int(g) for g in m.groups()) for m in step_re.finditer(array.group(2))]
        if not rows:
            raise RuntimeError(f"witness {array.group(1)} has no steps")
        steps.extend(rows)
    return len(arrays), steps


def index_by_id(document: Mapping[str, Any], collection: str) -> dict[str, Mapping[str, Any]]:
    out: dict[str, Mapping[str, Any]] = {}
    rows = document.get(collection, [])
    if not isinstance(rows, list):
        return out
    for row in rows:
        if isinstance(row, Mapping) and isinstance(row.get("id"), str):
            out[row["id"]] = row
    return out


def source_transition_id(row: Mapping[str, Any]) -> int:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping) or not isinstance(meta.get("source_transition_id"), int):
        raise RuntimeError(f"transition {row.get('id')!r} lacks source_transition_id")
    return int(meta["source_transition_id"])


def percent(part: int, whole: int) -> float:
    return 0.0 if whole == 0 else round(part * 100.0 / whole, 3)


def build_report(document: Mapping[str, Any], witness_source: Path) -> dict[str, Any]:
    witnesses, steps = parse_witness_steps(witness_source)

    transitions = document.get("transitions", [])
    nodes = document.get("nodes", [])
    choices = document.get("choices", [])
    if not isinstance(transitions, list) or not isinstance(nodes, list) or not isinstance(choices, list):
        raise RuntimeError("IR collections are malformed")
    if len(transitions) != EXPECTED_TRANSITIONS:
        raise RuntimeError(f"transition inventory mismatch: {len(transitions)} != {EXPECTED_TRANSITIONS}")
    if len(nodes) != EXPECTED_NODES:
        raise RuntimeError(f"node inventory mismatch: {len(nodes)} != {EXPECTED_NODES}")
    if len(choices) != EXPECTED_CHOICES:
        raise RuntimeError(f"choice inventory mismatch: {len(choices)} != {EXPECTED_CHOICES}")

    transition_by_source_id: dict[int, Mapping[str, Any]] = {}
    for row in transitions:
        if not isinstance(row, Mapping):
            continue
        sid = source_transition_id(row)
        if sid in transition_by_source_id:
            raise RuntimeError(f"duplicate source transition ID {sid}")
        transition_by_source_id[sid] = row

    witness_transition_ids = {transition for _, _, _, transition in steps}
    unknown = sorted(witness_transition_ids - set(transition_by_source_id))
    if unknown:
        raise RuntimeError(f"witness references transition IDs not in executable IR: {unknown[:20]}")

    covered_rows = [transition_by_source_id[sid] for sid in sorted(witness_transition_ids)]
    covered_sources = {str(row.get("source")) for row in covered_rows}

    coord_to_node: dict[tuple[int, int], str] = {}
    routing_only_ids: set[str] = set()
    for row in nodes:
        if not isinstance(row, Mapping) or not isinstance(row.get("id"), str):
            continue
        meta = row.get("metadata", {})
        if not isinstance(meta, Mapping):
            continue
        route = meta.get("route")
        scene = meta.get("scene")
        scene_key = meta.get("scene_key")
        if isinstance(route, int) and isinstance(scene, int):
            coord_to_node[(route, scene)] = row["id"]
        if scene_key in ROUTING_ONLY_SCENES:
            routing_only_ids.add(row["id"])

    witness_node_ids: set[str] = set()
    choice_node_ids: set[str] = set()
    for route, scene, choice, _ in steps:
        node_id = coord_to_node.get((route, scene))
        if node_id is None:
            raise RuntimeError(f"witness coordinate has no IR Node: {route}/{scene}")
        witness_node_ids.add(node_id)
        if choice != -99:
            choice_node_ids.add(node_id)

    effects = index_by_id(document, "effects")
    hooks = index_by_id(document, "external_hooks")
    callback38_hook_ids = {
        hook_id
        for hook_id, hook in hooks.items()
        if hook.get("symbol") == "overflow.sdhq:callback_38"
    }
    if len(callback38_hook_ids) != 1:
        raise RuntimeError("expected exactly one callback_38 hook")
    callback38_hook_id = next(iter(callback38_hook_ids))

    callback38_effect_ids = {
        effect_id
        for effect_id, effect in effects.items()
        if effect.get("op") == "ktrf:call-hook"
        and isinstance(effect.get("args"), Mapping)
        and effect["args"].get("hook") == callback38_hook_id
    }

    terminal_ids: set[int] = set()
    callback38_terminal_ids: set[int] = set()
    callback38_nonterminal_ids: set[int] = set()
    ending_registration_transition_ids: set[int] = set()

    for row in transitions:
        if not isinstance(row, Mapping):
            continue
        sid = source_transition_id(row)
        refs = row.get("effects", [])
        refs = refs if isinstance(refs, list) else []
        if row.get("terminal") is True:
            terminal_ids.add(sid)
        if any(ref in callback38_effect_ids for ref in refs):
            if row.get("terminal") is True:
                callback38_terminal_ids.add(sid)
            else:
                callback38_nonterminal_ids.add(sid)
        if any(
            isinstance(ref, str)
            and ref in effects
            and effects[ref].get("op") == "ktrf:register-ending"
            for ref in refs
        ):
            ending_registration_transition_ids.add(sid)

    if len(terminal_ids) != EXPECTED_TERMINALS:
        raise RuntimeError(f"terminal inventory mismatch: {len(terminal_ids)} != {EXPECTED_TERMINALS}")
    if len(callback38_nonterminal_ids) != EXPECTED_CALLBACK38_NONTERMINAL:
        raise RuntimeError(
            f"callback38 nonterminal inventory mismatch: {len(callback38_nonterminal_ids)} != {EXPECTED_CALLBACK38_NONTERMINAL}"
        )
    if len(callback38_terminal_ids) != EXPECTED_CALLBACK38_TERMINAL:
        raise RuntimeError(
            f"callback38 terminal inventory mismatch: {len(callback38_terminal_ids)} != {EXPECTED_CALLBACK38_TERMINAL}"
        )

    covered_terminal = witness_transition_ids & terminal_ids
    covered_cb38_terminal = witness_transition_ids & callback38_terminal_ids
    covered_cb38_nonterminal = witness_transition_ids & callback38_nonterminal_ids
    covered_ending_registration = witness_transition_ids & ending_registration_transition_ids

    all_transition_ids = set(transition_by_source_id)
    all_node_ids = {
        str(row.get("id")) for row in nodes if isinstance(row, Mapping) and isinstance(row.get("id"), str)
    }

    report = {
        "format": "ktrf-sdhq-differential-coverage-v0.1",
        "oracle_commit": "614461c2b14951ba117b9d2dedb4983cfa8ae8e6",
        "corpus": {
            "witnesses": witnesses,
            "steps": len(steps),
            "unique_transition_ids": len(witness_transition_ids),
            "unique_source_nodes": len(covered_sources),
            "unique_witness_nodes": len(witness_node_ids),
            "unique_choice_nodes_exercised": len(choice_node_ids),
        },
        "coverage": {
            "transitions": {
                "covered": len(witness_transition_ids),
                "total": len(all_transition_ids),
                "percent": percent(len(witness_transition_ids), len(all_transition_ids)),
            },
            "source_nodes": {
                "covered": len(covered_sources),
                "total": len(all_node_ids),
                "percent": percent(len(covered_sources), len(all_node_ids)),
            },
            "choice_nodes_exercised": {
                "covered": len(choice_node_ids),
                "total": len(choices),
                "percent": percent(len(choice_node_ids), len(choices)),
            },
            "terminal_transitions": {
                "covered": len(covered_terminal),
                "total": len(terminal_ids),
                "percent": percent(len(covered_terminal), len(terminal_ids)),
            },
            "callback38_nonterminal": {
                "covered": len(covered_cb38_nonterminal),
                "total": len(callback38_nonterminal_ids),
                "percent": percent(len(covered_cb38_nonterminal), len(callback38_nonterminal_ids)),
            },
            "callback38_terminal": {
                "covered": len(covered_cb38_terminal),
                "total": len(callback38_terminal_ids),
                "percent": percent(len(covered_cb38_terminal), len(callback38_terminal_ids)),
            },
            "ending_registration_transitions": {
                "covered": len(covered_ending_registration),
                "total": len(ending_registration_transition_ids),
                "percent": percent(len(covered_ending_registration), len(ending_registration_transition_ids)),
            },
            "routing_only_nodes": {
                "covered": len(witness_node_ids & routing_only_ids),
                "total": len(routing_only_ids),
                "covered_ids": sorted(witness_node_ids & routing_only_ids),
            },
        },
        "gaps": {
            "transition_ids": sorted(all_transition_ids - witness_transition_ids),
            "source_node_ids": sorted(all_node_ids - covered_sources),
            "choice_node_ids_not_exercised": sorted(
                {
                    str(row.get("node"))
                    for row in choices
                    if isinstance(row, Mapping) and isinstance(row.get("node"), str)
                }
                - choice_node_ids
            ),
            "terminal_transition_ids": sorted(terminal_ids - witness_transition_ids),
            "callback38_nonterminal_ids": sorted(callback38_nonterminal_ids - witness_transition_ids),
            "callback38_terminal_ids": sorted(callback38_terminal_ids - witness_transition_ids),
            "ending_registration_transition_ids": sorted(
                ending_registration_transition_ids - witness_transition_ids
            ),
        },
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit coverage of the School Days KTRF differential witnesses")
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument(
        "--witness-source",
        type=Path,
        default=Path("tests/SchoolDaysFullRouterTest.cpp"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/ktrf/school-days-hq.differential-coverage.json"),
    )
    args = parser.parse_args()

    document = json.loads(args.ir.resolve().read_text(encoding="utf-8-sig"))
    if not isinstance(document, Mapping):
        raise SystemExit("IR root must be an object")

    report = build_report(document, args.witness_source.resolve())
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

    cov = report["coverage"]
    corpus = report["corpus"]
    print("=== KTRF / School Days witness differential coverage ===")
    print(f"witnesses={corpus['witnesses']}")
    print(f"steps={corpus['steps']}")
    print(
        f"transitions={cov['transitions']['covered']}/{cov['transitions']['total']} "
        f"({cov['transitions']['percent']}%)"
    )
    print(
        f"source_nodes={cov['source_nodes']['covered']}/{cov['source_nodes']['total']} "
        f"({cov['source_nodes']['percent']}%)"
    )
    print(
        f"choice_nodes={cov['choice_nodes_exercised']['covered']}/{cov['choice_nodes_exercised']['total']} "
        f"({cov['choice_nodes_exercised']['percent']}%)"
    )
    print(
        f"terminal_transitions={cov['terminal_transitions']['covered']}/{cov['terminal_transitions']['total']} "
        f"({cov['terminal_transitions']['percent']}%)"
    )
    print(
        f"callback38_nonterminal={cov['callback38_nonterminal']['covered']}/{cov['callback38_nonterminal']['total']} "
        f"({cov['callback38_nonterminal']['percent']}%)"
    )
    print(
        f"callback38_terminal={cov['callback38_terminal']['covered']}/{cov['callback38_terminal']['total']} "
        f"({cov['callback38_terminal']['percent']}%)"
    )
    print(f"routing_only_nodes={cov['routing_only_nodes']['covered']}/{cov['routing_only_nodes']['total']}")
    print(f"output={output}")
    print("COVERAGE AUDIT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
