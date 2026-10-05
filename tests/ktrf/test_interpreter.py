#!/usr/bin/env python3
from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INTERPRETER_PATH = ROOT / "tools" / "ktrf" / "interpreter.py"
EXAMPLE_PATH = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"

spec = importlib.util.spec_from_file_location("ktrf_interpreter", INTERPRETER_PATH)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
Interpreter = module.Interpreter
KtrfRuntimeError = module.KtrfRuntimeError


def minimal() -> dict:
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


def deferred_choice_document() -> dict:
    doc = minimal()
    doc["document_id"] = "example.deferred-choice"
    doc["variables"] = [
        {
            "id": "example:var:choice",
            "type": "ktrf:int32",
            "scope": "ktrf:session",
            "default": -2,
        },
        {
            "id": "example:var:feeling",
            "type": "ktrf:int32",
            "scope": "ktrf:session",
            "default": 0,
        },
    ]
    doc["endings"] = []
    doc["expressions"] = [
        {
            "id": "example:expr:selected",
            "op": "ktrf:eq",
            "args": [
                {"kind": "variable", "ref": "example:var:choice"},
                {"kind": "literal", "value": 0},
            ],
            "result_type": "ktrf:bool",
        }
    ]
    doc["effects"] = [
        {
            "id": "example:effect:add-feeling",
            "op": "ktrf:add",
            "args": {
                "target": "example:var:feeling",
                "value": {"kind": "literal", "value": 3},
            },
        }
    ]
    doc["choices"] = [
        {
            "id": "example:choice:start",
            "node": "example:node:start",
            "result_variable": "example:var:choice",
            "routing_policy": "ktrf:deferred",
            "options": [
                {"id": "option-0", "value": 0, "effects": ["example:effect:add-feeling"]}
            ],
            "timeout": {"value": -1, "effects": []},
        }
    ]
    doc["nodes"] = [
        {"id": "example:node:start", "kind": "ktrf:scene", "resources": []},
        {"id": "example:node:next", "kind": "ktrf:scene", "resources": []},
    ]
    doc["transitions"] = [
        {
            "id": "example:t0",
            "source": "example:node:start",
            "destination": "example:node:next",
            "priority": 0,
            "predicate": "example:expr:selected",
            "effects": [],
            "terminal": False,
            "triggers": ["ktrf:next"],
        }
    ]
    return doc


class KtrfInterpreterTests(unittest.TestCase):
    def test_minimal_terminal_transition_registers_ending(self) -> None:
        vm = Interpreter(minimal())
        vm.reset()
        result = vm.trigger()
        self.assertIsNotNone(result)
        assert result is not None
        self.assertTrue(result.terminal)
        self.assertEqual("example:transition:end", result.transition_id)
        self.assertEqual(["example:ending:1"], vm.ending_registrations)
        self.assertTrue(vm.terminal)

    def test_choice_commit_is_deferred(self) -> None:
        vm = Interpreter(deferred_choice_document())
        vm.reset()
        self.assertTrue(vm.commit_choice(0))
        self.assertEqual("example:node:start", vm.current_node)
        self.assertEqual(0, vm.read_variable("example:var:choice"))
        self.assertEqual(3, vm.read_variable("example:var:feeling"))
        result = vm.trigger()
        self.assertIsNotNone(result)
        self.assertEqual("example:node:next", vm.current_node)

    def test_choice_commit_is_idempotent_for_same_result(self) -> None:
        vm = Interpreter(deferred_choice_document())
        vm.reset()
        self.assertTrue(vm.commit_choice(0))
        self.assertTrue(vm.commit_choice(0))
        self.assertEqual(3, vm.read_variable("example:var:feeling"))
        self.assertFalse(vm.commit_choice(-1))
        self.assertEqual(3, vm.read_variable("example:var:feeling"))

    def test_first_matching_priority_wins(self) -> None:
        doc = deferred_choice_document()
        doc["expressions"] = []
        doc["choices"] = []
        doc["transitions"] = [
            {
                "id": "example:t-high",
                "source": "example:node:start",
                "destination": "example:node:next",
                "priority": 10,
                "effects": [],
                "terminal": False,
                "triggers": ["ktrf:next"],
            },
            {
                "id": "example:t-first",
                "source": "example:node:start",
                "destination": "example:node:next",
                "priority": 0,
                "effects": [],
                "terminal": False,
                "triggers": ["ktrf:next"],
            },
        ]
        vm = Interpreter(doc)
        vm.reset()
        result = vm.trigger()
        assert result is not None
        self.assertEqual("example:t-first", result.transition_id)

    def test_effect_order_is_observable(self) -> None:
        doc = deferred_choice_document()
        doc["expressions"] = []
        doc["choices"] = []
        doc["effects"] = [
            {
                "id": "example:set-5",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:feeling",
                    "value": {"kind": "literal", "value": 5},
                },
            },
            {
                "id": "example:add-2",
                "op": "ktrf:add",
                "args": {
                    "target": "example:var:feeling",
                    "value": {"kind": "literal", "value": 2},
                },
            },
        ]
        doc["transitions"] = [
            {
                "id": "example:t0",
                "source": "example:node:start",
                "destination": "example:node:next",
                "priority": 0,
                "effects": ["example:set-5", "example:add-2"],
                "terminal": False,
                "triggers": ["ktrf:next"],
            }
        ]
        vm = Interpreter(doc)
        vm.reset()
        vm.trigger()
        self.assertEqual(7, vm.read_variable("example:var:feeling"))

    def test_trigger_after_terminal_is_rejected(self) -> None:
        vm = Interpreter(minimal())
        vm.reset()
        vm.trigger()
        with self.assertRaises(KtrfRuntimeError):
            vm.trigger()


if __name__ == "__main__":
    unittest.main(verbosity=2)
