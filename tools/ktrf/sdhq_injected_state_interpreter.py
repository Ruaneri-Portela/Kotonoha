#!/usr/bin/env python3
"""Test-only School Days injected-state adapter for the KTRF interpreter.

Targeted transition, source-Effect stress and broader state-matrix differentials
inject snapshots that may represent either side of a deferred Choice commit.
The generic Interpreter keeps committed Choice state separately from declared
Variable values, so a post-choice injected snapshot must restore that runtime
bookkeeping before routing is evaluated.

This adapter is intentionally School Days differential-test glue. It never
executes Choice outcome Effects while restoring a snapshot; those Effects are
already represented by the injected state and are covered independently by the
Choice/Feeling differential.
"""

from __future__ import annotations

import copy
import importlib.util
import sys
from pathlib import Path
from typing import Any, Mapping


_BASE_PATH = Path(__file__).with_name("interpreter.py")
_spec = importlib.util.spec_from_file_location("ktrf_reference_interpreter_base", _BASE_PATH)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"cannot load base KTRF interpreter: {_BASE_PATH}")
_base = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _base
_spec.loader.exec_module(_base)

KtrfRuntimeError = _base.KtrfRuntimeError
HookCall = _base.HookCall
TransitionResult = _base.TransitionResult


class Interpreter(_base.Interpreter):
    """Reference Interpreter plus test-only restoration of Choice commit state."""

    @staticmethod
    def _accepted_choice_values(row: Mapping[str, Any]) -> list[Any]:
        values: list[Any] = []
        for option in row.get("options", []):
            if isinstance(option, Mapping) and "value" in option:
                values.append(option.get("value"))
        timeout = row.get("timeout")
        if isinstance(timeout, Mapping) and "value" in timeout:
            values.append(timeout.get("value"))
        return values

    def _restore_committed_choice_state_from_values(self) -> None:
        if self.current_node is None:
            return
        for row in self.choices_by_node.get(self.current_node, []):
            if row.get("routing_policy") != "ktrf:deferred":
                continue
            choice_id = row.get("id")
            result_variable = row.get("result_variable")
            if not isinstance(choice_id, str) or not isinstance(result_variable, str):
                raise KtrfRuntimeError("malformed deferred Choice in injected state")
            if choice_id in self._committed_choices:
                continue
            value = self.read_variable(result_variable)
            if any(value == accepted for accepted in self._accepted_choice_values(row)):
                self._committed_choices[choice_id] = copy.deepcopy(value)

    def trigger(self, trigger: str = "ktrf:next") -> TransitionResult | None:
        # Injected School Days states carry choice_result. A valid outcome value
        # denotes a restored post-commit snapshot. The pending sentinel (-2) is
        # not an accepted outcome, so the generic deferred-Choice gate remains
        # active for pre-commit snapshots.
        self._restore_committed_choice_state_from_values()
        return super().trigger(trigger)
