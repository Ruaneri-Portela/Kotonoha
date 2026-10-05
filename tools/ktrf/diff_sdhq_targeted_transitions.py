#!/usr/bin/env python3
"""Targeted state-injection differential for uncovered School Days transitions.

This layer complements the 22 causal ending witnesses.  It does NOT claim that
its injected pre-states are historical gameplay snapshots.  Instead it finds a
small satisfying routing state for each transition missing from witness
coverage, injects the same logical state into:

1. the compiled frozen SchoolDaysRouter ResolveNext implementation; and
2. the KTRF Canonical Routing IR reference interpreter;

then compares the selected transition and complete observable post-state.

The goal is transition-semantic closure without confusing targeted state
injection with causal reachability proof.
"""

from __future__ import annotations

import argparse
import importlib.util
import itertools
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence


ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
PENDING_CHOICE = -2
INT32_MIN = -(2**31)
INT32_MAX = 2**31 - 1


@dataclass(frozen=True)
class Fixture:
    source_transition_id: int
    transition_id: str
    source_node: str
    route: int
    scene: int
    assignments: dict[str, int]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
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


def records(document: Mapping[str, Any], key: str) -> list[Mapping[str, Any]]:
    value = document.get(key, [])
    if not isinstance(value, list):
        return []
    return [row for row in value if isinstance(row, Mapping)]


def index_by_id(document: Mapping[str, Any], key: str) -> dict[str, Mapping[str, Any]]:
    out: dict[str, Mapping[str, Any]] = {}
    for row in records(document, key):
        row_id = row.get("id")
        if isinstance(row_id, str):
            out[row_id] = row
    return out


def source_transition_id(row: Mapping[str, Any]) -> int:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping) or not isinstance(meta.get("source_transition_id"), int):
        raise RuntimeError(f"transition {row.get('id')!r} lacks source_transition_id")
    return int(meta["source_transition_id"])


def node_coordinate(row: Mapping[str, Any]) -> tuple[int, int]:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping):
        raise RuntimeError(f"node {row.get('id')!r} lacks metadata")
    route = meta.get("route")
    scene = meta.get("scene")
    if not isinstance(route, int) or not isinstance(scene, int):
        raise RuntimeError(f"node {row.get('id')!r} lacks route/scene coordinate")
    return route, scene


def safe_neighbor(value: int, delta: int) -> int:
    result = value + delta
    return max(INT32_MIN, min(INT32_MAX, result))


class Selector:
    def __init__(self, document: Mapping[str, Any]):
        self.document = document
        self.expressions = index_by_id(document, "expressions")
        self.variables = index_by_id(document, "variables")
        self.nodes = index_by_id(document, "nodes")
        self.choices = records(document, "choices")
        self.choice_by_node: dict[str, list[Mapping[str, Any]]] = {}
        for row in self.choices:
            node = row.get("node")
            if isinstance(node, str):
                self.choice_by_node.setdefault(node, []).append(row)

        self.transitions_by_source: dict[str, list[Mapping[str, Any]]] = {}
        for row in records(document, "transitions"):
            source = row.get("source")
            if isinstance(source, str):
                self.transitions_by_source.setdefault(source, []).append(row)
        for rows in self.transitions_by_source.values():
            rows.sort(key=lambda r: (int(r.get("priority", 0)), str(r.get("id", ""))))

        self.defaults: dict[str, Any] = {
            var_id: row.get("default") for var_id, row in self.variables.items()
        }
        self.session_by_symbol: dict[str, str] = {}
        self.variable_source: dict[str, tuple[str, str]] = {}
        for var_id, row in self.variables.items():
            meta = row.get("metadata", {})
            if not isinstance(meta, Mapping):
                continue
            symbol = meta.get("source_symbol")
            storage = meta.get("source_storage")
            if isinstance(symbol, str) and isinstance(storage, str):
                self.variable_source[var_id] = (storage, symbol)
                if storage == "session":
                    self.session_by_symbol[symbol] = var_id

        self.choice_var = "sdhq:var:internal:choice_result"
        self.callback34_var = "sdhq:var:internal:callback34"
        if self.choice_var not in self.variables or self.callback34_var not in self.variables:
            raise RuntimeError("School Days internal routing Variables are missing")
        for symbol in ("ROUTE", "SCENE"):
            if symbol not in self.session_by_symbol:
                raise RuntimeError(f"School Days IR lacks session Variable {symbol}")
        self.route_var = self.session_by_symbol["ROUTE"]
        self.scene_var = self.session_by_symbol["SCENE"]

    def eval_value(self, value: Any, values: Mapping[str, Any]) -> Any:
        if not isinstance(value, Mapping):
            raise RuntimeError(f"invalid expression value {value!r}")
        kind = value.get("kind")
        if kind == "literal":
            return value.get("value")
        if kind == "variable":
            ref = value.get("ref")
            if not isinstance(ref, str):
                raise RuntimeError("variable operand lacks ref")
            return values.get(ref, self.defaults.get(ref))
        if kind == "expression":
            ref = value.get("ref")
            if not isinstance(ref, str):
                raise RuntimeError("expression operand lacks ref")
            return self.eval_expression(ref, values)
        if kind == "entity":
            return value.get("ref")
        raise RuntimeError(f"unsupported value kind {kind!r}")

    def eval_expression(self, expression_id: str, values: Mapping[str, Any]) -> Any:
        row = self.expressions.get(expression_id)
        if row is None:
            raise RuntimeError(f"unknown expression {expression_id!r}")
        args = row.get("args", [])
        if not isinstance(args, list):
            raise RuntimeError(f"expression {expression_id!r} args malformed")
        operands = [self.eval_value(arg, values) for arg in args]
        op = row.get("op")
        if op in {"ktrf:const", "ktrf:var"}:
            return operands[0]
        if op == "ktrf:eq":
            return operands[0] == operands[1]
        if op == "ktrf:ne":
            return operands[0] != operands[1]
        if op == "ktrf:lt":
            return operands[0] < operands[1]
        if op == "ktrf:le":
            return operands[0] <= operands[1]
        if op == "ktrf:gt":
            return operands[0] > operands[1]
        if op == "ktrf:ge":
            return operands[0] >= operands[1]
        if op == "ktrf:and":
            return all(bool(v) for v in operands)
        if op == "ktrf:or":
            return any(bool(v) for v in operands)
        if op == "ktrf:not":
            return not bool(operands[0])
        raise RuntimeError(f"targeted selector cannot execute op {op!r}")

    def variables_in_value(self, value: Any, seen_expr: set[str] | None = None) -> set[str]:
        if not isinstance(value, Mapping):
            return set()
        kind = value.get("kind")
        if kind == "variable" and isinstance(value.get("ref"), str):
            return {str(value["ref"])}
        if kind == "expression" and isinstance(value.get("ref"), str):
            return self.variables_in_expression(str(value["ref"]), seen_expr)
        return set()

    def variables_in_expression(self, expression_id: str, seen_expr: set[str] | None = None) -> set[str]:
        if seen_expr is None:
            seen_expr = set()
        if expression_id in seen_expr:
            return set()
        seen_expr.add(expression_id)
        row = self.expressions.get(expression_id)
        if row is None:
            return set()
        out: set[str] = set()
        args = row.get("args", [])
        if isinstance(args, list):
            for arg in args:
                out.update(self.variables_in_value(arg, seen_expr))
        return out

    def _add_hint(self, hints: dict[str, set[int]], var_id: str, value: int) -> None:
        bucket = hints.setdefault(var_id, set())
        bucket.add(value)
        bucket.add(safe_neighbor(value, -1))
        bucket.add(safe_neighbor(value, 1))

    def collect_hints(self, expression_id: str, hints: dict[str, set[int]], seen: set[str] | None = None) -> None:
        if seen is None:
            seen = set()
        if expression_id in seen:
            return
        seen.add(expression_id)
        row = self.expressions.get(expression_id)
        if row is None:
            return
        args = row.get("args", [])
        if not isinstance(args, list):
            return

        if row.get("op") in {"ktrf:eq", "ktrf:ne", "ktrf:lt", "ktrf:le", "ktrf:gt", "ktrf:ge"} and len(args) == 2:
            left, right = args
            if isinstance(left, Mapping) and isinstance(right, Mapping):
                if left.get("kind") == "variable" and right.get("kind") == "literal":
                    ref = left.get("ref")
                    value = right.get("value")
                    if isinstance(ref, str) and isinstance(value, int) and not isinstance(value, bool):
                        self._add_hint(hints, ref, value)
                if right.get("kind") == "variable" and left.get("kind") == "literal":
                    ref = right.get("ref")
                    value = left.get("value")
                    if isinstance(ref, str) and isinstance(value, int) and not isinstance(value, bool):
                        self._add_hint(hints, ref, value)
                if left.get("kind") == "variable" and right.get("kind") == "variable":
                    left_ref = left.get("ref")
                    right_ref = right.get("ref")
                    for ref in (left_ref, right_ref):
                        if isinstance(ref, str):
                            hints.setdefault(ref, set()).update({-1, 0, 1})

        for arg in args:
            if isinstance(arg, Mapping) and arg.get("kind") == "expression" and isinstance(arg.get("ref"), str):
                self.collect_hints(str(arg["ref"]), hints, seen)

    def select(self, source: str, values: Mapping[str, Any]) -> Mapping[str, Any] | None:
        if source in self.choice_by_node and values.get(self.choice_var, self.defaults[self.choice_var]) == PENDING_CHOICE:
            return None
        for row in self.transitions_by_source.get(source, []):
            triggers = row.get("triggers")
            if triggers is None:
                triggers = ["ktrf:next"]
            if not isinstance(triggers, list) or "ktrf:next" not in triggers:
                continue
            predicate = row.get("predicate")
            if predicate is None or self.eval_expression(str(predicate), values):
                return row
        return None

    def domains_for_source(self, source: str) -> tuple[list[str], list[list[int]]]:
        rows = self.transitions_by_source.get(source, [])
        relevant: set[str] = set()
        hints: dict[str, set[int]] = {}
        for row in rows:
            predicate = row.get("predicate")
            if isinstance(predicate, str):
                relevant.update(self.variables_in_expression(predicate))
                self.collect_hints(predicate, hints)

        choice_rows = self.choice_by_node.get(source, [])
        if choice_rows:
            relevant.add(self.choice_var)
            valid_values: set[int] = set()
            for choice in choice_rows:
                options = choice.get("options", [])
                if isinstance(options, list):
                    for option in options:
                        if isinstance(option, Mapping):
                            value = option.get("value")
                            if isinstance(value, int) and not isinstance(value, bool):
                                valid_values.add(value)
                timeout = choice.get("timeout")
                if isinstance(timeout, Mapping):
                    value = timeout.get("value")
                    if isinstance(value, int) and not isinstance(value, bool):
                        valid_values.add(value)
            hints[self.choice_var] = valid_values

        # ROUTE/SCENE are profile-coordinate mirrors. A targeted pre-state must
        # keep them consistent with the source Node rather than treating them as
        # freely injectable predicate variables.
        relevant.discard(self.route_var)
        relevant.discard(self.scene_var)

        variable_ids = sorted(relevant)
        domains: list[list[int]] = []
        for var_id in variable_ids:
            if var_id == self.choice_var and choice_rows:
                domain = sorted(hints.get(var_id, set()))
                if not domain:
                    raise RuntimeError(f"choice Node {source!r} has no routable result values")
                domains.append(domain)
                continue

            values: set[int] = set(hints.get(var_id, set()))
            default = self.defaults.get(var_id, 0)
            if isinstance(default, int) and not isinstance(default, bool):
                values.add(default)
            if not values:
                values.update({-1, 0, 1})
            domains.append(sorted(values))
        return variable_ids, domains

    def make_values(self, source: str, assignment: Mapping[str, int]) -> dict[str, Any]:
        values = dict(self.defaults)
        node = self.nodes.get(source)
        if node is None:
            raise RuntimeError(f"unknown source Node {source!r}")
        route, scene = node_coordinate(node)
        values[self.route_var] = route
        values[self.scene_var] = scene
        values.update(assignment)
        return values


def generate_fixtures(
    selector: Selector,
    target_ids: set[int],
    max_combinations_per_node: int,
) -> tuple[list[Fixture], list[int], int]:
    target_rows: dict[int, Mapping[str, Any]] = {}
    for rows in selector.transitions_by_source.values():
        for row in rows:
            sid = source_transition_id(row)
            if sid in target_ids:
                target_rows[sid] = row
    missing_catalog = sorted(target_ids - set(target_rows))
    if missing_catalog:
        raise RuntimeError(f"target transition IDs absent from IR: {missing_catalog[:20]}")

    targets_by_source: dict[str, set[int]] = {}
    for sid, row in target_rows.items():
        source = row.get("source")
        if not isinstance(source, str):
            raise RuntimeError(f"target t{sid} lacks source")
        targets_by_source.setdefault(source, set()).add(sid)

    fixtures: dict[int, Fixture] = {}
    combinations_tested = 0

    for source, source_targets in sorted(targets_by_source.items()):
        variable_ids, domains = selector.domains_for_source(source)
        iterator = itertools.product(*domains) if domains else [()]
        local_tested = 0
        for combo in iterator:
            local_tested += 1
            combinations_tested += 1
            if local_tested > max_combinations_per_node:
                break
            assignment = {var_id: int(value) for var_id, value in zip(variable_ids, combo)}
            values = selector.make_values(source, assignment)
            selected = selector.select(source, values)
            if selected is None:
                continue
            sid = source_transition_id(selected)
            if sid not in source_targets or sid in fixtures:
                continue
            node = selector.nodes[source]
            route, scene = node_coordinate(node)
            transition_id = selected.get("id")
            if not isinstance(transition_id, str):
                raise RuntimeError(f"t{sid} has invalid KTRF ID")
            fixtures[sid] = Fixture(
                source_transition_id=sid,
                transition_id=transition_id,
                source_node=source,
                route=route,
                scene=scene,
                assignments=assignment,
            )
            if source_targets.issubset(fixtures.keys()):
                break

    unresolved = sorted(target_ids - set(fixtures))
    return [fixtures[sid] for sid in sorted(fixtures)], unresolved, combinations_tested


def trace_payload(selector: Selector, fixtures: Sequence[Fixture]) -> str:
    lines: list[str] = []
    for fixture in fixtures:
        choice = int(fixture.assignments.get(selector.choice_var, selector.defaults.get(selector.choice_var, PENDING_CHOICE)))
        callback34 = int(fixture.assignments.get(selector.callback34_var, selector.defaults.get(selector.callback34_var, 0)))
        feeling_applied = 1 if fixture.source_node in selector.choice_by_node and choice != PENDING_CHOICE else 0
        lines.append("RESET")
        lines.append(
            f"INJECT {fixture.route} {fixture.scene} {choice} {callback34} {feeling_applied}"
        )
        for var_id, value in sorted(fixture.assignments.items()):
            if var_id in {selector.choice_var, selector.callback34_var, selector.route_var, selector.scene_var}:
                continue
            source = selector.variable_source.get(var_id)
            if source is None:
                raise RuntimeError(f"cannot inject Variable without School Days source metadata: {var_id}")
            storage, symbol = source
            if storage == "session":
                lines.append(f"SESSION {symbol} {value}")
            elif storage == "global":
                lines.append(f"GLOBAL {symbol} {value}")
            else:
                raise RuntimeError(f"unsupported source storage {storage!r} for {var_id}")
        lines.append(f"RESOLVE {fixture.source_transition_id}")
    return "\n".join(lines) + "\n"


def run_cpp_batch(
    executable: Path,
    selector: Selector,
    fixtures: Sequence[Fixture],
) -> list[dict[str, Any]]:
    completed = subprocess.run(
        [str(executable)],
        input=trace_payload(selector, fixtures),
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "compiled C++ targeted trace failed\n"
            f"exit={completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    rows: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if not isinstance(row, dict):
            raise RuntimeError("C++ targeted trace emitted non-object JSON")
        rows.append(row)
    if len(rows) != len(fixtures):
        raise RuntimeError(f"C++ targeted trace row count mismatch: {len(rows)} != {len(fixtures)}")
    return rows


def inject_ir(vm: Any, selector: Selector, fixture: Fixture) -> None:
    vm.reset("sdhq:entry:new-game")
    vm.current_node = fixture.source_node
    vm.terminal = False
    vm.ending_registrations = []
    vm.hook_history = []
    vm._committed_choices = {}
    vm.write_variable(selector.route_var, fixture.route)
    vm.write_variable(selector.scene_var, fixture.scene)
    for var_id, value in fixture.assignments.items():
        vm.write_variable(var_id, value)


def compare_fixture(
    fixture: Fixture,
    cpp: Mapping[str, Any],
    document: Mapping[str, Any],
    selector: Selector,
    Interpreter: Any,
    Differential: Any,
) -> None:
    diff = Differential(document, Interpreter)
    vm = Interpreter(document)
    inject_ir(vm, selector, fixture)
    result = vm.trigger("ktrf:next")
    if result is None:
        raise AssertionError(f"t{fixture.source_transition_id}: KTRF IR returned unresolved")

    def fail(field: str, expected: Any, actual: Any) -> None:
        raise AssertionError(
            f"t{fixture.source_transition_id} targeted differential: {field} divergence\n"
            f"  C++ oracle: {expected!r}\n"
            f"  KTRF IR:    {actual!r}\n"
            f"  injected:   {fixture.assignments!r}"
        )

    actual_sid = diff.source_transition_id(result)
    if actual_sid != fixture.source_transition_id:
        fail("transition", fixture.source_transition_id, actual_sid)
    if int(cpp.get("transition", -1)) != fixture.source_transition_id:
        fail("C++ selected transition", fixture.source_transition_id, cpp.get("transition"))

    actual_kind = "terminal" if result.terminal else "advanced"
    if cpp.get("kind") != actual_kind:
        fail("kind", cpp.get("kind"), actual_kind)

    cpp_coord = (int(cpp.get("route", -1)), int(cpp.get("scene", -1)))
    ir_coord = diff.profile_coordinate(vm)
    if cpp_coord != ir_coord:
        fail("profile ROUTE/SCENE", cpp_coord, ir_coord)

    if not result.terminal:
        node_coord = diff.node_coordinate(vm.current_node)
        if node_coord != cpp_coord:
            fail("current Node coordinate", cpp_coord, node_coord)
        cpp_scene = str(cpp.get("current_scene", ""))
        ir_scene = diff.node_scene_key(vm.current_node)
        if cpp_scene != ir_scene:
            fail("current SceneKey", cpp_scene, ir_scene)

    cpp_destination = str(cpp.get("destination", ""))
    ir_destination = diff.node_scene_key(result.destination)
    if cpp_destination != ir_destination:
        fail("destination", cpp_destination, ir_destination)

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
            raise AssertionError(f"t{fixture.source_transition_id}: malformed registered Ending {ending_id!r}")
        new_codes.append(int(ending["code"]))
    ir_ending_id = new_codes[-1] if new_codes else -1
    if int(cpp.get("ending_id", -1)) != ir_ending_id:
        fail("newly registered ending", cpp.get("ending_id"), ir_ending_id)

    cpp_choice = int(cpp.get("choice_result", 0))
    ir_choice = int(vm.read_variable(selector.choice_var))
    if cpp_choice != ir_choice:
        fail("choice_result", cpp_choice, ir_choice)

    cpp_callback34 = int(cpp.get("callback34", 0))
    ir_callback34 = int(vm.read_variable(selector.callback34_var))
    if cpp_callback34 != ir_callback34:
        fail("callback34", cpp_callback34, ir_callback34)

    cpp_session = cpp.get("session", {})
    cpp_global = cpp.get("global", {})
    if not isinstance(cpp_session, Mapping) or not isinstance(cpp_global, Mapping):
        raise AssertionError(f"t{fixture.source_transition_id}: malformed C++ state maps")

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


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Target uncovered School Days transitions with state-injection differential tests")
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, default=Path("tools/ktrf/interpreter.py"))
    parser.add_argument("--diff-module", type=Path, default=Path("tools/ktrf/diff_sdhq_witnesses.py"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/ktrf/school-days-hq.targeted-transition-diff.json"),
    )
    parser.add_argument("--max-combinations-per-node", type=int, default=250000)
    args = parser.parse_args(argv)

    ir_path = args.ir.resolve()
    coverage_path = args.coverage.resolve()
    oracle_exe = args.oracle_exe.resolve()
    interpreter_path = args.interpreter.resolve()
    diff_path = args.diff_module.resolve()
    for path in (ir_path, coverage_path, oracle_exe, interpreter_path, diff_path):
        if not path.exists():
            raise SystemExit(f"required path not found: {path}")

    document = json.loads(ir_path.read_text(encoding="utf-8-sig"))
    coverage = json.loads(coverage_path.read_text(encoding="utf-8-sig"))
    if not isinstance(document, Mapping) or not isinstance(coverage, Mapping):
        raise SystemExit("IR/coverage root must be an object")
    if coverage.get("oracle_commit") != ORACLE_COMMIT:
        raise SystemExit("coverage report does not target the frozen School Days oracle")

    gap_block = coverage.get("gaps", {})
    raw_targets = gap_block.get("transition_ids", []) if isinstance(gap_block, Mapping) else []
    if not isinstance(raw_targets, list) or not all(isinstance(x, int) for x in raw_targets):
        raise SystemExit("coverage report has malformed transition gap list")
    target_ids = {int(x) for x in raw_targets}

    interpreter_module = load_module("ktrf_targeted_interpreter", interpreter_path)
    diff_module = load_module("ktrf_targeted_diff_common", diff_path)
    Interpreter = interpreter_module.Interpreter
    Differential = diff_module.Differential
    selector = Selector(document)

    fixtures, unresolved, combinations = generate_fixtures(
        selector,
        target_ids,
        args.max_combinations_per_node,
    )

    report: dict[str, Any] = {
        "format": "ktrf-sdhq-targeted-transition-differential-v0.1",
        "oracle_commit": ORACLE_COMMIT,
        "input": {
            "target_gap_count": len(target_ids),
            "max_combinations_per_node": args.max_combinations_per_node,
        },
        "solver": {
            "combinations_tested": combinations,
            "fixtures_found": len(fixtures),
            "unresolved_transition_ids": unresolved,
        },
        "differential": {
            "compared": 0,
            "divergences": 0,
        },
        "combined_coverage": {},
        "fixtures": [
            {
                "source_transition_id": f.source_transition_id,
                "transition_id": f.transition_id,
                "source_node": f.source_node,
                "route": f.route,
                "scene": f.scene,
                "assignments": f.assignments,
            }
            for f in fixtures
        ],
    }

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    if unresolved:
        report["status"] = "INCOMPLETE"
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        print("=== KTRF / School Days targeted transition differential ===")
        print(f"targets={len(target_ids)}")
        print(f"fixtures={len(fixtures)}")
        print(f"solver_combinations={combinations}")
        print(f"unresolved={len(unresolved)}")
        print("unresolved_ids=" + ",".join(str(x) for x in unresolved[:100]))
        print(f"output={output}")
        print("TARGET FIXTURE GENERATION INCOMPLETE")
        return 3

    cpp_rows = run_cpp_batch(oracle_exe, selector, fixtures)
    compared = 0
    for fixture, cpp in zip(fixtures, cpp_rows):
        compare_fixture(fixture, cpp, document, selector, Interpreter, Differential)
        compared += 1

    total_transitions = len(records(document, "transitions"))
    witness_covered = total_transitions - len(target_ids)
    combined_covered = witness_covered + compared
    report["differential"] = {"compared": compared, "divergences": 0}
    report["combined_coverage"] = {
        "witness_transitions": witness_covered,
        "targeted_transitions": compared,
        "covered": combined_covered,
        "total": total_transitions,
        "percent": round(combined_covered * 100.0 / total_transitions, 3) if total_transitions else 0.0,
    }
    report["status"] = "PASS"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

    print("=== KTRF / School Days targeted transition differential ===")
    print(f"targets={len(target_ids)}")
    print(f"fixtures={len(fixtures)}")
    print(f"solver_combinations={combinations}")
    print("unresolved=0")
    print(f"differential={compared}/{len(target_ids)}")
    print(f"combined_transition_coverage={combined_covered}/{total_transitions} ({report['combined_coverage']['percent']}%)")
    print("divergences=0")
    print(f"output={output}")
    print("TARGETED TRANSITION DIFFERENTIAL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
