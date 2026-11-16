#!/usr/bin/env python3
"""Check a browser-hosted disc image before testing Kotonoha Emscripten.

No disc data is downloaded: HEAD plus two one-byte HTTP Range requests.
No files are modified. The optional local check detects broken/cyclic symlinks.
"""
import argparse
import os
from pathlib import Path
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


class DiscCheckError(RuntimeError):
    pass


def check_local(path: str) -> Path:
    link = Path(path)
    try:
        resolved = link.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise DiscCheckError(
            f"Invalid local disc path or symlink loop: {link}: {exc}"
        ) from exc
    if not resolved.is_file():
        raise DiscCheckError(f"Disc path is not a regular file: {resolved}")
    if not os.access(link, os.R_OK):
        raise DiscCheckError(
            f"Current user cannot read {link}; check directory traversal "
            f"and Nginx user access with 'namei -l {link}'"
        )
    return resolved


def _open(request: Request, timeout: float):
    try:
        return urlopen(request, timeout=timeout)
    except HTTPError as exc:
        raise DiscCheckError(
            f"HTTP {exc.code} for {request.full_url}; "
            "check Nginx root/alias, symlink policy and www-data permissions"
        ) from exc
    except URLError as exc:
        raise DiscCheckError(
            f"Cannot reach {request.full_url}: {exc.reason}"
        ) from exc


def check_http(url: str, timeout: float = 15.0) -> int:
    parsed = urlsplit(url)
    if (parsed.scheme not in ("http", "https") or not parsed.netloc or
            parsed.username or parsed.password or parsed.fragment):
        raise DiscCheckError("Provide an HTTP(S) URL without credentials or fragments")
    with _open(Request(url, method="HEAD",
                       headers={"Accept-Encoding": "identity"}), timeout) as head:
        if head.status != 200:
            raise DiscCheckError(f"Expected HTTP 200 for HEAD; got {head.status}")
        length = head.headers.get("Content-Length", "")
        if not length.isdecimal() or int(length) <= 0:
            raise DiscCheckError(
                "HEAD must expose a positive Content-Length for VFS discovery"
            )
        size = int(length)
        if head.headers.get("Accept-Ranges", "").strip().lower() != "bytes":
            raise DiscCheckError(
                "Server did not advertise 'Accept-Ranges: bytes'; "
                "large discs require 206 Partial Content"
            )
    # The remote VFS reads scattered sectors from an ISO/BIN, including
    # near its end. Verify both ends with one-byte requests and no whole-file
    # buffering even if the server ignores Range.
    for offset in sorted({0, size - 1}):
        request = Request(
            url, method="GET",
            headers={"Range": f"bytes={offset}-{offset}",
                     "Accept-Encoding": "identity"})
        with _open(request, timeout) as response:
            if response.status != 206:
                raise DiscCheckError(
                    f"Range {offset}-{offset} returned HTTP {response.status}, "
                    "not 206; refusing to download the whole disc"
                )
            expected = f"bytes {offset}-{offset}/{size}"
            if response.headers.get("Content-Range", "").strip() != expected:
                raise DiscCheckError(
                    f"Invalid Content-Range for offset {offset}: "
                    f"{response.headers.get('Content-Range')!r}; expected {expected!r}"
                )
            if len(response.read(1)) != 1:
                raise DiscCheckError(f"Empty HTTP Range response at offset {offset}")
    return size


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="e.g. http://localhost/games/wasm/Amagami/game-assets/Amagami.bin")
    parser.add_argument("--local", metavar="PATH",
                        help="optional Nginx filesystem path (check symlinks and access)")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args(argv)
    try:
        if args.timeout <= 0:
            raise DiscCheckError("Timeout must be positive")
        if args.local:
            print("Local disc:", check_local(args.local))
        length = check_http(args.url, args.timeout)
    except DiscCheckError as exc:
        print("FAIL:", exc, file=sys.stderr)
        return 1
    print(f"OK: {length} bytes; HEAD and first/last HTTP Range requests work")
    return 0


if __name__ == "__main__":
    sys.exit(main())
