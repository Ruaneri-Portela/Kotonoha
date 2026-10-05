#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ktrf" / "binary_container_v0_1.py"


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


ktrf = load_module("ktrf_binary_container_v0_1", MODULE_PATH)


class KtrfBinaryContainerTests(unittest.TestCase):
    def test_header_layout_and_magic(self):
        data = ktrf.build_container([])
        self.assertEqual(96, len(data))
        self.assertEqual(b"KTRF", data[:4])
        self.assertEqual(0, struct.unpack_from("<H", data, 4)[0])
        self.assertEqual(1, struct.unpack_from("<H", data, 6)[0])
        self.assertEqual(96, struct.unpack_from("<H", data, 8)[0])
        self.assertEqual(48, struct.unpack_from("<H", data, 10)[0])
        self.assertEqual(96, struct.unpack_from("<Q", data, 24)[0])
        self.assertEqual(96, struct.unpack_from("<Q", data, 32)[0])

    def test_round_trip_sections(self):
        sections = [
            ktrf.Section(b"NODE", b"nodes", item_count=1),
            ktrf.Section(b"META", b"meta"),
            ktrf.Section(b"STRS", b"strings", item_count=2),
        ]
        data = ktrf.build_container(sections)
        parsed = ktrf.parse_container(
            data,
            known_section_types=[b"META", b"STRS", b"NODE"],
        )
        self.assertEqual((b"META", b"STRS", b"NODE"), tuple(e.type_code for e in parsed.entries))
        self.assertEqual(b"meta", parsed.sections[b"META"])
        self.assertEqual(b"strings", parsed.sections[b"STRS"])
        self.assertEqual(b"nodes", parsed.sections[b"NODE"])

    def test_build_is_deterministic_independent_of_input_order(self):
        a = ktrf.build_container(
            [
                ktrf.Section(b"TRAN", b"t"),
                ktrf.Section(b"META", b"m"),
                ktrf.Section(b"NODE", b"n"),
            ]
        )
        b = ktrf.build_container(
            [
                ktrf.Section(b"NODE", b"n"),
                ktrf.Section(b"TRAN", b"t"),
                ktrf.Section(b"META", b"m"),
            ]
        )
        self.assertEqual(a, b)

    def test_content_hash_detects_payload_mutation(self):
        data = bytearray(ktrf.build_container([ktrf.Section(b"META", b"abcd")]))
        data[-1] ^= 0x01
        with self.assertRaisesRegex(ktrf.KtrfBinaryError, "content SHA-256 mismatch"):
            ktrf.parse_container(bytes(data), known_section_types=[b"META"])

    def test_header_crc_detects_header_mutation(self):
        data = bytearray(ktrf.build_container([]))
        data[12] ^= 0x01
        with self.assertRaises(ktrf.KtrfBinaryError):
            ktrf.parse_container(bytes(data))

    def test_unknown_required_section_is_rejected(self):
        data = ktrf.build_container([ktrf.Section(b"ZZZZ", b"x", flags=ktrf.SECTION_REQUIRED)])
        with self.assertRaisesRegex(ktrf.KtrfBinaryError, "unknown required section"):
            ktrf.parse_container(data, known_section_types=[b"META"])

    def test_unknown_optional_section_can_be_skipped(self):
        data = ktrf.build_container([ktrf.Section(b"ZZZZ", b"x", flags=0)])
        parsed = ktrf.parse_container(data, known_section_types=[b"META"])
        self.assertEqual(b"x", parsed.sections[b"ZZZZ"])

    def test_compression_flag_is_rejected_in_v0_1(self):
        with self.assertRaisesRegex(ktrf.KtrfBinaryError, "compression is not standardized"):
            ktrf.build_container(
                [ktrf.Section(b"META", b"x", flags=ktrf.SECTION_REQUIRED | ktrf.SECTION_COMPRESSED)]
            )

    def test_strs_is_deterministic_and_round_trips_utf8(self):
        payload_a, mapping_a = ktrf.encode_strs(["Kotonoha", "ação", "", "Kotonoha", "学校"])
        payload_b, mapping_b = ktrf.encode_strs(["学校", "Kotonoha", "ação"])
        self.assertEqual(payload_a, payload_b)
        self.assertEqual(mapping_a, mapping_b)
        values = ktrf.decode_strs(payload_a)
        self.assertEqual("", values[0])
        self.assertEqual(set(["", "Kotonoha", "ação", "学校"]), set(values))

    def test_strs_rejects_embedded_nul(self):
        with self.assertRaisesRegex(ktrf.KtrfBinaryError, "U\+0000"):
            ktrf.encode_strs(["bad\x00string"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
