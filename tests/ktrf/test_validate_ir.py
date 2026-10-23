#!/usr/bin/env python3
from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = ROOT / "tools" / "ktrf" / "validate_ir.py"
EXAMPLE_PATH = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"

spec = importlib.util.spec_from_file_location("ktrf_validate_ir", VALIDATOR_PATH)
assert spec is not None and spec.loader is not None
validator_module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = validator_module
spec.loader.exec_module(validator_module)
Validator = validator_module.Validator


def load_example() -> dict:
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


def error_codes(document: dict) -> set[str]:
    return {
        d.code
        for d in Validator(document).validate()
        if d.severity == "error"
    }


class KtrfSemanticValidatorTests(unittest.TestCase):
    def test_minimal_example_passes(self) -> None:
        diagnostics = Validator(load_example()).validate()
        self.assertEqual([], [d for d in diagnostics if d.severity == "error"])

    def test_duplicate_id_fails(self) -> None:
        doc = load_example()
        doc["variables"].append(copy.deepcopy(doc["variables"][0]))
        self.assertIn("KTRF1001", error_codes(doc))

    def test_missing_cross_reference_fails(self) -> None:
        doc = load_example()
        doc["transitions"][0]["source"] = "example:node:missing"
        self.assertIn("KTRF1301", error_codes(doc))

    def test_expression_cycle_fails(self) -> None:
        doc = load_example()
        doc["expressions"] = [
            {
                "id": "example:expr:a",
                "op": "ktrf:not",
                "args": [{"kind": "expression", "ref": "example:expr:b"}],
                "result_type": "ktrf:bool",
            },
            {
                "id": "example:expr:b",
                "op": "ktrf:not",
                "args": [{"kind": "expression", "ref": "example:expr:a"}],
                "result_type": "ktrf:bool",
            },
        ]
        doc["transitions"][0]["predicate"] = "example:expr:a"
        self.assertIn("KTRF1520", error_codes(doc))

    def test_terminal_destination_is_forbidden(self) -> None:
        doc = load_example()
        doc["transitions"][0]["destination"] = "example:node:start"
        self.assertIn("KTRF1801", error_codes(doc))

    def test_nonterminal_destination_is_required(self) -> None:
        doc = load_example()
        doc["transitions"][0]["terminal"] = False
        self.assertIn("KTRF1802", error_codes(doc))

    def test_priority_collision_fails(self) -> None:
        doc = load_example()
        second = copy.deepcopy(doc["transitions"][0])
        second["id"] = "example:transition:end2"
        doc["transitions"].append(second)
        self.assertIn("KTRF1805", error_codes(doc))

    def test_undeclared_namespace_fails(self) -> None:
        doc = load_example()
        doc["nodes"][0]["kind"] = "vendor.unknown:scene"
        self.assertIn("KTRF1105", error_codes(doc))

    def test_choice_value_type_mismatch_fails(self) -> None:
        doc = load_example()
        doc["choices"] = [
            {
                "id": "example:choice:test",
                "node": "example:node:start",
                "result_variable": "example:var:flag",
                "routing_policy": "ktrf:deferred",
                "options": [
                    {
                        "id": "yes",
                        "value": "not-an-int",
                        "effects": [],
                    }
                ],
            }
        ]
        self.assertIn("KTRF1703", error_codes(doc))

    def test_namespaced_operator_requires_required_feature(self) -> None:
        doc = load_example()
        doc["expressions"][0]["op"] = "example:custom-predicate"
        self.assertIn("KTRF1504", error_codes(doc))

        doc["features"]["required"].append(
            {"id": "example:custom-predicate", "version": "1.0.0"}
        )
        self.assertNotIn("KTRF1504", error_codes(doc))

    def test_capability_enforcement_rejects_unsupported_required_feature(self) -> None:
        doc = load_example()
        doc["features"]["required"].append(
            {"id": "example:custom-predicate", "version": "1.0.0"}
        )
        diagnostics = Validator(doc, enforce_capabilities=True).validate()
        self.assertIn(
            "KTRF1203",
            {d.code for d in diagnostics if d.severity == "error"},
        )

    def test_capability_enforcement_accepts_declared_support(self) -> None:
        doc = load_example()
        doc["features"]["required"].append(
            {"id": "example:custom-predicate", "version": "1.0.0"}
        )
        diagnostics = Validator(
            doc,
            enforce_capabilities=True,
            supported_features={"example:custom-predicate": "1.2.0"},
        ).validate()
        self.assertNotIn(
            "KTRF1203",
            {d.code for d in diagnostics if d.severity == "error"},
        )
        self.assertNotIn(
            "KTRF1204",
            {d.code for d in diagnostics if d.severity == "error"},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
