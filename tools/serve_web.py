#!/usr/bin/env python3
"""Serve Kotonoha WASM + large game-assets with correct HTTP byte ranges.

Usage:
    python3 tools/serve_web.py --root build-web/outputs --port 3000

Examples:
    /SchoolDays/index.html
    /SchoolDays/game-assets/Event01.GPK

HTTP GET Range requests return 206 and Content-Range. Without Range a normal
GET returns 200. HEAD exposes Content-Length and Accept-Ranges. Supports one
byte range (including suffix ranges); rejects multipart/invalid ranges.
The server is for trusted local development only, not public deployment.
"""

from __future__ import annotations

import argparse
import os
import re
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


RANGE = re.compile(r"bytes=(\d*)-(\d*)\Z", re.IGNORECASE)


class RangeRequestHandler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def end_headers(self) -> None:
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Range, Content-Type")
        self.send_header("Access-Control-Expose-Headers",
                         "Content-Length, Content-Range, Accept-Ranges")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def list_directory(self, path: str):
        self.send_error(403, "Directory listing is disabled")
        return None

    def _unsatisfiable(self, length: int):
        self.send_response(416, "Range Not Satisfiable")
        self.send_header("Content-Range", f"bytes */{length}")
        self.send_header("Content-Length", "0")
        self.end_headers()
        return None

    def send_head(self):
        # Delegates normal directory index/redirect and 404 behavior to
        # SimpleHTTPRequestHandler; only ordinary files require Range logic.
        path = Path(self.translate_path(self.path))
        root = Path(self.directory).resolve()
        try:
            if not path.resolve().is_relative_to(root):
                self.send_error(403, "Path outside document root")
                return None
            if not path.is_file():
                return super().send_head()
            file = path.open("rb")
        except OSError:
            self.send_error(404, "File not found")
            return None

        try:
            info = os.fstat(file.fileno())
            size = info.st_size
            requested = self.headers.get("Range")
            if requested is not None:
                match = RANGE.fullmatch(requested.strip())
                if not match or (not match[1] and not match[2]):
                    file.close()
                    return self._unsatisfiable(size)
                first, last = match.groups()
                if first:
                    start = int(first)
                    end = int(last) if last else size - 1
                else:
                    suffix = int(last)
                    if suffix <= 0:
                        file.close()
                        return self._unsatisfiable(size)
                    start, end = max(0, size - suffix), size - 1
                if start >= size or end < start or end < 0:
                    file.close()
                    return self._unsatisfiable(size)
                end = min(end, size - 1)
                self.send_response(206)
                self.send_header("Content-Range",
                                 f"bytes {start}-{end}/{size}")
                remaining = end - start + 1
                file.seek(start)
            else:
                self.send_response(200)
                remaining = size

            self._bytes_remaining = remaining
            self.send_header("Content-Type", self.guess_type(str(path)))
            self.send_header("Content-Length", str(remaining))
            self.send_header("Last-Modified", self.date_time_string(info.st_mtime))
            self.end_headers()
            return file
        except Exception:
            file.close()
            raise

    def copyfile(self, source, outputfile) -> None:
        # Do not send the rest of the GPK after the advertised byte range.
        count = getattr(self, "_bytes_remaining", None)
        if count is None:
            super().copyfile(source, outputfile)
            return
        while count > 0:
            chunk = source.read(min(count, 256 * 1024))
            if not chunk:
                break
            outputfile.write(chunk)
            count -= len(chunk)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="build-web/outputs",
                        help="Directory containing each game's web output")
    parser.add_argument("--port", type=int, default=3000)
    parser.add_argument("--bind", default="127.0.0.1",
                        help="Local interface (default: 127.0.0.1)")
    args = parser.parse_args()
    root = Path(args.root).expanduser().resolve()
    if not root.is_dir():
        parser.error(f"Document root does not exist: {root}")
    handler = partial(RangeRequestHandler, directory=str(root))
    with ThreadingHTTPServer((args.bind, args.port), handler) as server:
        print(f"[Kotonoha] HTTP Range server: http://{args.bind}:{args.port}/",
              flush=True)
        print(f"[Kotonoha] Serving: {root}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
