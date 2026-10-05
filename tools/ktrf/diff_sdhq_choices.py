#!/usr/bin/env python3
"""Exhaustively differential-test exported School Days Choice outcomes.

For every exported Choice option and timeout outcome, this tool injects the same
post-scene/pre-choice state into the frozen C++ SchoolDaysRouter test adapter
and the KTRF Canonical Routing IR reference interpreter, commits the Choice,
and compares the resulting routing state.

The test explicitly covers all exported source FeelingResolution rows and all
source FeelingDelta rows.  It also checks same-result idempotence and rejection
of a different result after a Choice has already been committed.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence


EXPECTED_CHOICES = 287
EXPECTED_FEELING_RESOLUTIONS = 772
EXPECTED_FEELING_DELTAS = 302
PENDING_CHOICE = -2


@dataclass(frozen=True)
class Outcome:
    choice_id: str
    node_id: str
    scene_key: str
    route: int
    scene: int
    value: int
    effect_ids: tuple[str, ...]
    resolution_index: int | None
    alternate_value: int


@dataclass(frozen=True)
class Scenario:
    outcome: Outcome
    mode: str
    seeds: tuple[tuple[str, int], ...]


def load_interpreter(path: Path):
    spec = importlib.util.spec_from_file_location("ktrf_interpreter_choice_diff", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load interpreter: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    return module.Interpreter


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


def node_coordinate(row: Mapping[str, Any]) -> tuple[int, int, str]:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping):
        raise RuntimeError(f"node {row.get('id')!r} lacks metadata")
    route = meta.get("route")
    scene = meta.get("scene")
    scene_key = meta.get("scene_key")
    if not isinstance(route, int) or not isinstance(scene, int) or not isinstance(scene_key, str):
        raise RuntimeError(f"node {row.get('id')!r} lacks School Days coordinate metadata")
    return route, scene, scene_key


def outcome_metadata(row: Mapping[str, Any]) -> tuple[list[str], int | None]:
    effects = row.get("effects", [])
    if not isinstance(effects, list) or not all(isinstance(x, str) for x in effects):
        raise RuntimeError("Choice outcome effects are malformed")
    meta = row.get("metadata", {})
    resolution: int | None = None
    if isinstance(meta, Mapping):
        value = meta.get("source_feeling_resolution_index")
        if isinstance(value, int):
            resolution = value
        elif value is not None:
            raise RuntimeError("source_feeling_resolution_index is not integer/null")
    return list(effects), resolution


def build_outcomes(document: Mapping[str, Any]) -> tuple[list[Outcome], set[int], set[int]]:
    choices = records(document, "choices")
    nodes = index_by_id(document, "nodes")
    effects = index_by_id(document, "effects")

    if len(choices) != EXPECTED_CHOICES:
        raise RuntimeError(f"Choice inventory mismatch: {len(choices)} != {EXPECTED_CHOICES}")

    outcomes: list[Outcome] = []
    resolution_indices: set[int] = set()
    delta_indices: set[int] = set()
    seen_node_value: set[tuple[str, int]] = set()

    for choice in choices:
        choice_id = choice.get("id")
        node_id = choice.get("node")
        if not isinstance(choice_id, str) or not isinstance(node_id, str):
            raise RuntimeError("Choice lacks id/node")
        node = nodes.get(node_id)
        if node is None:
            raise RuntimeError(f"Choice {choice_id} references unknown Node {node_id}")
        route, scene, scene_key = node_coordinate(node)

        raw_outcomes: list[Mapping[str, Any]] = []
        options = choice.get("options", [])
        if not isinstance(options, list):
            raise RuntimeError(f"Choice {choice_id} options are malformed")
        raw_outcomes.extend(row for row in options if isinstance(row, Mapping))
        timeout = choice.get("timeout")
        if not isinstance(timeout, Mapping):
            raise RuntimeError(f"Choice {choice_id} has no timeout outcome")
        raw_outcomes.append(timeout)

        values: list[int] = []
        parsed: list[tuple[int, tuple[str, ...], int | None]] = []
        for raw in raw_outcomes:
            value = raw.get("value")
            if not isinstance(value, int) or isinstance(value, bool):
                raise RuntimeError(f"Choice {choice_id} has non-integer outcome value")
            if value in values:
                raise RuntimeError(f"Choice {choice_id} duplicates outcome value {value}")
            values.append(value)
            effect_ids, resolution_index = outcome_metadata(raw)
            parsed.append((value, tuple(effect_ids), resolution_index))

            if resolution_index is not None:
                if resolution_index in resolution_indices:
                    raise RuntimeError(f"FeelingResolution index {resolution_index} is exported more than once")
                resolution_indices.add(resolution_index)

            for effect_id in effect_ids:
                effect = effects.get(effect_id)
                if effect is None:
                    raise RuntimeError(f"Choice {choice_id} references unknown Effect {effect_id}")
                meta = effect.get("metadata", {})
                if isinstance(meta, Mapping) and isinstance(meta.get("source_feeling_delta_index"), int):
                    delta_indices.add(int(meta["source_feeling_delta_index"]))

        if len(values) < 2:
            raise RuntimeError(f"Choice {choice_id} must expose at least option + timeout for differential testing")

        for value, effect_ids, resolution_index in parsed:
            key = (node_id, value)
            if key in seen_node_value:
                raise RuntimeError(f"duplicate Choice outcome for Node/value {key}")
            seen_node_value.add(key)
            alternate = next(v for v in values if v != value)
            outcomes.append(
                Outcome(
                    choice_id=choice_id,
                    node_id=node_id,
                    scene_key=scene_key,
                    route=route,
                    scene=scene,
                    value=value,
                    effect_ids=effect_ids,
                    resolution_index=resolution_index,
                    alternate_value=alternate,
                )
            )

    if resolution_indices != set(range(EXPECTED_FEELING_RESOLUTIONS)):
        missing = sorted(set(range(EXPECTED_FEELING_RESOLUTIONS)) - resolution_indices)
        extra = sorted(resolution_indices - set(range(EXPECTED_FEELING_RESOLUTIONS)))
        raise RuntimeError(
            f"FeelingResolution coverage mismatch: covered={len(resolution_indices)} "
            f"missing={missing[:20]} extra={extra[:20]}"
        )
    if delta_indices != set(range(EXPECTED_FEELING_DELTAS)):
        missing = sorted(set(range(EXPECTED_FEELING_DELTAS)) - delta_indices)
        extra = sorted(delta_indices - set(range(EXPECTED_FEELING_DELTAS)))
        raise RuntimeError(
            f"FeelingDelta coverage mismatch: covered={len(delta_indices)} "
            f"missing={missing[:20]} extra={extra[:20]}"
        )

    return outcomes, resolution_indices, delta_indices


def source_variable_map(document: Mapping[str, Any]) -> tuple[dict[str, tuple[str, str]], dict[str, str]]:
    by_id: dict[str, tuple[str, str]] = {}
    session_by_symbol: dict[str, str] = {}
    for row in records(document, "variables"):
        var_id = row.get("id")
        meta = row.get("metadata", {})
        if not isinstance(var_id, str) or not isinstance(meta, Mapping):
            continue
        symbol = meta.get("source_symbol")
        storage = meta.get("source_storage")
        if isinstance(symbol, str) and isinstance(storage, str):
            by_id[var_id] = (storage, symbol)
            if storage == "session":
                session_by_symbol[symbol] = var_id
    return by_id, session_by_symbol


def touched_variables(outcome: Outcome, effects: Mapping[str, Mapping[str, Any]]) -> list[str]:
    out: set[str] = set()
    for effect_id in outcome.effect_ids:
        effect = effects[effect_id]
        if effect.get("op") != "ktrf:add":
            raise RuntimeError(f"Choice feeling Effect {effect_id} is not ktrf:add")
        args = effect.get("args", {})
        target = args.get("target") if isinstance(args, Mapping) else None
        if not isinstance(target, str):
            raise RuntimeError(f"Choice feeling Effect {effect_id} lacks target")
        out.add(target)
    return sorted(out)


def build_scenarios(
    outcomes: Sequence[Outcome],
    effects: Mapping[str, Mapping[str, Any]],
) -> list[Scenario]:
    scenarios: list[Scenario] = []
    for outcome in outcomes:
        touched = touched_variables(outcome, effects)
        baseline = tuple((var_id, 0) for var_id in touched)
        stress = tuple((var_id, -37 + i * 11) for i, var_id in enumerate(touched))
        scenarios.append(Scenario(outcome=outcome, mode="baseline", seeds=baseline))
        scenarios.append(Scenario(outcome=outcome, mode="stress", seeds=stress))
    return scenarios


def cpp_payload_for_scenario(
    scenario: Scenario,
    var_source: Mapping[str, tuple[str, str]],
) -> list[str]:
    outcome = scenario.outcome
    lines = [
        "RESET",
        f"INJECT {outcome.route} {outcome.scene} {PENDING_CHOICE} 0 0",
    ]
    for var_id, value in scenario.seeds:
        source = var_source.get(var_id)
        if source is None:
            raise RuntimeError(f"feeling Variable {var_id} lacks source storage metadata")
        storage, symbol = source
        if storage != "session":
            raise RuntimeError(f"feeling Variable {var_id} is not session-backed")
        lines.append(f"SESSION {symbol} {value}")
    lines.extend(
        [
            f"ACCEPT {outcome.value}",
            f"ACCEPT {outcome.value}",
            f"ACCEPT {outcome.alternate_value}",
        ]
    )
    return lines


def run_cpp(
    executable: Path,
    scenarios: Sequence[Scenario],
    var_source: Mapping[str, tuple[str, str]],
) -> list[dict[str, Any]]:
    lines: list[str] = []
    for scenario in scenarios:
        lines.extend(cpp_payload_for_scenario(scenario, var_source))

    completed = subprocess.run(
        [str(executable)],
        input="\n".join(lines) + "\n",
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"C++ choice trace failed with exit {completed.returncode}\n"
            f"stdout:\n{completed.stdout[-4000:]}\n"
            f"stderr:\n{completed.stderr[-4000:]}"
        )

    rows: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if not isinstance(row, dict):
            raise RuntimeError("C++ choice trace row is not an object")
        rows.append(row)

    expected = len(scenarios) * 3
    if len(rows) != expected:
        raise RuntimeError(f"C++ choice row count mismatch: {len(rows)} != {expected}")
    return rows


def fail(case: str, field: str, expected: Any, actual: Any) -> None:
    raise AssertionError(
        f"{case}: {field} divergence\n"
        f"  C++ oracle: {expected!r}\n"
        f"  KTRF IR:    {actual!r}"
    )


def compare_state(
    case: str,
    cpp: Mapping[str, Any],
    vm,
    outcome: Outcome,
    expected_accepted: bool,
    var_source: Mapping[str, tuple[str, str]],
) -> None:
    if cpp.get("event") != "choice":
        fail(case, "event", "choice", cpp.get("event"))
    if bool(cpp.get("accepted")) != expected_accepted:
        fail(case, "accepted", expected_accepted, cpp.get("accepted"))
    if cpp.get("current_scene") != outcome.scene_key:
        fail(case, "current_scene", outcome.scene_key, cpp.get("current_scene"))
    if (int(cpp.get("route", -999)), int(cpp.get("scene", -999))) != (outcome.route, outcome.scene):
        fail(case, "route/scene", (outcome.route, outcome.scene), (cpp.get("route"), cpp.get("scene")))

    choice_var = "sdhq:var:internal:choice_result"
    callback34_var = "sdhq:var:internal:callback34"
    actual_choice = int(vm.read_variable(choice_var))
    if int(cpp.get("choice_result", -999)) != actual_choice:
        fail(case, "choice_result", cpp.get("choice_result"), actual_choice)
    actual_callback34 = int(vm.read_variable(callback34_var))
    if int(cpp.get("callback34", -999)) != actual_callback34:
        fail(case, "callback34", cpp.get("callback34"), actual_callback34)

    cpp_session = cpp.get("session", {})
    cpp_global = cpp.get("global", {})
    if not isinstance(cpp_session, Mapping) or not isinstance(cpp_global, Mapping):
        raise RuntimeError(f"{case}: malformed C++ state maps")

    for var_id, (storage, symbol) in var_source.items():
        if storage == "session":
            expected = int(cpp_session.get(symbol, 0))
        elif storage == "global":
            expected = int(cpp_global.get(symbol, 0))
        else:
            continue
        actual = int(vm.read_variable(var_id))
        if expected != actual:
            fail(case, f"{storage} {symbol}", expected, actual)

    cpp_endings = [int(x) for x in cpp.get("endings", [])]
    if cpp_endings:
        fail(case, "ending registrations during Choice commit", [], cpp_endings)


def configure_vm(vm, scenario: Scenario, session_by_symbol: Mapping[str, str]) -> None:
    outcome = scenario.outcome
    vm.reset("sdhq:entry:new-game")
    vm.current_node = outcome.node_id
    vm.terminal = False
    vm._committed_choices = {}
    vm.write_variable(session_by_symbol["ROUTE"], outcome.route)
    vm.write_variable(session_by_symbol["SCENE"], outcome.scene)
    vm.write_variable("sdhq:var:internal:choice_result", PENDING_CHOICE)
    vm.write_variable("sdhq:var:internal:callback34", 0)
    for var_id, value in scenario.seeds:
        vm.write_variable(var_id, value)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Differentially verify every School Days Choice/FeelingResolution")
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, default=Path("tools/ktrf/interpreter.py"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/ktrf/school-days-hq.choice-diff.json"),
    )
    args = parser.parse_args(argv)

    ir_path = args.ir.resolve()
    oracle_exe = args.oracle_exe.resolve()
    interpreter_path = args.interpreter.resolve()
    for path in (ir_path, oracle_exe, interpreter_path):
        if not path.exists():
            raise SystemExit(f"required path not found: {path}")

    document = json.loads(ir_path.read_text(encoding="utf-8-sig"))
    if not isinstance(document, Mapping):
        raise SystemExit("IR root must be an object")

    Interpreter = load_interpreter(interpreter_path)
    outcomes, resolution_indices, delta_indices = build_outcomes(document)
    effects = index_by_id(document, "effects")
    var_source, session_by_symbol = source_variable_map(document)
    for required in ("ROUTE", "SCENE"):
        if required not in session_by_symbol:
            raise RuntimeError(f"School Days IR lacks required session Variable {required}")

    scenarios = build_scenarios(outcomes, effects)
    cpp_rows = run_cpp(oracle_exe, scenarios, var_source)

    row_index = 0
    accepted_checks = 0
    idempotence_checks = 0
    alternate_rejection_checks = 0

    for scenario_index, scenario in enumerate(scenarios):
        outcome = scenario.outcome
        case_base = (
            f"scenario={scenario_index} mode={scenario.mode} "
            f"choice={outcome.choice_id} value={outcome.value}"
        )
        vm = Interpreter(document)
        configure_vm(vm, scenario, session_by_symbol)

        first_ok = vm.commit_choice(outcome.value, outcome.choice_id)
        if not first_ok:
            raise AssertionError(f"{case_base}: KTRF rejected initial valid Choice result")
        cpp_first = cpp_rows[row_index]
        row_index += 1
        compare_state(case_base + " first", cpp_first, vm, outcome, True, var_source)
        if cpp_first.get("feeling_applied") is not True:
            fail(case_base + " first", "feeling_applied", True, cpp_first.get("feeling_applied"))
        accepted_checks += 1

        before_repeat = vm.snapshot()
        repeat_ok = vm.commit_choice(outcome.value, outcome.choice_id)
        after_repeat = vm.snapshot()
        if not repeat_ok:
            raise AssertionError(f"{case_base}: KTRF rejected repeated identical Choice result")
        if before_repeat != after_repeat:
            raise AssertionError(f"{case_base}: repeated identical Choice result mutated KTRF state")
        cpp_repeat = cpp_rows[row_index]
        row_index += 1
        compare_state(case_base + " repeat", cpp_repeat, vm, outcome, True, var_source)
        idempotence_checks += 1

        before_alt = vm.snapshot()
        alt_ok = vm.commit_choice(outcome.alternate_value, outcome.choice_id)
        after_alt = vm.snapshot()
        if alt_ok:
            raise AssertionError(f"{case_base}: KTRF accepted a different result after commit")
        if before_alt != after_alt:
            raise AssertionError(f"{case_base}: rejected alternate Choice mutated KTRF state")
        cpp_alt = cpp_rows[row_index]
        row_index += 1
        compare_state(case_base + " alternate", cpp_alt, vm, outcome, False, var_source)
        alternate_rejection_checks += 1

    if row_index != len(cpp_rows):
        raise RuntimeError(f"internal row accounting mismatch: {row_index} != {len(cpp_rows)}")

    report = {
        "format": "ktrf-sdhq-choice-differential-v0.1",
        "oracle_commit": "614461c2b14951ba117b9d2dedb4983cfa8ae8e6",
        "choices": EXPECTED_CHOICES,
        "choice_outcomes": len(outcomes),
        "seed_modes": 2,
        "scenarios": len(scenarios),
        "feeling_resolutions": {
            "covered": len(resolution_indices),
            "total": EXPECTED_FEELING_RESOLUTIONS,
        },
        "feeling_deltas": {
            "covered": len(delta_indices),
            "total": EXPECTED_FEELING_DELTAS,
        },
        "accepted_checks": accepted_checks,
        "idempotence_checks": idempotence_checks,
        "alternate_rejection_checks": alternate_rejection_checks,
        "divergences": 0,
        "status": "PASS",
    }

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

    print("=== KTRF / School Days Choice + Feeling differential ===")
    print(f"choices={EXPECTED_CHOICES}/{EXPECTED_CHOICES}")
    print(f"choice_outcomes={len(outcomes)}")
    print(f"seed_modes=2")
    print(f"scenarios={len(scenarios)}")
    print(f"feeling_resolutions={len(resolution_indices)}/{EXPECTED_FEELING_RESOLUTIONS}")
    print(f"feeling_deltas={len(delta_indices)}/{EXPECTED_FEELING_DELTAS}")
    print(f"accepted_checks={accepted_checks}")
    print(f"idempotence_checks={idempotence_checks}")
    print(f"alternate_rejection_checks={alternate_rejection_checks}")
    print("divergences=0")
    print(f"output={output}")
    print("CHOICE + FEELING DIFFERENTIAL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
