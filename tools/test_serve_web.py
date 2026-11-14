#!/usr/bin/env python3
"""Unit tests for the Kotonoha local HTTP Range server (stdlib only)."""

from __future__ import annotations

import http.client
import tempfile
import threading
import unittest
from functools import partial
from http.server import ThreadingHTTPServer
from pathlib import Path

from serve_web import RangeRequestHandler


class RangeServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.size = 320 * 1024 * 1024  # sparse GPK, no 320 MiB RAM required
        with (cls.root / "event01.gpk").open("wb") as file:
            file.write(b"GPK!")
            file.seek(cls.size - 8)
            file.write(b"GPKTAIL!")
        (cls.root / "index.html").write_text("<p>Kotonoha</p>", encoding="utf-8")
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0),
            partial(RangeRequestHandler, directory=str(cls.root)))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        cls.temp.cleanup()

    def request(self, method, name, headers=None):
        con = http.client.HTTPConnection("127.0.0.1",
            self.server.server_address[1], timeout=5)
        try:
            con.request(method, name, headers=headers or {})
            result = con.getresponse()
            code = result.status
            info = {name.lower(): value for name, value in result.getheaders()}
            body = result.read()
            return code, info, body
        finally:
            con.close()

    def test_head_large_gpk_exposes_length_and_ranges(self):
        code, headers, body = self.request("HEAD", "/event01.gpk")
        self.assertEqual(code, 200)
        self.assertEqual(body, b"")
        self.assertEqual(int(headers["content-length"]), self.size)
        self.assertEqual(headers["accept-ranges"], "bytes")

    def test_get_tail_returns_only_eight_bytes(self):
        code, headers, body = self.request("GET", "/event01.gpk",
            {"Range": f"bytes={self.size - 8}-{self.size - 1}"})
        self.assertEqual(code, 206)
        self.assertEqual(headers["content-range"],
            f"bytes {self.size - 8}-{self.size - 1}/{self.size}")
        self.assertEqual(headers["content-length"], "8")
        self.assertEqual(body, b"GPKTAIL!")

    def test_suffix_and_start_range(self):
        status, headers, body = self.request("GET", "/event01.gpk",
            {"Range": "bytes=-8"})
        self.assertEqual((status, body), (206, b"GPKTAIL!"))
        status, headers, body = self.request("GET", "/event01.gpk",
            {"Range": "bytes=0-3"})
        self.assertEqual((status, body), (206, b"GPK!"))

    def test_invalid_and_out_of_bounds_ranges(self):
        for value in (f"bytes={self.size}-", "bytes=5-4",
                      "bytes=0-1,3-4", "bytes=-0"):
            status, headers, body = self.request("GET", "/event01.gpk",
                                                {"Range": value})
            self.assertEqual(status, 416)
            self.assertEqual(headers["content-range"],
                             f"bytes */{self.size}")
            self.assertEqual(body, b"")

    def test_standard_get_head_and_cors(self):
        status, headers, body = self.request("GET", "/index.html")
        self.assertEqual(status, 200)
        self.assertIn(b"Kotonoha", body)
        self.assertEqual(headers["access-control-allow-origin"], "*")
        status, headers, body = self.request("OPTIONS", "/event01.gpk")
        self.assertEqual(status, 204)
        self.assertIn("Range", headers["access-control-allow-headers"])

    def test_directory_not_listable(self):
        status, headers, body = self.request("GET", "/")
        # Root directory is permitted to serve index.html, but nested
        # unindexed directories must not expose a directory listing.
        self.assertEqual(status, 200)
        (self.root / "private").mkdir(exist_ok=True)
        status, headers, body = self.request("GET", "/private/")
        self.assertEqual(status, 403)


if __name__ == "__main__":
    unittest.main()
