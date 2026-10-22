#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
import math
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_semantic_phase3_v0_1.py"
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


phase3 = load_module("ktrf_binary_semantic_phase3_v0_1", MODULE_PATH)


def load_example():
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


class KtrfBinarySemanticPhase3Tests(unittest.TestCase):
    def test_minimal_example_round_trip_projection(self):
        document = load_example()
        encoded = phase3.build_phase3_container(document)
        decoded = phase3.parse_phase3_container(encoded)
        self.assertEqual(phase3.canonical_phase3_projection(document), decoded)

    def test_phase3_sections_are_present_in_canonical_order(self):
        document = load_example()
        data = phase3.build_phase3_container(document)
        parsed = phase3.container.parse_container(
            data,
            known_section_types=phase3.PHASE3_SECTION_TYPES,
        )
        self.assertEqual(
            phase3.PHASE3_SECTION_TYPES,
            tuple(entry.type_code for entry in parsed.entries),
        )
        expr = next(e for e in parsed.entries if e.type_code == b"EXPR")
        self.assertEqual(len(document["expressions"]), expr.item_count)

    def test_expression_indices_are_canonical_by_id(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:z",
                "op": "ktrf:eq",
                "args": [
                    {"kind": "variable", "ref": "example:var:flag"},
                    {"kind": "literal", "value": 0},
                ],
                "result_type": "ktrf:bool",
            },
            {
                "id": "example:expr:a",
                "op": "ktrf:ne",
                "args": [
                    {"kind": "variable", "ref": "example:var:flag"},
                    {"kind": "literal", "value": 1},
                ],
                "result_type": "ktrf:bool",
            },
        ]
        self.assertEqual(
            {"example:expr:a": 0, "example:expr:z": 1},
            phase3.expression_index_map(document),
        )

    def test_deterministic_under_expression_source_reordering(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:z",
                "op": "ktrf:eq",
                "args": [
                    {"kind": "variable", "ref": "example:var:flag"},
                    {"kind": "literal", "value": 0},
                ],
                "result_type": "ktrf:bool",
            },
            {
                "id": "example:expr:a",
                "op": "ktrf:ne",
                "args": [
                    {"kind": "variable", "ref": "example:var:flag"},
                    {"kind": "literal", "value": 1},
                ],
                "result_type": "ktrf:bool",
            },
        ]

        shuffled = copy.deepcopy(document)
        shuffled["expressions"].reverse()

        self.assertEqual(
            phase3.build_phase3_container(document),
            phase3.build_phase3_container(shuffled),
        )

    def test_argument_order_is_preserved(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:ordered",
                "op": "example:ordered-op",
                "args": [
                    {"kind": "literal", "value": 7},
                    {"kind": "literal", "value": 3},
                    {"kind": "variable", "ref": "example:var:flag"},
                ],
            }
        ]

        decoded = phase3.parse_phase3_container(
            phase3.build_phase3_container(document)
        )
        self.assertEqual(
            [
                {"kind": "literal", "value": 7},
                {"kind": "literal", "value": 3},
                {"kind": "variable", "ref": "example:var:flag"},
            ],
            decoded["expressions"][0]["args"],
        )

    def test_all_phase3_literal_scalar_kinds_round_trip(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:literals",
                "op": "example:pack",
                "args": [
                    {"kind": "literal", "value": None},
                    {"kind": "literal", "value": True},
                    {"kind": "literal", "value": -9223372036854775808},
                    {"kind": "literal", "value": 1.25},
                    {"kind": "literal", "value": "ação/学校"},
                ],
            }
        ]

        decoded = phase3.parse_phase3_container(
            phase3.build_phase3_container(document)
        )
        values = [arg["value"] for arg in decoded["expressions"][0]["args"]]
        self.assertIsNone(values[0])
        self.assertIs(values[1], True)
        self.assertEqual(-9223372036854775808, values[2])
        self.assertTrue(math.isclose(1.25, values[3], rel_tol=0, abs_tol=1e-12))
        self.assertEqual("ação/学校", values[4])

    def test_expression_reference_round_trip_uses_stable_id(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:base",
                "op": "ktrf:eq",
                "args": [
                    {"kind": "variable", "ref": "example:var:flag"},
                    {"kind": "literal", "value": 0},
                ],
                "result_type": "ktrf:bool",
            },
            {
                "id": "example:expr:outer",
                "op": "ktrf:eq",
                "args": [
                    {"kind": "expression", "ref": "example:expr:base"},
                    {"kind": "literal", "value": True},
                ],
                "result_type": "ktrf:bool",
            },
        ]

        decoded = phase3.parse_phase3_container(
            phase3.build_phase3_container(document)
        )
        by_id = {row["id"]: row for row in decoded["expressions"]}
        self.assertEqual(
            {"kind": "expression", "ref": "example:expr:base"},
            by_id["example:expr:outer"]["args"][0],
        )

    def test_ending_entity_reference_round_trip(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:ending-ref",
                "op": "example:is-ending",
                "args": [
                    {
                        "kind": "entity",
                        "entity": "ending",
                        "ref": "example:ending:1",
                    }
                ],
                "result_type": "ktrf:bool",
            }
        ]

        decoded = phase3.parse_phase3_container(
            phase3.build_phase3_container(document)
        )
        self.assertEqual(
            {
                "kind": "entity",
                "entity": "ending",
                "ref": "example:ending:1",
            },
            decoded["expressions"][0]["args"][0],
        )

    def test_unknown_variable_reference_is_rejected(self):
        document = load_example()
        document["expressions"][0]["args"][0] = {
            "kind": "variable",
            "ref": "example:var:missing",
        }
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "unknown variable"):
            phase3.build_phase3_container(document)

    def test_unknown_expression_reference_is_rejected(self):
        document = load_example()
        document["expressions"][0]["args"][0] = {
            "kind": "expression",
            "ref": "example:expr:missing",
        }
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "unknown expression"):
            phase3.build_phase3_container(document)

    def test_expression_cycle_is_rejected(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:a",
                "op": "example:identity",
                "args": [{"kind": "expression", "ref": "example:expr:b"}],
            },
            {
                "id": "example:expr:b",
                "op": "example:identity",
                "args": [{"kind": "expression", "ref": "example:expr:a"}],
            },
        ]
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "cycle"):
            phase3.build_phase3_container(document)

    def test_unknown_entity_kind_is_deferred_not_guessed(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:node-ref",
                "op": "example:identity",
                "args": [
                    {
                        "kind": "entity",
                        "entity": "node",
                        "ref": "example:node:start",
                    }
                ],
            }
        ]
        with self.assertRaisesRegex(
            phase3.KtrfPhase3Error, "does not define entity reference kind"
        ):
            phase3.build_phase3_container(document)

    def test_unknown_ending_entity_id_is_rejected(self):
        document = load_example()
        document["expressions"] = [
            {
                "id": "example:expr:ending-ref",
                "op": "example:identity",
                "args": [
                    {
                        "kind": "entity",
                        "entity": "ending",
                        "ref": "example:ending:missing",
                    }
                ],
            }
        ]
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "unknown ending entity"):
            phase3.build_phase3_container(document)

    def test_container_literal_is_deferred_not_guessed(self):
        document = load_example()
        document["expressions"][0]["args"][1] = {
            "kind": "literal",
            "value": {"x": 1},
        }
        with self.assertRaisesRegex(
            phase3.KtrfPhase3Error, "only defines null/bool/int64/float64/string"
        ):
            phase3.build_phase3_container(document)

    def test_literal_int64_range_is_strict(self):
        document = load_example()
        document["expressions"][0]["args"][1] = {
            "kind": "literal",
            "value": 9223372036854775808,
        }
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "signed int64 range"):
            phase3.build_phase3_container(document)

    def test_literal_float_must_be_finite(self):
        document = load_example()
        document["expressions"][0]["args"][1] = {
            "kind": "literal",
            "value": float("inf"),
        }
        with self.assertRaisesRegex(phase3.KtrfPhase3Error, "must be finite"):
            phase3.build_phase3_container(document)


if __name__ == "__main__":
    unittest.main(verbosity=2)
