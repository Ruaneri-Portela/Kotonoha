#!/usr/bin/env python3
"""Native broader finite-state differential for School Days HQ.

Scenario generation is deliberately reused from diff_sdhq_state_matrix.py.
Only the KTRF execution backend changes: the semantic Python interpreter is
replaced with the production native .ktnroute reader/runtime/router.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Mapping, Sequence

EXPECTED_NODES = 1857
EXPECTED_TRANSITIONS = 2458
EXPECTED_CHOICES = 287
PENDING_CHOICE = -2
NULL_INDEX = 0xFFFFFFFF


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path.resolve())
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    return module


def load_json(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise RuntimeError(f"JSON root is not an object: {path}")
    return value


def records(document: Mapping[str, Any], key: str) -> list[Mapping[str, Any]]:
    value = document.get(key, [])
    if not isinstance(value, list):
        return []
    return [row for row in value if isinstance(row, Mapping)]


def canonical_rows(document: Mapping[str, Any], key: str) -> list[Mapping[str, Any]]:
    rows = records(document, key)
    for row in rows:
        if not isinstance(row.get("id"), str):
            raise RuntimeError(f"{key} record lacks stable id")
    return sorted(rows, key=lambda row: str(row["id"]).encode("utf-8"))


def metadata_string(row: Mapping[str, Any], key: str) -> str:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping):
        return ""
    value = meta.get(key)
    return value if isinstance(value, str) else ""


def build_native_payload(
    scenarios: Sequence[Any],
    selector: Any,
    node_index: Mapping[str, int],
    variable_index: Mapping[str, int],
) -> str:
    lines: list[str] = []
    for scenario in scenarios:
        if scenario.source_node not in node_index:
            raise RuntimeError(f"native node index missing {scenario.source_node}")
        lines.append("RESET")
        lines.append(f"ACTIVATE {node_index[scenario.source_node]}")
        lines.append(f"SET {variable_index[selector.route_var]} {scenario.route}")
        lines.append(f"SET {variable_index[selector.scene_var]} {scenario.scene}")

        for var_id, value in sorted(scenario.assignments.items()):
            if var_id in {selector.route_var, selector.scene_var}:
                continue
            if var_id not in variable_index:
                raise RuntimeError(f"native variable index missing {var_id}")
            lines.append(f"SET {variable_index[var_id]} {int(value)}")

        choice_value = int(
            scenario.assignments.get(
                selector.choice_var,
                selector.defaults.get(selector.choice_var, PENDING_CHOICE),
            )
        )
        if scenario.source_node in selector.choice_by_node and choice_value != PENDING_CHOICE:
            lines.append("MARK_CHOICES_COMMITTED")

        lines.append("RESOLVE")
    return "\n".join(lines) + "\n"


def run_native_batch(
    executable: Path,
    artifact: Path,
    scenarios: Sequence[Any],
    selector: Any,
    node_index: Mapping[str, int],
    variable_index: Mapping[str, int],
) -> list[dict[str, Any]]:
    completed = subprocess.run(
        [str(executable), str(artifact)],
        input=build_native_payload(scenarios, selector, node_index, variable_index),
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "native KTRF state-matrix trace failed\n"
            f"exit={completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    rows: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if not isinstance(row, dict):
            raise RuntimeError("native trace emitted non-object JSON")
        rows.append(row)
    if len(rows) != len(scenarios):
        raise RuntimeError(f"native row count mismatch: {len(rows)} != {len(scenarios)}")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Broader School Days oracle versus native KTRF state-matrix differential"
    )
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument("--native-exe", type=Path, required=True)
    parser.add_argument(
        "--matrix-module",
        type=Path,
        default=Path("tools/ktrf/diff_sdhq_state_matrix.py"),
    )
    parser.add_argument(
        "--targeted-module",
        type=Path,
        default=Path("tools/ktrf/diff_sdhq_targeted_transitions.py"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/ktrf/school-days-hq.native-state-matrix-diff.json"),
    )
    parser.add_argument("--max-combinations-per-node", type=int, default=250000)
    parser.add_argument("--max-pairwise-per-node", type=int, default=32)
    parser.add_argument("--batch-size", type=int, default=500)
    args = parser.parse_args()

    for path in (
        args.ir,
        args.artifact,
        args.oracle_exe,
        args.native_exe,
        args.matrix_module,
        args.targeted_module,
    ):
        if not path.resolve().exists():
            raise SystemExit(f"required path not found: {path}")
    if args.batch_size <= 0:
        raise SystemExit("--batch-size must be > 0")
    if args.max_pairwise_per_node < 0:
        raise SystemExit("--max-pairwise-per-node must be >= 0")

    document = load_json(args.ir.resolve())
    matrix = load_module("ktrf_native_matrix_source", args.matrix_module)
    targeted = load_module("ktrf_native_matrix_targeted", args.targeted_module)
    selector = targeted.Selector(document)

    scenarios, generation = matrix.build_scenarios(
        document,
        selector,
        targeted,
        args.max_combinations_per_node,
        args.max_pairwise_per_node,
    )
    expected_ids = [
        matrix.expected_transition(selector, scenario, targeted.source_transition_id)
        for scenario in scenarios
    ]

    nodes = canonical_rows(document, "nodes")
    variables = canonical_rows(document, "variables")
    transitions = canonical_rows(document, "transitions")
    hooks = canonical_rows(document, "external_hooks")
    endings = canonical_rows(document, "endings")
    choices = canonical_rows(document, "choices")

    if len(nodes) != EXPECTED_NODES:
        raise RuntimeError(f"Node inventory mismatch: {len(nodes)}")
    if len(transitions) != EXPECTED_TRANSITIONS:
        raise RuntimeError(f"Transition inventory mismatch: {len(transitions)}")
    if len(choices) != EXPECTED_CHOICES:
        raise RuntimeError(f"Choice inventory mismatch: {len(choices)}")

    node_index = {str(row["id"]): i for i, row in enumerate(nodes)}
    variable_index = {str(row["id"]): i for i, row in enumerate(variables)}

    source_transition_by_physical: list[int] = [
        targeted.source_transition_id(row) for row in transitions
    ]
    expected_transition_ids = set(source_transition_by_physical)

    session_vars: list[tuple[int, str]] = []
    global_vars: list[tuple[int, str]] = []
    for index, row in enumerate(variables):
        meta = row.get("metadata", {})
        if not isinstance(meta, Mapping):
            continue
        symbol = meta.get("source_symbol")
        storage = meta.get("source_storage")
        if not isinstance(symbol, str):
            continue
        if storage == "session":
            session_vars.append((index, symbol))
        elif storage == "global":
            global_vars.append((index, symbol))

    choice_index = variable_index[selector.choice_var]
    callback34_index = variable_index[selector.callback34_var]

    selected_ids: set[int] = set()
    covered_nodes: set[str] = set()
    pending_choice_checks = 0
    outcomes: Counter[str] = Counter()
    category_counts: Counter[str] = Counter()

    artifact = args.artifact.resolve()
    oracle_exe = args.oracle_exe.resolve()
    native_exe = args.native_exe.resolve()

    for batch_start in range(0, len(scenarios), args.batch_size):
        batch = scenarios[batch_start : batch_start + args.batch_size]
        batch_expected = expected_ids[batch_start : batch_start + len(batch)]
        oracle_rows = matrix.run_cpp_batch(
            oracle_exe, selector, batch, batch_expected
        )
        native_rows = run_native_batch(
            native_exe,
            artifact,
            batch,
            selector,
            node_index,
            variable_index,
        )

        for local_index, (scenario, expected_sid, oracle, native) in enumerate(
            zip(batch, batch_expected, oracle_rows, native_rows)
        ):
            scenario_index = batch_start + local_index

            def fail(field: str, expected: Any, actual: Any) -> None:
                raise AssertionError(
                    f"native state-matrix scenario {scenario_index} {scenario.source_node}: "
                    f"{field} divergence\n"
                    f"  oracle:      {expected!r}\n"
                    f"  native KTRF: {actual!r}\n"
                    f"  assignments: {scenario.assignments!r}\n"
                    f"  categories:  {sorted(scenario.categories)!r}"
                )

            covered_nodes.add(scenario.source_node)
            for category in scenario.categories:
                category_counts[category] += 1

            oracle_sid = int(oracle.get("transition", -999999))
            if oracle_sid != expected_sid:
                fail("oracle/selector transition", expected_sid, oracle_sid)

            physical_transition = int(native.get("transition_index", NULL_INDEX))
            if physical_transition != NULL_INDEX and not (0 <= physical_transition < len(transitions)):
                fail("physical transition index range", f"0..{len(transitions)-1} or NULL", physical_transition)
            native_sid = (
                -1
                if physical_transition == NULL_INDEX
                else source_transition_by_physical[physical_transition]
            )
            if native_sid != oracle_sid:
                fail("selected transition", oracle_sid, native_sid)
            if native_sid != -1:
                selected_ids.add(native_sid)

            expected_source_index = node_index[scenario.source_node]
            if int(native.get("source_node_index", NULL_INDEX)) != expected_source_index:
                fail(
                    "source node index",
                    expected_source_index,
                    native.get("source_node_index"),
                )

            oracle_kind = str(oracle.get("kind", ""))
            status = int(native.get("status", -1))
            if oracle_kind == "advanced":
                if status != 2:
                    fail("route status", 2, status)
                outcomes["advanced"] += 1
            elif oracle_kind == "terminal":
                if status != 3:
                    fail("route status", 3, status)
                outcomes["terminal"] += 1
            elif oracle_kind == "unresolved":
                if status not in (0, 1):
                    fail("route status unresolved", "0 or 1", status)
                outcomes["unresolved"] += 1
            else:
                fail("oracle kind", "advanced/terminal/unresolved", oracle_kind)

            is_pending_default = (
                "default-node" in scenario.categories
                and scenario.source_node in selector.choice_by_node
                and int(
                    scenario.assignments.get(
                        selector.choice_var,
                        selector.defaults.get(selector.choice_var, PENDING_CHOICE),
                    )
                )
                == PENDING_CHOICE
            )
            if is_pending_default:
                pending_choice_checks += 1
                if oracle_kind != "unresolved":
                    fail("pending choice oracle kind", "unresolved", oracle_kind)
                if status != 1:
                    fail("pending choice native status", 1, status)

            destination_index = int(native.get("destination_node_index", NULL_INDEX))
            if destination_index != NULL_INDEX and not (0 <= destination_index < len(nodes)):
                fail("destination node index range", f"0..{len(nodes)-1} or NULL", destination_index)
            native_destination = (
                ""
                if destination_index == NULL_INDEX
                else metadata_string(nodes[destination_index], "scene_key")
            )
            if native_destination != str(oracle.get("destination", "")):
                fail("destination SceneKey", oracle.get("destination", ""), native_destination)

            current_index = int(native.get("current_node_index", NULL_INDEX))
            if oracle_kind != "terminal":
                if not (0 <= current_index < len(nodes)):
                    fail("current node index range", f"0..{len(nodes)-1}", current_index)
                native_current = metadata_string(nodes[current_index], "scene_key")
                if native_current != str(oracle.get("current_scene", "")):
                    fail("current SceneKey", oracle.get("current_scene", ""), native_current)

            native_hooks: list[str] = []
            for index in native.get("hooks", []):
                physical = int(index)
                if not (0 <= physical < len(hooks)):
                    fail("hook index range", f"0..{len(hooks)-1}", physical)
                native_hooks.append(str(hooks[physical].get("symbol", "")))
            oracle_hooks = [
                f"overflow.sdhq:{name}" for name in oracle.get("callbacks", [])
            ]
            if native_hooks != oracle_hooks:
                fail("callbacks/hooks", oracle_hooks, native_hooks)

            native_ending_codes: list[int] = []
            for index in native.get("endings", []):
                physical = int(index)
                if not (0 <= physical < len(endings)):
                    fail("ending index range", f"0..{len(endings)-1}", physical)
                row = endings[physical]
                code = row.get("code")
                if not isinstance(code, int):
                    raise RuntimeError(f"ending {row.get('id')!r} has non-integer code")
                native_ending_codes.append(code)
            oracle_endings = [int(value) for value in oracle.get("endings", [])]
            if native_ending_codes != oracle_endings:
                fail("ending registrations", oracle_endings, native_ending_codes)

            values = native.get("variables")
            if not isinstance(values, list) or len(values) != len(variables):
                fail("VARS vector length", len(variables), len(values) if isinstance(values, list) else values)

            if int(values[choice_index]) != int(oracle.get("choice_result", 0)):
                fail("choice_result", oracle.get("choice_result"), values[choice_index])
            if int(values[callback34_index]) != int(oracle.get("callback34", 0)):
                fail("callback34", oracle.get("callback34"), values[callback34_index])

            oracle_session = oracle.get("session", {})
            oracle_global = oracle.get("global", {})
            if not isinstance(oracle_session, Mapping) or not isinstance(oracle_global, Mapping):
                raise RuntimeError("oracle emitted malformed variable maps")
            for index, symbol in session_vars:
                expected = int(oracle_session.get(symbol, 0))
                actual = int(values[index])
                if expected != actual:
                    fail(f"session {symbol}", expected, actual)
            for index, symbol in global_vars:
                expected = int(oracle_global.get(symbol, 0))
                actual = int(values[index])
                if expected != actual:
                    fail(f"global {symbol}", expected, actual)

    if len(covered_nodes) != EXPECTED_NODES:
        raise AssertionError(
            f"source Node coverage incomplete: {len(covered_nodes)} != {EXPECTED_NODES}"
        )
    if selected_ids != expected_transition_ids:
        missing = sorted(expected_transition_ids - selected_ids)
        extra = sorted(selected_ids - expected_transition_ids)
        raise AssertionError(
            f"selected Transition coverage incomplete: {len(selected_ids)} != "
            f"{EXPECTED_TRANSITIONS}; missing={missing[:50]} extra={extra[:50]}"
        )
    if pending_choice_checks != EXPECTED_CHOICES:
        raise AssertionError(
            f"pending Choice default coverage incomplete: {pending_choice_checks} != "
            f"{EXPECTED_CHOICES}"
        )

    report = {
        "format": "ktrf-native-state-matrix-differential-v0.1",
        "oracle_commit": getattr(matrix, "ORACLE_COMMIT", ""),
        "artifact": str(artifact),
        "generation": generation,
        "coverage": {
            "source_nodes": len(covered_nodes),
            "selected_transitions": len(selected_ids),
            "pending_choice_default_checks": pending_choice_checks,
            "choices": len(choices),
        },
        "outcomes": dict(sorted(outcomes.items())),
        "categories": dict(sorted(category_counts.items())),
        "selected_transition_ids": sorted(selected_ids),
        "divergences": 0,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print("")
    print("=== KTRF native broader state-matrix differential ===")
    print(f"scenarios={len(scenarios)}")
    print(f"source_nodes={len(covered_nodes)}/{EXPECTED_NODES}")
    print(f"selected_transitions={len(selected_ids)}/{EXPECTED_TRANSITIONS}")
    print(f"pending_choice_default_checks={pending_choice_checks}/{EXPECTED_CHOICES}")
    print(f"advanced={outcomes['advanced']}")
    print(f"terminal={outcomes['terminal']}")
    print(f"unresolved={outcomes['unresolved']}")
    print("divergences=0")
    print("NATIVE BROADER STATE MATRIX PASS")
    print(f"report={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
