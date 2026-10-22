#!/usr/bin/env python3

from __future__ import annotations

import base64
import copy
import importlib.util
import json
import math
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_semantic_phase1_v0_1.py"
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


phase1 = load_module("ktrf_binary_semantic_phase1_v0_1", MODULE_PATH)


def load_example():
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


class KtrfBinarySemanticPhase1Tests(unittest.TestCase):
    def test_minimal_example_round_trip_projection(self):
        document = load_example()
        encoded = phase1.build_phase1_container(document)
        decoded = phase1.parse_phase1_container(encoded)
        self.assertEqual(phase1.canonical_phase1_projection(document), decoded)

    def test_phase1_sections_are_present_in_canonical_order(self):
        document = load_example()
        data = phase1.build_phase1_container(document)
        parsed = phase1.container.parse_container(
            data,
            known_section_types=phase1.PHASE1_SECTION_TYPES,
        )
        self.assertEqual(
            phase1.PHASE1_SECTION_TYPES,
            tuple(entry.type_code for entry in parsed.entries),
        )
        self.assertEqual(1, next(e for e in parsed.entries if e.type_code == b"META").item_count)

    def test_deterministic_under_source_array_reordering(self):
        document = load_example()
        document["namespaces"] += [
            {"prefix": "zeta", "uri": "urn:test:zeta"},
            {"prefix": "alpha", "uri": "urn:test:alpha"},
        ]
        document["features"]["optional"] = [
            {"id": "example:z", "version": "1.0.0"},
            {"id": "example:a", "version": "1.0.0"},
        ]
        document["entry_points"] += [
            {"id": "example:entry:z", "node": "example:node:start", "trigger": "ktrf:new-game"},
            {"id": "example:entry:a", "node": "example:node:start", "trigger": "ktrf:new-game"},
        ]
        document["variables"] += [
            {"id": "example:var:z", "type": "ktrf:uint32", "scope": "ktrf:session", "default": 9},
            {"id": "example:var:a", "type": "ktrf:bool", "scope": "ktrf:global", "default": True},
        ]

        shuffled = copy.deepcopy(document)
        shuffled["namespaces"].reverse()
        shuffled["features"]["required"].reverse()
        shuffled["features"]["optional"].reverse()
        shuffled["entry_points"].reverse()
        shuffled["variables"].reverse()

        self.assertEqual(
            phase1.build_phase1_container(document),
            phase1.build_phase1_container(shuffled),
        )

    def test_all_core_variable_defaults_round_trip(self):
        document = load_example()
        document["variables"] = [
            {"id": "v:bool", "type": "ktrf:bool", "scope": "ktrf:session", "default": True},
            {"id": "v:i32", "type": "ktrf:int32", "scope": "ktrf:session", "default": -2147483648},
            {"id": "v:u32", "type": "ktrf:uint32", "scope": "ktrf:global", "default": 4294967295},
            {"id": "v:f32", "type": "ktrf:float32", "scope": "ktrf:session", "default": 1.25},
            {"id": "v:f64", "type": "ktrf:float64", "scope": "ktrf:session", "default": -0.125},
            {"id": "v:str", "type": "ktrf:string", "scope": "ktrf:global", "default": "ação/学校"},
            {"id": "v:bytes", "type": "ktrf:bytes", "scope": "ktrf:global", "default": base64.b64encode(b"\x00\x01KTRF\xff").decode("ascii")},
        ]
        decoded = phase1.parse_phase1_container(phase1.build_phase1_container(document))
        by_id = {row["id"]: row for row in decoded["variables"]}
        self.assertIs(by_id["v:bool"]["default"], True)
        self.assertEqual(-2147483648, by_id["v:i32"]["default"])
        self.assertEqual(4294967295, by_id["v:u32"]["default"])
        self.assertTrue(math.isclose(1.25, by_id["v:f32"]["default"], rel_tol=0, abs_tol=1e-7))
        self.assertEqual(-0.125, by_id["v:f64"]["default"])
        self.assertEqual("ação/学校", by_id["v:str"]["default"])
        self.assertEqual(document["variables"][-1]["default"], by_id["v:bytes"]["default"])

    def test_int32_range_is_strict(self):
        document = load_example()
        document["variables"][0]["default"] = 2147483648
        with self.assertRaisesRegex(phase1.KtrfPhase1Error, "int32 default is out of range"):
            phase1.build_phase1_container(document)

    def test_bytes_default_requires_canonical_base64(self):
        document = load_example()
        document["variables"] = [
            {"id": "v:bytes", "type": "ktrf:bytes", "scope": "ktrf:session", "default": "YQ"}
        ]
        with self.assertRaisesRegex(phase1.KtrfPhase1Error, "canonical base64"):
            phase1.build_phase1_container(document)

    def test_custom_variable_type_is_deferred_not_guessed(self):
        document = load_example()
        document["variables"] = [
            {"id": "v:custom", "type": "example:opaque", "scope": "ktrf:session", "default": 1}
        ]
        with self.assertRaisesRegex(phase1.KtrfPhase1Error, "does not define defaults"):
            phase1.build_phase1_container(document)

    def test_string_default_is_interned(self):
        document = load_example()
        document["variables"] = [
            {"id": "v:text", "type": "ktrf:string", "scope": "ktrf:session", "default": "hello"}
        ]
        data = phase1.build_phase1_container(document)
        parsed = phase1.container.parse_container(data, known_section_types=phase1.PHASE1_SECTION_TYPES)
        strings = phase1.container.decode_strs(parsed.sections[b"STRS"])
        self.assertIn("hello", strings)


if __name__ == "__main__":
    unittest.main(verbosity=2)
