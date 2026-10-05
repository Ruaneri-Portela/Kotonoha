#!/usr/bin/env python3
"""Differentially execute School Days ending witnesses.

The same witness steps are executed by:

1. the compiled C++ SchoolDaysRouter through SchoolDaysOracleTrace; and
2. the KTRF Canonical Routing IR reference Interpreter.

The verifier compares selected transition IDs, post-step logical state,
variables, endings and callback/hook events after every step.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence


@dataclass(frozen=True)
class WitnessStep:
    route: int
    scene: int
    choice: int
    transition: int


@dataclass(frozen=True)
class Witness:
    name: str
    steps: tuple[WitnessStep, ...]


def load_interpreter(path: Path):
    spec = importlib.util.spec_from_file_location("ktrf_interpreter_diff", path)
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


def parse_witnesses(path: Path) -> list[Witness]:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"static\s+const\s+Step\s+(kEnding\d+)\[\]\s*=\s*\{(.*?)\n\};",
        re.S,
    )
    step_re = re.compile(
        r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}"
    )
    witnesses: list[Witness] = []
    for match in pattern.finditer(text):
        steps = tuple(
            WitnessStep(*(int(group) for group in row.groups()))
            for row in step_re.finditer(match.group(2))
        )
        if not steps:
            raise RuntimeError(f"witness {match.group(1)} contains no steps")
        witnesses.append(Witness(match.group(1), steps))

    if len(witnesses) != 22:
        raise RuntimeError(f"expected 22 ending witness arrays, found {len(witnesses)}")
    return witnesses


def run_cpp_trace(executable: Path, witness: Witness) -> list[dict[str, Any]]:
    payload = ["RESET"]
    payload.extend(
        f"STEP {s.route} {s.scene} {s.choice} {s.transition}"
        for s in witness.steps
    )
    completed = subprocess.run(
        [str(executable)],
        input="\n".join(payload) + "\n",
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"C++ oracle trace failed for {witness.name} with exit {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    rows: list[dict[str, Any]] = []
    for line in completed.stdout.splitlines():
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"invalid JSON from C++ oracle for {witness.name}: {line!r}"
            ) from exc
        if not isinstance(row, dict):
            raise RuntimeError("C++ oracle JSON row is not an object")
        rows.append(row)
    if len(rows) != len(witness.steps):
        raise RuntimeError(
            f"C++ oracle row count mismatch for {witness.name}: {len(rows)} != {len(witness.steps)}"
        )
    return rows


class Differential:
    def __init__(self, document: Mapping[str, Any], Interpreter):
        self.doc = document
        self.Interpreter = Interpreter
        self.transitions = {
            row["id"]: row
            for row in document.get("transitions", [])
            if isinstance(row, Mapping) and isinstance(row.get("id"), str)
        }
        self.nodes = {
            row["id"]: row
            for row in document.get("nodes", [])
            if isinstance(row, Mapping) and isinstance(row.get("id"), str)
        }
        self.endings = {
            row["id"]: row
            for row in document.get("endings", [])
            if isinstance(row, Mapping) and isinstance(row.get("id"), str)
        }
        self.session_vars: list[tuple[str, str]] = []
        self.global_vars: list[tuple[str, str]] = []
        self.internal_choice = "sdhq:var:internal:choice_result"
        self.internal_callback34 = "sdhq:var:internal:callback34"

        for row in document.get("variables", []):
            if not isinstance(row, Mapping) or not isinstance(row.get("id"), str):
                continue
            meta = row.get("metadata", {})
            if not isinstance(meta, Mapping):
                continue
            symbol = meta.get("source_symbol")
            storage = meta.get("source_storage")
            if not isinstance(symbol, str):
                continue
            if storage == "session":
                self.session_vars.append((row["id"], symbol))
            elif storage == "global":
                self.global_vars.append((row["id"], symbol))

    @staticmethod
    def fail(witness: str, step_index: int, field: str, expected: Any, actual: Any) -> None:
        raise AssertionError(
            f"{witness} step {step_index}: {field} divergence\n"
            f"  C++ oracle: {expected!r}\n"
            f"  KTRF IR:    {actual!r}"
        )

    def source_transition_id(self, result) -> int:
        row = self.transitions.get(result.transition_id)
        if row is None:
            raise AssertionError(f"KTRF transition {result.transition_id!r} not indexed")
        meta = row.get("metadata", {})
        value = meta.get("source_transition_id") if isinstance(meta, Mapping) else None
        if not isinstance(value, int):
            raise AssertionError(f"KTRF transition {result.transition_id!r} lacks source_transition_id")
        return value

    def node_coordinate(self, node_id: str | None) -> tuple[int, int] | None:
        if node_id is None:
            return None
        row = self.nodes.get(node_id)
        if row is None:
            return None
        meta = row.get("metadata", {})
        if not isinstance(meta, Mapping):
            return None
        route = meta.get("route")
        scene = meta.get("scene")
        if isinstance(route, int) and isinstance(scene, int):
            return route, scene
        return None

    def node_scene_key(self, node_id: str | None) -> str:
        if node_id is None:
            return ""
        row = self.nodes.get(node_id)
        if row is None:
            return ""
        meta = row.get("metadata", {})
        if not isinstance(meta, Mapping):
            return ""
        value = meta.get("scene_key")
        return value if isinstance(value, str) else ""

    def ending_codes(self, vm) -> list[int]:
        out: list[int] = []
        for ending_id in vm.ending_registrations:
            row = self.endings.get(ending_id)
            if row is None or not isinstance(row.get("code"), int):
                raise AssertionError(f"ending {ending_id!r} has no integer code")
            out.append(int(row["code"]))
        return out

    def compare_witness(self, witness: Witness, cpp_rows: Sequence[Mapping[str, Any]]) -> int:
        vm = self.Interpreter(self.doc)
        vm.reset("sdhq:entry:new-game")
        compared = 0

        for i, (step, cpp) in enumerate(zip(witness.steps, cpp_rows)):
            coord = self.node_coordinate(vm.current_node)
            if coord != (step.route, step.scene):
                self.fail(witness.name, i, "pre route/scene", (step.route, step.scene), coord)

            if step.choice != -99:
                if not vm.commit_choice(step.choice):
                    raise AssertionError(
                        f"{witness.name} step {i}: KTRF choice rejected {step.choice} at {vm.current_node}"
                    )

            result = vm.trigger("ktrf:next")
            if result is None:
                raise AssertionError(f"{witness.name} step {i}: KTRF returned unresolved")

            actual_transition = self.source_transition_id(result)
            if int(cpp["transition"]) != actual_transition:
                self.fail(witness.name, i, "transition", cpp["transition"], actual_transition)

            actual_kind = "terminal" if result.terminal else "advanced"
            if cpp["kind"] != actual_kind:
                self.fail(witness.name, i, "kind", cpp["kind"], actual_kind)

            ir_coord = self.node_coordinate(vm.current_node)
            # Terminal KTRF retains the logical source Node. C++ currently does
            # the same for the frozen Normal New Game oracle; if a future
            # profile invalidates this assumption the differential will expose it.
            if ir_coord is None:
                self.fail(witness.name, i, "post route/scene", (cpp["route"], cpp["scene"]), ir_coord)
            if (int(cpp["route"]), int(cpp["scene"])) != ir_coord:
                self.fail(
                    witness.name,
                    i,
                    "post route/scene",
                    (cpp["route"], cpp["scene"]),
                    ir_coord,
                )

            scene_key = self.node_scene_key(vm.current_node)
            if cpp["current_scene"] != scene_key:
                self.fail(witness.name, i, "current SceneKey", cpp["current_scene"], scene_key)

            expected_destination = str(cpp["destination"])
            actual_destination = self.node_scene_key(result.destination)
            if expected_destination != actual_destination:
                self.fail(witness.name, i, "destination", expected_destination, actual_destination)

            hook_symbols = [call.symbol for call in result.hook_calls]
            cpp_hook_symbols = [f"overflow.sdhq:{name}" for name in cpp.get("callbacks", [])]
            if cpp_hook_symbols != hook_symbols:
                self.fail(witness.name, i, "callbacks/hooks", cpp_hook_symbols, hook_symbols)

            cpp_endings = [int(x) for x in cpp.get("endings", [])]
            ir_endings = self.ending_codes(vm)
            if cpp_endings != ir_endings:
                self.fail(witness.name, i, "ending registrations", cpp_endings, ir_endings)

            if int(cpp["choice_result"]) != int(vm.read_variable(self.internal_choice)):
                self.fail(
                    witness.name,
                    i,
                    "choice_result",
                    cpp["choice_result"],
                    vm.read_variable(self.internal_choice),
                )
            if int(cpp["callback34"]) != int(vm.read_variable(self.internal_callback34)):
                self.fail(
                    witness.name,
                    i,
                    "callback34",
                    cpp["callback34"],
                    vm.read_variable(self.internal_callback34),
                )

            cpp_session = cpp.get("session", {})
            cpp_global = cpp.get("global", {})
            if not isinstance(cpp_session, Mapping) or not isinstance(cpp_global, Mapping):
                raise AssertionError(f"{witness.name} step {i}: malformed C++ variable maps")

            for var_id, symbol in self.session_vars:
                expected = int(cpp_session.get(symbol, 0))
                actual = int(vm.read_variable(var_id))
                if expected != actual:
                    self.fail(witness.name, i, f"session {symbol}", expected, actual)

            for var_id, symbol in self.global_vars:
                expected = int(cpp_global.get(symbol, 0))
                actual = int(vm.read_variable(var_id))
                if expected != actual:
                    self.fail(witness.name, i, f"global {symbol}", expected, actual)

            compared += 1

        return compared


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Differentially verify KTRF IR against compiled SchoolDaysRouter witnesses")
    parser.add_argument("--ir", type=Path, required=True)
    parser.add_argument("--oracle-exe", type=Path, required=True)
    parser.add_argument(
        "--witness-source",
        type=Path,
        default=Path("tests/SchoolDaysFullRouterTest.cpp"),
    )
    parser.add_argument(
        "--interpreter",
        type=Path,
        default=Path("tools/ktrf/interpreter.py"),
    )
    args = parser.parse_args(argv)

    ir_path = args.ir.resolve()
    executable = args.oracle_exe.resolve()
    witness_source = args.witness_source.resolve()
    interpreter_path = args.interpreter.resolve()

    for path in (ir_path, executable, witness_source, interpreter_path):
        if not path.exists():
            raise SystemExit(f"required path not found: {path}")

    document = json.loads(ir_path.read_text(encoding="utf-8-sig"))
    if not isinstance(document, Mapping):
        raise SystemExit("IR root must be an object")

    Interpreter = load_interpreter(interpreter_path)
    witnesses = parse_witnesses(witness_source)
    diff = Differential(document, Interpreter)

    total_steps = 0
    for witness in witnesses:
        cpp_rows = run_cpp_trace(executable, witness)
        count = diff.compare_witness(witness, cpp_rows)
        total_steps += count
        print(f"PASS {witness.name}: {count} step(s)")

    print("")
    print("=== KTRF / School Days C++ differential ===")
    print(f"witnesses={len(witnesses)}")
    print(f"steps={total_steps}")
    print("divergences=0")
    print("DIFFERENTIAL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
