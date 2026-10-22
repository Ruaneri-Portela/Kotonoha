#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_semantic_phase5_v0_1.py"
EXAMPLE_PATH = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(spec.name, None)
        raise
    return module


phase5 = load_module("ktrf_binary_semantic_phase5_v0_1", MODULE_PATH)


def load_example():
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


def add_choice(document, *, choice_id="example:choice:start", with_timeout=True):
    row = {
        "id": choice_id,
        "node": "example:node:start",
        "result_variable": "example:var:flag",
        "routing_policy": "ktrf:deferred",
        "options": [
            {
                "id": "option-0",
                "value": 0,
                "effects": ["example:effect:register_ending_1"],
            },
            {"id": "option-1", "value": 1, "effects": []},
        ],
    }
    if with_timeout:
        row["timeout"] = {
            "value": -1,
            "effects": ["example:effect:register_ending_1"],
        }
    document["choices"] = [row]
    return row


class KtrfBinarySemanticPhase5Tests(unittest.TestCase):
    def test_minimal_choice_round_trip_projection(self):
        document = load_example()
        add_choice(document)
        data = phase5.build_phase5_container(document)
        decoded = phase5.parse_phase5_container(data)
        self.assertEqual(phase5.canonical_phase5_projection(document), decoded)

    def test_phase5_sections_are_present_in_canonical_order(self):
        document = load_example()
        add_choice(document)
        data = phase5.build_phase5_container(document)
        parsed = phase5.container.parse_container(
            data, known_section_types=phase5.PHASE5_SECTION_TYPES
        )
        self.assertEqual(
            phase5.PHASE5_SECTION_TYPES,
            tuple(entry.type_code for entry in parsed.entries),
        )
        self.assertEqual(1, next(e for e in parsed.entries if e.type_code == b"CHOI").item_count)

    def test_choice_catalog_is_deterministic_under_source_reordering(self):
        document = load_example()
        first = add_choice(document, choice_id="example:choice:z")
        second = copy.deepcopy(first)
        second["id"] = "example:choice:a"
        document["choices"].append(second)

        shuffled = copy.deepcopy(document)
        shuffled["choices"].reverse()
        self.assertEqual(
            phase5.build_phase5_container(document),
            phase5.build_phase5_container(shuffled),
        )

    def test_option_order_is_semantic_and_preserved(self):
        document = load_example()
        row = add_choice(document)
        row["options"] = [
            {"id": "option-b", "value": 9, "effects": []},
            {"id": "option-a", "value": 3, "effects": []},
        ]
        decoded = phase5.parse_phase5_container(phase5.build_phase5_container(document))
        self.assertEqual(
            ["option-b", "option-a"],
            [option["id"] for option in decoded["choices"][0]["options"]],
        )

    def test_effect_reference_order_is_preserved(self):
        document = load_example()
        document["effects"].append(
            {
                "id": "example:effect:set_flag",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": 7},
                },
            }
        )
        row = add_choice(document)
        row["options"][0]["effects"] = [
            "example:effect:set_flag",
            "example:effect:register_ending_1",
        ]
        decoded = phase5.parse_phase5_container(phase5.build_phase5_container(document))
        self.assertEqual(
            row["options"][0]["effects"],
            decoded["choices"][0]["options"][0]["effects"],
        )

    def test_timeout_absence_is_preserved(self):
        document = load_example()
        add_choice(document, with_timeout=False)
        decoded = phase5.parse_phase5_container(phase5.build_phase5_container(document))
        self.assertNotIn("timeout", decoded["choices"][0])

    def test_unknown_effect_reference_is_rejected(self):
        document = load_example()
        row = add_choice(document)
        row["options"][0]["effects"] = ["example:effect:missing"]
        with self.assertRaisesRegex(phase5.KtrfPhase5Error, "unknown effect"):
            phase5.build_phase5_container(document)

    def test_unknown_result_variable_is_rejected(self):
        document = load_example()
        row = add_choice(document)
        row["result_variable"] = "example:var:missing"
        with self.assertRaisesRegex(phase5.KtrfPhase5Error, "unknown result variable"):
            phase5.build_phase5_container(document)

    def test_unknown_node_is_rejected(self):
        document = load_example()
        row = add_choice(document)
        row["node"] = "example:node:missing"
        with self.assertRaisesRegex(phase5.KtrfPhase5Error, "unknown node"):
            phase5.build_phase5_container(document)

    def test_non_scalar_option_value_is_rejected(self):
        document = load_example()
        row = add_choice(document)
        row["options"][0]["value"] = {"not": "scalar"}
        with self.assertRaisesRegex(phase5.KtrfPhase5Error, "only defines"):
            phase5.build_phase5_container(document)

    def test_duplicate_local_option_id_is_rejected(self):
        document = load_example()
        row = add_choice(document)
        row["options"][1]["id"] = row["options"][0]["id"]
        with self.assertRaisesRegex(phase5.KtrfPhase5Error, "duplicate option id"):
            phase5.build_phase5_container(document)


if __name__ == "__main__":
    unittest.main(verbosity=2)
