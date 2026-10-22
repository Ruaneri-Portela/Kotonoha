#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "ktrf"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import binary_full_v0_1 as full  # noqa: E402


EXAMPLE = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"


def load_example() -> dict:
    return json.loads(EXAMPLE.read_text(encoding="utf-8"))


class FullBinaryV01Tests(unittest.TestCase):
    def test_minimal_full_compile_and_validation(self) -> None:
        doc = load_example()
        data, parsed, decoded, counts = full.compile_document(doc)
        self.assertEqual(
            tuple(entry.type_code for entry in parsed.entries),
            full.CORE_SECTION_TYPES,
        )
        self.assertEqual(counts["TRAN"], 1)
        self.assertEqual(counts["NODE"], 1)
        self.assertEqual(decoded["transitions"][0]["id"], "example:transition:end")

        parsed2, decoded2, counts2 = full.validate_artifact(data)
        self.assertEqual(parsed2.content_sha256, parsed.content_sha256)
        self.assertEqual(decoded2, decoded)
        self.assertEqual(counts2, counts)

    def test_reordering_catalogs_does_not_change_binary(self) -> None:
        doc = load_example()
        second_node = {
            "id": "example:node:zzz",
            "kind": "ktrf:scene",
            "resources": [],
        }
        second_transition = {
            "id": "example:transition:zzz",
            "source": "example:node:zzz",
            "priority": 0,
            "effects": [],
            "terminal": True,
            "triggers": ["ktrf:next"],
        }
        doc["nodes"].append(second_node)
        doc["transitions"].append(second_transition)

        first, *_ = full.compile_document(doc)
        reordered = copy.deepcopy(doc)
        reordered["nodes"].reverse()
        reordered["transitions"].reverse()
        second, *_ = full.compile_document(reordered)
        self.assertEqual(first, second)

    def test_single_byte_corruption_is_rejected(self) -> None:
        data, *_ = full.compile_document(load_example())
        broken = bytearray(data)
        broken[-1] ^= 0x01
        with self.assertRaises(full.KtrfFullError):
            full.validate_artifact(bytes(broken))

    def test_manifest_roundtrip(self) -> None:
        data, parsed, _decoded, counts = full.compile_document(load_example())
        manifest = full.build_manifest(
            data=data,
            parsed=parsed,
            counts=counts,
            artifact_name="example.ktnroute",
            source_ir_name="minimal-routing-ir.json",
            source_ir_sha256="0" * 64,
        )
        full.verify_manifest(data, parsed, counts, manifest)

        bad = copy.deepcopy(manifest)
        bad["sha256"] = "f" * 64
        with self.assertRaisesRegex(full.KtrfFullError, "SHA-256"):
            full.verify_manifest(data, parsed, counts, bad)

    def test_semantic_counts_match_directory(self) -> None:
        data, parsed, decoded, counts = full.compile_document(load_example())
        expected = full.semantic_item_counts(decoded)
        directory = {
            entry.type_code.decode("ascii"): entry.item_count
            for entry in parsed.entries
        }
        for section, wanted in expected.items():
            self.assertEqual(directory[section], wanted)
            self.assertEqual(counts[section], wanted)
        self.assertIn("STRS", directory)
        self.assertGreater(directory["STRS"], 0)
        self.assertEqual(full.sha256_bytes(data), full.sha256_bytes(data))


if __name__ == "__main__":
    unittest.main(verbosity=2)
