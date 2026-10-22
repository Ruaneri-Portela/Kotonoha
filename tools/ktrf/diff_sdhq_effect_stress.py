#!/usr/bin/env python3
"""Stress School Days source Effects against the KTRF IR interpreter.

Transition closure already executes every frozen executable transition once.
This layer strengthens data-sensitive Effect coverage by re-running every
transition that owns at least one recovered source Effect under two additional
non-default pre-state seed modes while preserving the same selected branch.

The script reuses the targeted-transition selector/fixture generator and the
same compiled SchoolDaysRouter trace adapter.  It does not introduce a second
routing model.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Mapping, Sequence


ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
EXPECTED_SOURCE_EFFECTS = 6183
SEED_MODES = ("positive", "negative")


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


def index_by_id(document: Mapping[str, Any], key: str) -> dict[str, Mapping[str, Any]]:
    out: dict[str, Mapping[str, Any]] = {}
    for row in records(document, key):
        row_id = row.get("id")
        if isinstance(row_id, str):
            out[row_id] = row
    return out


def stable_seed(var_id: str, mode_index: int) -> int:
    # Deterministic across Python processes; do not use hash().
    acc = 0
    for i, ch in enumerate(var_id, start=1):
        acc = (acc + i * ord(ch)) % 9973
    base = 31 + (acc % 211)
    if mode_index == 0:
        return base
    return -(base + 113)


def source_effect_meta(effect: Mapping[str, Any]) -> Mapping[str, Any] | None:
    meta = effect.get("metadata")
    if not isinstance(meta, Mapping):
        return None
    if not isinstance(meta.get("source_effect_index"), int):
        return None
    return meta


def branch_variables(selector: Any, source: str) -> set[str]:
    out: set[str] = set()
    for row in selector.transitions_by_source.get(source, []):
        predicate = row.get("predicate")
        if isinstance(predicate, str):
            out.update(selector.variables_in_expression(predicate))
    return out


def touched_variables(effect: Mapping[str, Any]) -> set[str]:
    args = effect.get("args", {})
    if not isinstance(args, Mapping):
        return set()
    op = effect.get("op")
    out: set[str] = set()
    if op in {"ktrf:set", "ktrf:add"}:
        target = args.get("target")
        if isinstance(target, str):
            out.add(target)
    elif op == "ktrf:copy":
        target = args.get("target")
        source = args.get("source")
        if isinstance(target, str):
            out.add(target)
        if isinstance(source, str):
            out.add(source)
    return out


def transition_source_effects(
    transition: Mapping[str, Any],
    effects: Mapping[str, Mapping[str, Any]],
) -> list[str]:
    refs = transition.get("effects", [])
    if not isinstance(refs, list):
        return []
    out: list[str] = []
    for ref in refs:
        if not isinstance(ref, str):
            continue
        effect = effects.get(ref)
        if effect is not None and source_effect_meta(effect) is not None:
            out.append(ref)
    return out


def make_stress_fixture(
    base: Any,
    selector: Any,
    transition: Mapping[str, Any],
    source_effect_ids: Sequence[str],
    effects: Mapping[str, Mapping[str, Any]],
    mode_index: int,
    Fixture: Any,
) -> tuple[Any, dict[str, Any]]:
    assignments = dict(base.assignments)
    branch_vars = branch_variables(selector, base.source_node)
    protected = {
        selector.route_var,
        selector.scene_var,
        selector.choice_var,
        selector.callback34_var,
    }

    touched: set[str] = set()
    for effect_id in source_effect_ids:
        touched.update(touched_variables(effects[effect_id]))

    stressable = sorted(
        var_id
        for var_id in touched
        if var_id not in branch_vars
        and var_id not in protected
        and var_id in selector.variable_source
    )

    for var_id in stressable:
        assignments[var_id] = stable_seed(var_id, mode_index)

    # Avoid accidental no-op pre-seeds for constant assignments where the
    # target is free to perturb.
    for effect_id in source_effect_ids:
        effect = effects[effect_id]
        if effect.get("op") != "ktrf:set":
            continue
        args = effect.get("args", {})
        if not isinstance(args, Mapping):
            continue
        target = args.get("target")
        value_ref = args.get("value")
        if not isinstance(target, str) or target not in stressable:
            continue
        if isinstance(value_ref, Mapping) and value_ref.get("kind") == "literal":
            literal_value = value_ref.get("value")
            if isinstance(literal_value, int) and assignments.get(target) == literal_value:
                assignments[target] = literal_value + (17 if mode_index == 0 else -19)

    values = selector.make_values(base.source_node, assignments)
    selected = selector.select(base.source_node, values)
    if selected is None:
        raise RuntimeError(
            f"stress mode {SEED_MODES[mode_index]} made t{base.source_transition_id} unresolved"
        )
    selected_sid = selector_source_transition_id(selected)
    if selected_sid != base.source_transition_id:
        raise RuntimeError(
            f"stress mode {SEED_MODES[mode_index]} changed selected transition "
            f"t{base.source_transition_id} -> t{selected_sid}"
        )

    fixture = Fixture(
        source_transition_id=base.source_transition_id,
        transition_id=base.transition_id,
        source_node=base.source_node,
        route=base.route,
        scene=base.scene,
        assignments=assignments,
    )
    return fixture, {
        "branch_variable_count": len(branch_vars),
        "touched_variable_count": len(touched),
        "stressable_variable_count": len(stressable),
        "stressable_variables": stressable,
    }


def selector_source_transition_id(row: Mapping[str, Any]) -> int:
    meta = row.get("metadata", {})
    if not isinstance(meta, Mapping) or not isinstance(meta.get("source_transition_id"), int):
        raise RuntimeError(f"transition {row.get('id')!r} lacks source_transition_id")
    return int(meta["source_transition_id"])


def effect_sensitivity_report(
    transitions: Sequence[Mapping[str, Any]],
    effects: Mapping[str, Mapping[str, Any]],
    selector: Any,
) -> dict[str, Any]:
    kind_counts: Counter[str] = Counter()
    free_target = 0
    constrained_target = 0
    copy_total = 0
    copy_free_source = 0
    copy_constrained_source = 0
    copy_self = 0

    for transition in transitions:
        source = transition.get("source")
        if not isinstance(source, str):
            continue
        branch_vars = branch_variables(selector, source)
        for effect_id in transition_source_effects(transition, effects):
            effect = effects[effect_id]
            meta = source_effect_meta(effect)
            assert meta is not None
            kind = str(meta.get("source_kind", "unknown"))
            kind_counts[kind] += 1
            args = effect.get("args", {})
            if not isinstance(args, Mapping):
                continue
            if effect.get("op") == "ktrf:set":
                target = args.get("target")
                if isinstance(target, str):
                    if target in branch_vars or target in {
                        selector.route_var,
                        selector.scene_var,
                        selector.choice_var,
                        selector.callback34_var,
                    }:
                        constrained_target += 1
                    else:
                        free_target += 1
            elif effect.get("op") == "ktrf:copy":
                copy_total += 1
                source_var = args.get("source")
                target_var = args.get("target")
                if source_var == target_var:
                    copy_self += 1
                if isinstance(source_var, str):
                    if source_var in branch_vars or source_var in {
                        selector.route_var,
                        selector.scene_var,
                        selector.choice_var,
                        selector.callback34_var,
                    }:
                        copy_constrained_source += 1
                    else:
                        copy_free_source += 1

    return {
        "source_kind_counts": dict(sorted(kind_counts.items())),
        "constant_set_targets": {
            "free_to_preseed": free_target,
            "branch_or_coordinate_constrained": constrained_target,
        },
        "copy_effects": {
            "total": copy_total,
            "free_source_to_preseed": copy_free_source,
            "branch_or_coordinate_constrained_source": copy_constrained_source,
            "self_copy": copy_self,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Stress School Days source Effects with non-default pre-state differential execution")
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, default=Path("tools/ktrf/interpreter.py"))
    parser.add_argument("--targeted-module", type=Path, default=Path("tools/ktrf/diff_sdhq_targeted_transitions.py"))
    parser.add_argument("--diff-module", type=Path, default=Path("tools/ktrf/diff_sdhq_witnesses.py"))
    parser.add_argument("--output", type=Path, default=Path("build/ktrf/school-days-hq.effect-stress-diff.json"))
    parser.add_argument("--max-combinations-per-node", type=int, default=250000)
    args = parser.parse_args()

    for path in (args.ir, args.oracle_exe, args.interpreter, args.targeted_module, args.diff_module):
        if not path.resolve().exists():
            raise SystemExit(f"required path not found: {path.resolve()}")

    document = load_json(args.ir.resolve())

    # Load the already-validated reference components instead of reimplementing
    # transition selection or post-state comparison here.
    import importlib.util
    import sys

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

    targeted = load_module("ktrf_effect_stress_targeted", args.targeted_module)
    interpreter_module = load_module("ktrf_effect_stress_interpreter", args.interpreter)
    diff_module = load_module("ktrf_effect_stress_common", args.diff_module)

    Selector = targeted.Selector
    Fixture = targeted.Fixture
    Interpreter = interpreter_module.Interpreter
    Differential = diff_module.Differential

    selector = Selector(document)
    effects = index_by_id(document, "effects")
    transitions = records(document, "transitions")

    source_effect_ids = {
        effect_id
        for effect_id, effect in effects.items()
        if source_effect_meta(effect) is not None
    }
    if len(source_effect_ids) != EXPECTED_SOURCE_EFFECTS:
        raise SystemExit(
            f"source Effect inventory mismatch: {len(source_effect_ids)} != {EXPECTED_SOURCE_EFFECTS}"
        )

    target_transitions: list[Mapping[str, Any]] = []
    target_ids: set[int] = set()
    covered_source_effect_ids: set[str] = set()
    transition_by_sid: dict[int, Mapping[str, Any]] = {}

    for transition in transitions:
        sid = selector_source_transition_id(transition)
        transition_by_sid[sid] = transition
        refs = transition_source_effects(transition, effects)
        if refs:
            target_transitions.append(transition)
            target_ids.add(sid)
            covered_source_effect_ids.update(refs)

    missing_effects = sorted(source_effect_ids - covered_source_effect_ids)
    if missing_effects:
        raise SystemExit(
            f"{len(missing_effects)} source Effects are not owned by any executable transition; first={missing_effects[:10]}"
        )

    base_fixtures, unresolved, combinations = targeted.generate_fixtures(
        selector,
        target_ids,
        args.max_combinations_per_node,
    )
    if unresolved:
        raise SystemExit(
            "effect-stress fixture generation incomplete: " + ",".join(str(x) for x in unresolved[:100])
        )

    stress_fixtures: list[Any] = []
    fixture_metadata: list[dict[str, Any]] = []
    for mode_index, mode_name in enumerate(SEED_MODES):
        for base in base_fixtures:
            transition = transition_by_sid[base.source_transition_id]
            refs = transition_source_effects(transition, effects)
            fixture, meta = make_stress_fixture(
                base,
                selector,
                transition,
                refs,
                effects,
                mode_index,
                Fixture,
            )
            stress_fixtures.append(fixture)
            fixture_metadata.append(
                {
                    "mode": mode_name,
                    "source_transition_id": base.source_transition_id,
                    "source_effect_count": len(refs),
                    **meta,
                }
            )

    cpp_rows = targeted.run_cpp_batch(args.oracle_exe.resolve(), selector, stress_fixtures)
    compared = 0
    for fixture, cpp in zip(stress_fixtures, cpp_rows):
        targeted.compare_fixture(
            fixture,
            cpp,
            document,
            selector,
            Interpreter,
            Differential,
        )
        compared += 1

    sensitivity = effect_sensitivity_report(target_transitions, effects, selector)
    stressable_refs = sum(1 for row in fixture_metadata if row["stressable_variable_count"] > 0)
    max_stressable = max((int(row["stressable_variable_count"]) for row in fixture_metadata), default=0)

    report = {
        "format": "ktrf-sdhq-effect-stress-differential-v0.1",
        "oracle_commit": ORACLE_COMMIT,
        "source_effect_inventory": {
            "covered": len(covered_source_effect_ids),
            "total": len(source_effect_ids),
        },
        "transitions_with_source_effects": len(target_ids),
        "solver_combinations": combinations,
        "seed_modes": list(SEED_MODES),
        "scenarios": len(stress_fixtures),
        "scenarios_with_stressable_variables": stressable_refs,
        "max_stressable_variables_in_scenario": max_stressable,
        "sensitivity": sensitivity,
        "differential": {
            "compared": compared,
            "divergences": 0,
        },
        "status": "PASS",
        "fixtures": fixture_metadata,
    }

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print("=== KTRF / School Days source Effect stress differential ===")
    print(f"source_effects={len(covered_source_effect_ids)}/{len(source_effect_ids)}")
    print(f"transitions_with_source_effects={len(target_ids)}")
    print(f"seed_modes={len(SEED_MODES)}")
    print(f"scenarios={len(stress_fixtures)}")
    print(f"solver_combinations={combinations}")
    print(f"scenarios_with_stressable_variables={stressable_refs}")
    print("source_kind_counts=" + json.dumps(sensitivity["source_kind_counts"], sort_keys=True))
    print("copy_effects=" + json.dumps(sensitivity["copy_effects"], sort_keys=True))
    print("constant_set_targets=" + json.dumps(sensitivity["constant_set_targets"], sort_keys=True))
    print(f"differential={compared}/{len(stress_fixtures)}")
    print("divergences=0")
    print(f"output={output}")
    print("SOURCE EFFECT STRESS DIFFERENTIAL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
