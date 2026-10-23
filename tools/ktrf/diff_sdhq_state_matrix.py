#!/usr/bin/env python3
"""Broader finite state-matrix differential for School Days HQ.

This complements causal witnesses, targeted transition closure, Choice/Feeling
coverage and source-Effect stress.  It intentionally does not claim exhaustive
int32-state or historical reachability proof.  Instead it samples deterministic
boundary neighborhoods around every executable branch fixture while also
probing every Node in its coherent default state.
"""

from __future__ import annotations

import argparse
import importlib.util
import itertools
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping, Sequence


ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
EXPECTED_NODES = 1857
EXPECTED_TRANSITIONS = 2458
EXPECTED_CHOICES = 287
PENDING_CHOICE = -2


@dataclass
class Scenario:
    source_node: str
    route: int
    scene: int
    assignments: dict[str, int]
    categories: set[str] = field(default_factory=set)


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


def scenario_key(source_node: str, assignments: Mapping[str, int]) -> tuple[str, tuple[tuple[str, int], ...]]:
    return source_node, tuple(sorted((str(k), int(v)) for k, v in assignments.items()))


def add_scenario(
    store: dict[tuple[str, tuple[tuple[str, int], ...]], Scenario],
    source_node: str,
    route: int,
    scene: int,
    assignments: Mapping[str, int],
    category: str,
) -> None:
    normalized = {str(k): int(v) for k, v in assignments.items()}
    key = scenario_key(source_node, normalized)
    existing = store.get(key)
    if existing is None:
        store[key] = Scenario(source_node, route, scene, normalized, {category})
    else:
        existing.categories.add(category)


def build_scenarios(
    document: Mapping[str, Any],
    selector: Any,
    targeted: Any,
    max_combinations_per_node: int,
    max_pairwise_per_node: int,
) -> tuple[list[Scenario], dict[str, Any]]:
    nodes = selector.nodes
    transitions = records(document, "transitions")
    if len(nodes) != EXPECTED_NODES:
        raise RuntimeError(f"Node inventory mismatch: {len(nodes)} != {EXPECTED_NODES}")
    if len(transitions) != EXPECTED_TRANSITIONS:
        raise RuntimeError(
            f"Transition inventory mismatch: {len(transitions)} != {EXPECTED_TRANSITIONS}"
        )
    if len(selector.choices) != EXPECTED_CHOICES:
        raise RuntimeError(f"Choice inventory mismatch: {len(selector.choices)} != {EXPECTED_CHOICES}")

    store: dict[tuple[str, tuple[tuple[str, int], ...]], Scenario] = {}

    # A. Coherent reset/default state for every logical Node.  On Choice Nodes
    # this intentionally leaves choice_result at -2 and therefore probes the
    # pending-choice routing gate.
    for node_id in sorted(nodes):
        route, scene = targeted.node_coordinate(nodes[node_id])
        add_scenario(store, node_id, route, scene, {}, "default-node")

    # B. One known-satisfying fixture for every executable Transition.
    all_transition_ids = {
        targeted.source_transition_id(row) for row in transitions
    }
    base_fixtures, unresolved, combinations = targeted.generate_fixtures(
        selector,
        all_transition_ids,
        max_combinations_per_node,
    )
    if unresolved:
        raise RuntimeError(
            "all-transition fixture generation incomplete: "
            + ",".join(str(x) for x in unresolved[:100])
        )

    bases_by_source: dict[str, list[Any]] = {}
    for fixture in base_fixtures:
        bases_by_source.setdefault(fixture.source_node, []).append(fixture)
        add_scenario(
            store,
            fixture.source_node,
            fixture.route,
            fixture.scene,
            fixture.assignments,
            "transition-base",
        )

    # C. Single-variable boundary/domain perturbations around every base.
    boundary_generated = 0
    for source in sorted(bases_by_source):
        variable_ids, domains = selector.domains_for_source(source)
        domain_by_var = dict(zip(variable_ids, domains))
        for base in sorted(bases_by_source[source], key=lambda f: f.source_transition_id):
            effective = selector.make_values(source, base.assignments)
            for var_id in variable_ids:
                for value in domain_by_var[var_id]:
                    if effective.get(var_id) == value:
                        continue
                    assignments = dict(base.assignments)
                    assignments[var_id] = int(value)
                    add_scenario(
                        store,
                        source,
                        base.route,
                        base.scene,
                        assignments,
                        "single-boundary",
                    )
                    boundary_generated += 1

    # D. Deterministic limited pairwise combinations.  Use one satisfying anchor
    # per source Node so we broaden interactions without a Cartesian explosion.
    pairwise_generated = 0
    pairwise_sources = 0
    if max_pairwise_per_node > 0:
        for source in sorted(bases_by_source):
            variable_ids, domains = selector.domains_for_source(source)
            if len(variable_ids) < 2:
                continue
            pairwise_sources += 1
            anchor = min(bases_by_source[source], key=lambda f: f.source_transition_id)
            emitted = 0
            stop = False
            for i in range(len(variable_ids)):
                for j in range(i + 1, len(variable_ids)):
                    left = variable_ids[i]
                    right = variable_ids[j]
                    for lv, rv in itertools.product(domains[i], domains[j]):
                        assignments = dict(anchor.assignments)
                        assignments[left] = int(lv)
                        assignments[right] = int(rv)
                        add_scenario(
                            store,
                            source,
                            anchor.route,
                            anchor.scene,
                            assignments,
                            "pairwise-boundary",
                        )
                        emitted += 1
                        pairwise_generated += 1
                        if emitted >= max_pairwise_per_node:
                            stop = True
                            break
                    if stop:
                        break
                if stop:
                    break

    scenarios = sorted(
        store.values(),
        key=lambda s: (s.route, s.scene, s.source_node, tuple(sorted(s.assignments.items()))),
    )
    generation = {
        "solver_combinations": combinations,
        "base_fixtures": len(base_fixtures),
        "single_boundary_candidates_generated": boundary_generated,
        "pairwise_candidates_generated": pairwise_generated,
        "pairwise_sources": pairwise_sources,
        "max_pairwise_per_node": max_pairwise_per_node,
        "unique_scenarios": len(scenarios),
    }
    return scenarios, generation


def expected_transition(selector: Any, scenario: Scenario, source_transition_id) -> int:
    values = selector.make_values(scenario.source_node, scenario.assignments)
    selected = selector.select(scenario.source_node, values)
    return -1 if selected is None else source_transition_id(selected)


def trace_payload(
    selector: Any,
    scenarios: Sequence[Scenario],
    expected_ids: Sequence[int],
) -> str:
    lines: list[str] = []
    for scenario, expected_sid in zip(scenarios, expected_ids):
        choice = int(
            scenario.assignments.get(
                selector.choice_var,
                selector.defaults.get(selector.choice_var, PENDING_CHOICE),
            )
        )
        callback34 = int(
            scenario.assignments.get(
                selector.callback34_var,
                selector.defaults.get(selector.callback34_var, 0),
            )
        )
        feeling_applied = 1 if scenario.source_node in selector.choice_by_node and choice != PENDING_CHOICE else 0
        lines.append("RESET")
        lines.append(
            f"INJECT {scenario.route} {scenario.scene} {choice} {callback34} {feeling_applied}"
        )
        for var_id, value in sorted(scenario.assignments.items()):
            if var_id in {
                selector.choice_var,
                selector.callback34_var,
                selector.route_var,
                selector.scene_var,
            }:
                continue
            source = selector.variable_source.get(var_id)
            if source is None:
                raise RuntimeError(
                    f"cannot inject Variable without School Days source metadata: {var_id}"
                )
            storage, symbol = source
            if storage == "session":
                lines.append(f"SESSION {symbol} {value}")
            elif storage == "global":
                lines.append(f"GLOBAL {symbol} {value}")
            else:
                raise RuntimeError(f"unsupported source storage {storage!r} for {var_id}")
        # The existing trace protocol accepts -1: unresolved ResolveNext returns
        # transitionId=-1, so no alternate oracle entry point is required.
        lines.append(f"RESOLVE {expected_sid}")
    return "\n".join(lines) + "\n"


def run_cpp_batch(
    executable: Path,
    selector: Any,
    scenarios: Sequence[Scenario],
    expected_ids: Sequence[int],
) -> list[dict[str, Any]]:
    completed = subprocess.run(
        [str(executable)],
        input=trace_payload(selector, scenarios, expected_ids),
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "compiled C++ state-matrix trace failed\n"
            f"exit={completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    rows: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if not isinstance(row, dict):
            raise RuntimeError("C++ state-matrix trace emitted non-object JSON")
        rows.append(row)
    if len(rows) != len(scenarios):
        raise RuntimeError(f"C++ row count mismatch: {len(rows)} != {len(scenarios)}")
    return rows


def inject_ir(vm: Any, selector: Any, scenario: Scenario) -> None:
    vm.reset("sdhq:entry:new-game")
    vm.current_node = scenario.source_node
    vm.terminal = False
    vm.ending_registrations = []
    vm.hook_history = []
    vm._committed_choices = {}
    vm.write_variable(selector.route_var, scenario.route)
    vm.write_variable(selector.scene_var, scenario.scene)
    for var_id, value in scenario.assignments.items():
        vm.write_variable(var_id, value)


def compare_state_maps(
    scenario: Scenario,
    cpp: Mapping[str, Any],
    vm: Any,
    diff: Any,
    fail,
) -> None:
    cpp_session = cpp.get("session", {})
    cpp_global = cpp.get("global", {})
    if not isinstance(cpp_session, Mapping) or not isinstance(cpp_global, Mapping):
        raise AssertionError("malformed C++ state maps")
    for var_id, symbol in diff.session_vars:
        expected = int(cpp_session.get(symbol, 0))
        actual = int(vm.read_variable(var_id))
        if expected != actual:
            fail(f"session {symbol}", expected, actual)
    for var_id, symbol in diff.global_vars:
        expected = int(cpp_global.get(symbol, 0))
        actual = int(vm.read_variable(var_id))
        if expected != actual:
            fail(f"global {symbol}", expected, actual)


def compare_scenario(
    scenario_index: int,
    scenario: Scenario,
    expected_sid: int,
    cpp: Mapping[str, Any],
    document: Mapping[str, Any],
    selector: Any,
    Interpreter: Any,
    Differential: Any,
) -> str:
    diff = Differential(document, Interpreter)
    vm = Interpreter(document)
    inject_ir(vm, selector, scenario)
    result = vm.trigger("ktrf:next")

    def fail(field: str, expected: Any, actual: Any) -> None:
        raise AssertionError(
            f"state-matrix scenario {scenario_index} {scenario.source_node}: {field} divergence\n"
            f"  expected/C++: {expected!r}\n"
            f"  KTRF IR:      {actual!r}\n"
            f"  assignments:  {scenario.assignments!r}\n"
            f"  categories:   {sorted(scenario.categories)!r}"
        )

    cpp_transition = int(cpp.get("transition", -999999))
    if cpp_transition != expected_sid:
        fail("C++ selected transition", expected_sid, cpp_transition)

    if expected_sid == -1:
        if result is not None:
            fail("unresolved result", None, result.transition_id)
        if cpp.get("kind") != "unresolved":
            fail("kind", "unresolved", cpp.get("kind"))
        if str(cpp.get("destination", "")) != "":
            fail("destination", "", cpp.get("destination"))
        if int(cpp.get("ending_id", -1)) != -1:
            fail("ending_id", -1, cpp.get("ending_id"))
        if list(cpp.get("callbacks", [])):
            fail("callbacks", [], cpp.get("callbacks"))
        if (int(cpp.get("route", -1)), int(cpp.get("scene", -1))) != (scenario.route, scenario.scene):
            fail(
                "retained ROUTE/SCENE",
                (scenario.route, scenario.scene),
                (cpp.get("route"), cpp.get("scene")),
            )
        expected_scene = diff.node_scene_key(scenario.source_node)
        if str(cpp.get("current_scene", "")) != expected_scene:
            fail("retained SceneKey", expected_scene, cpp.get("current_scene"))
        if diff.profile_coordinate(vm) != (scenario.route, scenario.scene):
            fail("IR retained ROUTE/SCENE", (scenario.route, scenario.scene), diff.profile_coordinate(vm))
        if vm.current_node != scenario.source_node:
            fail("IR retained Node", scenario.source_node, vm.current_node)
        if vm.ending_registrations:
            fail("IR endings", [], vm.ending_registrations)
        if vm.hook_history:
            fail("IR hooks", [], vm.hook_history)
        compare_state_maps(scenario, cpp, vm, diff, fail)
        return "unresolved"

    if result is None:
        fail("resolved result", expected_sid, None)
    assert result is not None
    actual_sid = diff.source_transition_id(result)
    if actual_sid != expected_sid:
        fail("transition", expected_sid, actual_sid)

    actual_kind = "terminal" if result.terminal else "advanced"
    if cpp.get("kind") != actual_kind:
        fail("kind", cpp.get("kind"), actual_kind)

    cpp_coord = (int(cpp.get("route", -1)), int(cpp.get("scene", -1)))
    ir_coord = diff.profile_coordinate(vm)
    if cpp_coord != ir_coord:
        fail("post ROUTE/SCENE", cpp_coord, ir_coord)

    if not result.terminal:
        node_coord = diff.node_coordinate(vm.current_node)
        if node_coord != cpp_coord:
            fail("post Node coordinate", cpp_coord, node_coord)
        if str(cpp.get("current_scene", "")) != diff.node_scene_key(vm.current_node):
            fail("current SceneKey", cpp.get("current_scene"), diff.node_scene_key(vm.current_node))

    if str(cpp.get("destination", "")) != diff.node_scene_key(result.destination):
        fail("destination", cpp.get("destination"), diff.node_scene_key(result.destination))

    cpp_hooks = [f"overflow.sdhq:{name}" for name in cpp.get("callbacks", [])]
    ir_hooks = [call.symbol for call in result.hook_calls]
    if cpp_hooks != ir_hooks:
        fail("callbacks/hooks", cpp_hooks, ir_hooks)

    cpp_endings = [int(x) for x in cpp.get("endings", [])]
    ir_endings = diff.ending_codes(vm)
    if cpp_endings != ir_endings:
        fail("ending registrations", cpp_endings, ir_endings)

    new_codes: list[int] = []
    for ending_id in result.registered_endings:
        ending = diff.endings.get(ending_id)
        if ending is None or not isinstance(ending.get("code"), int):
            raise AssertionError(f"malformed registered Ending {ending_id!r}")
        new_codes.append(int(ending["code"]))
    ir_ending_id = new_codes[-1] if new_codes else -1
    if int(cpp.get("ending_id", -1)) != ir_ending_id:
        fail("newly registered ending", cpp.get("ending_id"), ir_ending_id)

    if int(cpp.get("choice_result", 0)) != int(vm.read_variable(selector.choice_var)):
        fail("choice_result", cpp.get("choice_result"), vm.read_variable(selector.choice_var))
    if int(cpp.get("callback34", 0)) != int(vm.read_variable(selector.callback34_var)):
        fail("callback34", cpp.get("callback34"), vm.read_variable(selector.callback34_var))

    compare_state_maps(scenario, cpp, vm, diff, fail)
    return actual_kind


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Broader deterministic School Days C++ versus KTRF state-matrix differential"
    )
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, default=Path("tools/ktrf/interpreter.py"))
    parser.add_argument("--targeted-module", type=Path, default=Path("tools/ktrf/diff_sdhq_targeted_transitions.py"))
    parser.add_argument("--diff-module", type=Path, default=Path("tools/ktrf/diff_sdhq_witnesses.py"))
    parser.add_argument("--output", type=Path, default=Path("build/ktrf/school-days-hq.state-matrix-diff.json"))
    parser.add_argument("--max-combinations-per-node", type=int, default=250000)
    parser.add_argument("--max-pairwise-per-node", type=int, default=32)
    parser.add_argument("--batch-size", type=int, default=500)
    args = parser.parse_args()

    paths = (args.ir, args.oracle_exe, args.interpreter, args.targeted_module, args.diff_module)
    for path in paths:
        if not path.resolve().exists():
            raise SystemExit(f"required path not found: {path.resolve()}")
    if args.batch_size <= 0:
        raise SystemExit("--batch-size must be > 0")
    if args.max_pairwise_per_node < 0:
        raise SystemExit("--max-pairwise-per-node must be >= 0")

    document = load_json(args.ir.resolve())
    targeted = load_module("ktrf_state_matrix_targeted", args.targeted_module)
    diff_module = load_module("ktrf_state_matrix_common", args.diff_module)
    interpreter_module = load_module("ktrf_state_matrix_interpreter", args.interpreter)

    selector = targeted.Selector(document)
    Interpreter = interpreter_module.Interpreter
    Differential = diff_module.Differential

    scenarios, generation = build_scenarios(
        document,
        selector,
        targeted,
        args.max_combinations_per_node,
        args.max_pairwise_per_node,
    )
    expected_ids = [
        expected_transition(selector, s, targeted.source_transition_id) for s in scenarios
    ]

    unique_nodes = {s.source_node for s in scenarios}
    selected_ids = {sid for sid in expected_ids if sid >= 0}
    if len(unique_nodes) != EXPECTED_NODES:
        raise SystemExit(f"state-matrix Node coverage mismatch: {len(unique_nodes)} != {EXPECTED_NODES}")
    if len(selected_ids) != EXPECTED_TRANSITIONS:
        missing = sorted(
            {targeted.source_transition_id(row) for row in records(document, "transitions")} - selected_ids
        )
        raise SystemExit(
            f"state-matrix Transition coverage mismatch: {len(selected_ids)} != {EXPECTED_TRANSITIONS}; first missing={missing[:20]}"
        )

    # Every Choice Node has a default-node scenario whose pending result must
    # remain unresolved in both runtimes.
    choice_nodes = set(selector.choice_by_node)
    default_choice_pending = 0
    for scenario, sid in zip(scenarios, expected_ids):
        if "default-node" not in scenario.categories or scenario.source_node not in choice_nodes:
            continue
        choice = scenario.assignments.get(selector.choice_var, selector.defaults.get(selector.choice_var))
        if choice == PENDING_CHOICE and sid == -1:
            default_choice_pending += 1
    if default_choice_pending != EXPECTED_CHOICES:
        raise SystemExit(
            f"pending Choice gate coverage mismatch: {default_choice_pending} != {EXPECTED_CHOICES}"
        )

    category_membership: Counter[str] = Counter()
    for scenario in scenarios:
        for category in scenario.categories:
            category_membership[category] += 1

    outcome_counts: Counter[str] = Counter()
    compared = 0
    executable = args.oracle_exe.resolve()
    for start in range(0, len(scenarios), args.batch_size):
        chunk = scenarios[start : start + args.batch_size]
        chunk_expected = expected_ids[start : start + args.batch_size]
        cpp_rows = run_cpp_batch(executable, selector, chunk, chunk_expected)
        for offset, (scenario, expected_sid, cpp) in enumerate(zip(chunk, chunk_expected, cpp_rows)):
            outcome = compare_scenario(
                start + offset,
                scenario,
                expected_sid,
                cpp,
                document,
                selector,
                Interpreter,
                Differential,
            )
            outcome_counts[outcome] += 1
            compared += 1

    report = {
        "format": "ktrf-sdhq-state-matrix-differential-v0.1",
        "oracle_commit": ORACLE_COMMIT,
        "generation": generation,
        "coverage": {
            "source_nodes": {"covered": len(unique_nodes), "total": EXPECTED_NODES},
            "selected_transitions": {"covered": len(selected_ids), "total": EXPECTED_TRANSITIONS},
            "choice_pending_default_checks": {"covered": default_choice_pending, "total": EXPECTED_CHOICES},
            "category_membership": dict(sorted(category_membership.items())),
        },
        "outcomes": dict(sorted(outcome_counts.items())),
        "differential": {"compared": compared, "divergences": 0},
        "selected_transition_ids": sorted(selected_ids),
        "status": "PASS",
    }

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print("=== KTRF / School Days broader state-matrix differential ===")
    print(f"nodes={len(unique_nodes)}/{EXPECTED_NODES}")
    print(f"selected_transitions={len(selected_ids)}/{EXPECTED_TRANSITIONS}")
    print(f"choice_pending_default={default_choice_pending}/{EXPECTED_CHOICES}")
    print(f"base_fixtures={generation['base_fixtures']}")
    print(f"solver_combinations={generation['solver_combinations']}")
    print(f"unique_scenarios={len(scenarios)}")
    print("category_membership=" + json.dumps(dict(sorted(category_membership.items())), sort_keys=True))
    print("outcomes=" + json.dumps(dict(sorted(outcome_counts.items())), sort_keys=True))
    print(f"differential={compared}/{len(scenarios)}")
    print("divergences=0")
    print(f"output={output}")
    print("BROADER STATE MATRIX DIFFERENTIAL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
