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
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_semantic_phase2_v0_1.py"
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


phase2 = load_module("ktrf_binary_semantic_phase2_v0_1", MODULE_PATH)


def load_example():
    return json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))


class KtrfBinarySemanticPhase2Tests(unittest.TestCase):
    def test_minimal_example_round_trip_projection(self):
        document = load_example()
        encoded = phase2.build_phase2_container(document)
        decoded = phase2.parse_phase2_container(encoded)
        self.assertEqual(phase2.canonical_phase2_projection(document), decoded)

    def test_phase2_sections_are_present_in_canonical_order(self):
        data = phase2.build_phase2_container(load_example())
        parsed = phase2.container.parse_container(
            data,
            known_section_types=phase2.PHASE2_SECTION_TYPES,
        )
        self.assertEqual(
            phase2.PHASE2_SECTION_TYPES,
            tuple(entry.type_code for entry in parsed.entries),
        )
        self.assertEqual(
            1,
            next(e for e in parsed.entries if e.type_code == b"ENDG").item_count,
        )

    def test_catalog_indices_are_canonical_zero_based_positions(self):
        document = load_example()
        document["resource_locators"] = [
            {"id": "example:resource:z", "scheme": "example:path", "value": "z"},
            {"id": "example:resource:a", "scheme": "example:path", "value": "a"},
        ]
        document["external_hooks"] = [
            {
                "id": "example:hook:z",
                "symbol": "example:z",
                "contract": "example/1.0.0",
            },
            {
                "id": "example:hook:a",
                "symbol": "example:a",
                "contract": "example/1.0.0",
            },
        ]
        document["endings"] = [
            {"id": "example:ending:z", "code": 9},
            {"id": "example:ending:a", "code": 1},
        ]

        indices = phase2.catalog_index_maps(document)
        self.assertEqual(
            {"example:resource:a": 0, "example:resource:z": 1},
            indices["RSRC"],
        )
        self.assertEqual(
            {"example:hook:a": 0, "example:hook:z": 1},
            indices["HOOK"],
        )
        self.assertEqual(
            {"example:ending:a": 0, "example:ending:z": 1},
            indices["ENDG"],
        )

    def test_deterministic_under_catalog_source_reordering(self):
        document = load_example()
        document["resource_locators"] = [
            {
                "id": "example:resource:z",
                "scheme": "example:path",
                "value": "z",
                "required": True,
            },
            {
                "id": "example:resource:a",
                "scheme": "example:path",
                "value": "a",
                "required": False,
            },
        ]
        document["external_hooks"] = [
            {
                "id": "example:hook:z",
                "symbol": "example:z",
                "contract": "example/1.0.0",
            },
            {
                "id": "example:hook:a",
                "symbol": "example:a",
                "contract": "example/1.0.0",
            },
        ]
        document["endings"] = [
            {"id": "example:ending:z", "code": "z"},
            {"id": "example:ending:a", "code": "a"},
        ]

        shuffled = copy.deepcopy(document)
        shuffled["resource_locators"].reverse()
        shuffled["external_hooks"].reverse()
        shuffled["endings"].reverse()

        self.assertEqual(
            phase2.build_phase2_container(document),
            phase2.build_phase2_container(shuffled),
        )

    def test_resource_optional_presence_round_trips(self):
        document = load_example()
        document["resource_locators"] = [
            {
                "id": "example:resource:absent",
                "scheme": "example:path",
                "value": "a",
            },
            {
                "id": "example:resource:false",
                "scheme": "example:path",
                "value": "b",
                "required": False,
            },
            {
                "id": "example:resource:true",
                "scheme": "example:path",
                "value": "c",
                "media_type": "application/octet-stream",
                "required": True,
            },
        ]
        decoded = phase2.parse_phase2_container(
            phase2.build_phase2_container(document)
        )
        by_id = {row["id"]: row for row in decoded["resource_locators"]}

        self.assertNotIn("required", by_id["example:resource:absent"])
        self.assertIs(by_id["example:resource:false"]["required"], False)
        self.assertIs(by_id["example:resource:true"]["required"], True)
        self.assertEqual(
            "application/octet-stream",
            by_id["example:resource:true"]["media_type"],
        )

    def test_non_string_resource_value_is_deferred_not_guessed(self):
        document = load_example()
        document["resource_locators"] = [
            {
                "id": "example:resource:object",
                "scheme": "example:opaque",
                "value": {"path": "x"},
            }
        ]
        with self.assertRaisesRegex(
            phase2.KtrfPhase2Error,
            "only defines string ResourceLocator.value",
        ):
            phase2.build_phase2_container(document)

    def test_hook_arguments_schema_is_deferred_not_guessed(self):
        document = load_example()
        document["external_hooks"] = [
            {
                "id": "example:hook:test",
                "symbol": "example:test",
                "contract": "example/1.0.0",
                "arguments_schema": {"type": "array"},
            }
        ]
        with self.assertRaisesRegex(
            phase2.KtrfPhase2Error,
            "does not define ExternalHook.arguments_schema",
        ):
            phase2.build_phase2_container(document)

    def test_all_supported_ending_code_scalars_round_trip(self):
        document = load_example()
        document["endings"] = [
            {"id": "example:ending:absent", "label": "Absent"},
            {"id": "example:ending:null", "code": None},
            {"id": "example:ending:false", "code": False},
            {"id": "example:ending:true", "code": True},
            {"id": "example:ending:min", "code": -9223372036854775808},
            {"id": "example:ending:max", "code": 9223372036854775807},
            {"id": "example:ending:float", "code": 3.5},
            {"id": "example:ending:string", "code": "good-end"},
        ]
        decoded = phase2.parse_phase2_container(
            phase2.build_phase2_container(document)
        )
        by_id = {row["id"]: row for row in decoded["endings"]}

        self.assertNotIn("code", by_id["example:ending:absent"])
        self.assertIsNone(by_id["example:ending:null"]["code"])
        self.assertIs(by_id["example:ending:false"]["code"], False)
        self.assertIs(by_id["example:ending:true"]["code"], True)
        self.assertEqual(-9223372036854775808, by_id["example:ending:min"]["code"])
        self.assertEqual(9223372036854775807, by_id["example:ending:max"]["code"])
        self.assertTrue(
            math.isclose(
                3.5,
                by_id["example:ending:float"]["code"],
                rel_tol=0,
                abs_tol=0,
            )
        )
        self.assertEqual("good-end", by_id["example:ending:string"]["code"])
        self.assertEqual("Absent", by_id["example:ending:absent"]["label"])

    def test_ending_integer_range_is_strict(self):
        document = load_example()
        document["endings"] = [
            {"id": "example:ending:too-large", "code": 9223372036854775808}
        ]
        with self.assertRaisesRegex(
            phase2.KtrfPhase2Error,
            "signed int64 range",
        ):
            phase2.build_phase2_container(document)

    def test_ending_nonfinite_float_is_rejected(self):
        document = load_example()
        document["endings"] = [
            {"id": "example:ending:nan", "code": float("nan")}
        ]
        with self.assertRaisesRegex(
            phase2.KtrfPhase2Error,
            "must be finite",
        ):
            phase2.build_phase2_container(document)

    def test_duplicate_catalog_id_is_rejected(self):
        document = load_example()
        document["endings"] = [
            {"id": "example:ending:dup", "code": 1},
            {"id": "example:ending:dup", "code": 2},
        ]
        with self.assertRaisesRegex(
            phase2.KtrfPhase2Error,
            "duplicate Ending id",
        ):
            phase2.build_phase2_container(document)


if __name__ == "__main__":
    unittest.main(verbosity=2)
