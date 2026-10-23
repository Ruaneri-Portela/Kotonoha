#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_semantic_phase4_v0_1.py"
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


phase4 = load_module("ktrf_binary_semantic_phase4_v0_1", MODULE_PATH)


def load_example():
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


class KtrfBinarySemanticPhase4Tests(unittest.TestCase):
    def test_minimal_example_round_trip_projection(self):
        document = load_example()
        data = phase4.build_phase4_container(document)
        decoded = phase4.parse_phase4_container(data)
        self.assertEqual(phase4.canonical_phase4_projection(document), decoded)

    def test_phase4_sections_are_present_in_canonical_order(self):
        document = load_example()
        data = phase4.build_phase4_container(document)
        parsed = phase4.container.parse_container(
            data, known_section_types=phase4.PHASE4_SECTION_TYPES
        )
        self.assertEqual(
            phase4.PHASE4_SECTION_TYPES,
            tuple(entry.type_code for entry in parsed.entries),
        )
        self.assertEqual(
            1, next(e for e in parsed.entries if e.type_code == b"EFFT").item_count
        )

    def test_all_supported_effect_shapes_round_trip(self):
        document = load_example()
        document["variables"].append(
            {
                "id": "example:var:other",
                "type": "ktrf:int32",
                "scope": "ktrf:session",
                "default": 3,
            }
        )
        document["external_hooks"] = [
            {
                "id": "example:hook:test",
                "symbol": "example:test-hook",
                "contract": "example.test/1.0.0",
            }
        ]
        document["effects"] = [
            {
                "id": "example:effect:set",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": -4},
                },
            },
            {
                "id": "example:effect:add",
                "op": "ktrf:add",
                "args": {
                    "target": "example:var:flag",
                    "value": {
                        "kind": "expression",
                        "ref": "example:expr:flag_eq_0",
                    },
                },
            },
            {
                "id": "example:effect:copy",
                "op": "ktrf:copy",
                "args": {
                    "target": "example:var:other",
                    "source": "example:var:flag",
                },
            },
            {
                "id": "example:effect:ending",
                "op": "ktrf:register-ending",
                "args": {"ending": "example:ending:1"},
            },
            {
                "id": "example:effect:hook",
                "op": "ktrf:call-hook",
                "args": {
                    "hook": "example:hook:test",
                    "arguments": [
                        {"kind": "literal", "value": "hello"},
                        {"kind": "variable", "ref": "example:var:flag"},
                        {
                            "kind": "entity",
                            "entity": "ending",
                            "ref": "example:ending:1",
                        },
                    ],
                },
            },
        ]

        decoded = phase4.parse_phase4_container(phase4.build_phase4_container(document))
        by_id = {row["id"]: row for row in decoded["effects"]}
        self.assertEqual(-4, by_id["example:effect:set"]["args"]["value"]["value"])
        self.assertEqual(
            "example:expr:flag_eq_0",
            by_id["example:effect:add"]["args"]["value"]["ref"],
        )
        self.assertEqual(
            "example:var:flag", by_id["example:effect:copy"]["args"]["source"]
        )
        self.assertEqual(
            "example:ending:1", by_id["example:effect:ending"]["args"]["ending"]
        )
        hook_args = by_id["example:effect:hook"]["args"]["arguments"]
        self.assertEqual("hello", hook_args[0]["value"])
        self.assertEqual("example:var:flag", hook_args[1]["ref"])
        self.assertEqual("example:ending:1", hook_args[2]["ref"])

    def test_call_hook_argument_order_is_semantic(self):
        document = load_example()
        document["external_hooks"] = [
            {
                "id": "example:hook:test",
                "symbol": "example:test-hook",
                "contract": "example.test/1.0.0",
            }
        ]
        document["effects"] = [
            {
                "id": "example:effect:hook",
                "op": "ktrf:call-hook",
                "args": {
                    "hook": "example:hook:test",
                    "arguments": [
                        {"kind": "literal", "value": 3},
                        {"kind": "literal", "value": 1},
                        {"kind": "literal", "value": 2},
                    ],
                },
            }
        ]
        decoded = phase4.parse_phase4_container(phase4.build_phase4_container(document))
        self.assertEqual(
            [3, 1, 2],
            [x["value"] for x in decoded["effects"][0]["args"]["arguments"]],
        )

    def test_effect_catalog_is_deterministic_under_source_reordering(self):
        document = load_example()
        document["effects"] += [
            {
                "id": "example:effect:z",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": 2},
                },
            },
            {
                "id": "example:effect:a",
                "op": "ktrf:add",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": 1},
                },
            },
        ]
        shuffled = copy.deepcopy(document)
        shuffled["effects"].reverse()
        self.assertEqual(
            phase4.build_phase4_container(document),
            phase4.build_phase4_container(shuffled),
        )

    def test_unknown_operator_is_rejected(self):
        document = load_example()
        document["effects"] = [
            {"id": "example:effect:x", "op": "example:magic", "args": {}}
        ]
        with self.assertRaisesRegex(phase4.KtrfPhase4Error, "does not define Effect operator"):
            phase4.build_phase4_container(document)

    def test_unknown_target_variable_is_rejected(self):
        document = load_example()
        document["effects"] = [
            {
                "id": "example:effect:x",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:missing",
                    "value": {"kind": "literal", "value": 1},
                },
            }
        ]
        with self.assertRaisesRegex(phase4.KtrfPhase4Error, "unknown target variable"):
            phase4.build_phase4_container(document)

    def test_unknown_hook_is_rejected(self):
        document = load_example()
        document["effects"] = [
            {
                "id": "example:effect:x",
                "op": "ktrf:call-hook",
                "args": {"hook": "example:hook:missing", "arguments": []},
            }
        ]
        with self.assertRaisesRegex(phase4.KtrfPhase4Error, "unknown hook"):
            phase4.build_phase4_container(document)

    def test_extra_core_args_are_not_silently_dropped(self):
        document = load_example()
        document["effects"] = [
            {
                "id": "example:effect:x",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": 1},
                    "mystery": 7,
                },
            }
        ]
        with self.assertRaisesRegex(phase4.KtrfPhase4Error, "args must contain exactly"):
            phase4.build_phase4_container(document)

    def test_string_value_is_interned(self):
        document = load_example()
        document["effects"] = [
            {
                "id": "example:effect:text",
                "op": "ktrf:set",
                "args": {
                    "target": "example:var:flag",
                    "value": {"kind": "literal", "value": "efeito/効果"},
                },
            }
        ]
        data = phase4.build_phase4_container(document)
        parsed = phase4.container.parse_container(
            data, known_section_types=phase4.PHASE4_SECTION_TYPES
        )
        strings = phase4.container.decode_strs(parsed.sections[b"STRS"])
        self.assertIn("efeito/効果", strings)


if __name__ == "__main__":
    unittest.main(verbosity=2)
