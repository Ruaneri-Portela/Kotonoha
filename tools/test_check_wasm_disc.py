#!/usr/bin/env python3
"""Unit tests for the non-downloading WASM disc preflight."""
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_wasm_disc as probe


class Response:
    def __init__(self, status, headers, body=b"x"):
        self.status = status
        self.headers = headers
        self.body = io.BytesIO(body)

    def read(self, count):
        return self.body.read(count)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.body.close()


class CheckWasmDiscTest(unittest.TestCase):
    def setUp(self):
        self.url = "https://example.invalid/Amagami/game-assets/Amagami.bin"

    def fake_open(self, request, timeout):
        if request.get_method() == "HEAD":
            return Response(200, {
                "Content-Length": "4294967296",
                "Accept-Ranges": "bytes",
            }, b"")
        offset = request.get_header("Range")
        self.assertIn(offset, {"bytes=0-0", "bytes=4294967295-4294967295"})
        start = offset.split("=")[1].split("-")[0]
        return Response(206, {
            "Content-Range": f"bytes {start}-{start}/4294967296"
        })

    def test_large_disc_uses_only_two_single_byte_ranges(self):
        with patch.object(probe, "urlopen", side_effect=self.fake_open) as opened:
            self.assertEqual(probe.check_http(self.url), 4294967296)
            self.assertEqual(opened.call_count, 3)
            self.assertEqual(opened.call_args_list[0].args[0].get_method(),
                             "HEAD")

    def test_rejects_ignored_range_without_reading_body(self):
        def ignored(request, timeout):
            if request.get_method() == "HEAD":
                return Response(200, {"Content-Length": "100",
                                      "Accept-Ranges": "bytes"})
            return Response(200, {"Content-Length": "100"}, b"x" * 100)
        with patch.object(probe, "urlopen", side_effect=ignored):
            with self.assertRaisesRegex(probe.DiscCheckError, "not 206"):
                probe.check_http(self.url)

    def test_rejects_missing_range_header(self):
        with patch.object(probe, "urlopen",
                          return_value=Response(200, {"Content-Length": "100"})):
            with self.assertRaisesRegex(probe.DiscCheckError, "Accept-Ranges"):
                probe.check_http(self.url)

    def test_rejects_bad_content_range(self):
        def bad_range(request, timeout):
            if request.get_method() == "HEAD":
                return Response(200, {"Content-Length": "100",
                                      "Accept-Ranges": "bytes"})
            return Response(206, {"Content-Range": "bytes 10-10/100"})
        with patch.object(probe, "urlopen", side_effect=bad_range):
            with self.assertRaisesRegex(probe.DiscCheckError, "Content-Range"):
                probe.check_http(self.url)

    def test_cyclic_symlink_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            link = Path(directory) / "Amagami.bin"
            try:
                link.symlink_to("Amagami.bin")
            except OSError as error:
                if getattr(error, "winerror", None) == 1314:
                    self.skipTest("Windows symlink privilege is unavailable")
                raise
            with self.assertRaisesRegex(probe.DiscCheckError, "symlink"):
                probe.check_local(str(link))

    def test_normal_file_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            real = Path(directory) / "real.bin"
            real.write_bytes(b"example")
            link = Path(directory) / "Amagami.bin"
            try:
                link.symlink_to(real.name)
            except OSError as error:
                if getattr(error, "winerror", None) == 1314:
                    self.skipTest("Windows symlink privilege is unavailable")
                raise
            self.assertEqual(probe.check_local(str(link)), real.resolve())

    def test_rejects_non_http_url(self):
        with self.assertRaises(probe.DiscCheckError):
            probe.check_http("file:///mnt/data2/Games/Amagami.bin")


if __name__ == "__main__":
    unittest.main()
