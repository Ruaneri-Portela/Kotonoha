"""Build the game-specific integrated School Days L×H pack (no retail edits).

Usage (Windows):
    python build_lxh_gpk.py --source-overlay <Release/display-options> \
        --output <NEW isolated scratch/LxH.GPK> --manifest <NEW scratch/manifest.json>

The archive contains exactly seven extended ORS files, seven universal
English title cards and seven WMV/OGG pairs. It uses the same
STKFile0PACKFILE/UTF-16 PIDX/zlib/XOR framing as the v1.02 GPK reader, with
uncompressed file bodies for seekable streaming. It never edits source files,
retail GPKs, translations or saves. `--output` MUST NOT already exist.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import zlib


KEY = bytes.fromhex("82 ee 1d b3 57 e9 2c c2 2f 54 7b 10 4c 9a 75 49")
TRAILER_MARKER = b"STKFile0PIDX"
PACK_MARKER = b"STKFile0PACKFILE"
ENDINGS = (
    "05-A2-F00", "05-A2-Z00", "05-B0-F00", "05-G0-L03",
    "05-KC-F00", "05-SB-E01", "05-SC-L03",
)


def wanted_paths() -> list[str]:
    paths = []
    for ending in ENDINGS:
        paths.extend((
            f"Script/English/05/{ending}.ENG.ORS",
            f"LxH/{ending}-LXH.png",
            f"LxH/Media/{ending}-LXH.wmv",
            f"LxH/Media/{ending}-LXH.ogg",
        ))
    return paths


def digest_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as inp:
        for chunk in iter(lambda: inp.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_index_entry(path: str, offset: int, size: int) -> bytes:
    name = path.encode("utf-16le")
    if not name or len(name) % 2 or len(name) // 2 > 65535:
        raise ValueError("Invalid UTF-16 GPK name")
    if offset < 0 or size <= 0 or offset + size > 0xFFFFFFFF:
        raise ValueError("GPK raw entry exceeds the 32-bit retail format")
    return (struct.pack("<H", len(name) // 2) + name +
            struct.pack("<HHHII4sIB", 0, 0, 0, offset, size,
                        b"    ", size, 0))


def create(overlay: Path, output: Path, manifest: Path) -> dict:
    if not overlay.is_dir():
        raise ValueError("Source overlay directory is missing")
    if output.exists() or manifest.exists() or output == manifest:
        raise FileExistsError("Output or manifest exists; refusing overwrite")
    if output.suffix.casefold() != ".gpk":
        raise ValueError("Output must be a GPK file")
    paths = wanted_paths()
    if len(paths) != 28 or len({p.casefold() for p in paths}) != 28:
        raise AssertionError("Duplicate GPK logical paths")
    members = [(relative, overlay.joinpath(*relative.split("/")))
               for relative in paths]
    for logical, absolute in members:
        if not absolute.is_file() or absolute.is_symlink():
            raise FileNotFoundError(f"Source missing/not regular: {logical}")
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    if output.resolve().is_relative_to(overlay.resolve()):
        raise ValueError("Never write the GPK into the source overlay")

    partial = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=".LxH.GPK.build-", dir=output.parent, delete=False,
        ) as temp:
            partial = Path(temp.name)
            index = bytearray()
            entries = []
            for logical, absolute in members:
                offset = temp.tell()
                hasher = hashlib.sha256()
                length = 0
                with absolute.open("rb") as src:
                    while True:
                        chunk = src.read(1024 * 1024)
                        if not chunk:
                            break
                        hasher.update(chunk)
                        temp.write(chunk)
                        length += len(chunk)
                index.extend(write_index_entry(logical, offset, length))
                entries.append({"path": logical, "bytes": length,
                                "sha256": hasher.hexdigest(),
                                "physicalOffset": offset})
            index.extend(b"\x00\x00\x00\x00")
            compressed = zlib.compress(bytes(index), level=9)
            coded = struct.pack("<I", len(index)) + compressed
            encoded = bytes(b ^ KEY[i & 15] for i, b in enumerate(coded))
            temp.write(encoded)
            temp.write(TRAILER_MARKER + struct.pack("<I", len(encoded)) + PACK_MARKER)
            temp.flush()
        # A separate parse path verifies exact PIDX semantics and entry bytes.
        verify_archive(partial, entries)
        # The user explicitly requested creation, but NEVER overwrite an
        # existing pack; publishing is atomic within the target directory.
        if output.exists():
            raise FileExistsError("Concurrent GPK appeared before publication")
        partial.rename(output)
        partial = None
        report = {"archive": str(output), "archiveSha256": digest_file(output),
                  "archiveBytes": output.stat().st_size,
                  "entryCount": len(entries), "mode": "raw-seekable",
                  "entries": entries}
        manifest.write_text(json.dumps(report, indent=2), encoding="utf-8")
        return report
    finally:
        if partial is not None:
            partial.unlink(missing_ok=True)


def verify_archive(archive: Path, expected: list[dict]) -> None:
    size = archive.stat().st_size
    with archive.open("rb") as f:
        f.seek(-32, 2)
        trailer = f.read(32)
        if trailer[:12] != TRAILER_MARKER or trailer[16:] != PACK_MARKER:
            raise ValueError("Incorrect GPK trailer signature")
        encoded_len = struct.unpack_from("<I", trailer, 12)[0]
        if not 4 < encoded_len < size - 32:
            raise ValueError("Incorrect GPK index length")
        pidx_offset = size - 32 - encoded_len
        f.seek(pidx_offset)
        raw_encoded = f.read(encoded_len)
        decoded = bytes(b ^ KEY[i & 15] for i, b in enumerate(raw_encoded))
        uncompressed_len = struct.unpack_from("<I", decoded)[0]
        index = zlib.decompress(decoded[4:])
        if len(index) != uncompressed_len:
            raise ValueError("PIDX decoded size mismatch")
        at = 0
        previous_end = 0
        for item in expected:
            name_units = struct.unpack_from("<H", index, at)[0]
            at += 2
            name = index[at:at + name_units * 2].decode("utf-16le")
            at += name_units * 2
            sv, ver, zero, off, stored, magic, orig, prefix = struct.unpack_from(
                "<HHHII4sIB", index, at)
            at += struct.calcsize("<HHHII4sIB")
            if (name != item["path"] or any((sv, ver, zero, prefix)) or
                    magic != b"    " or stored != orig or stored != item["bytes"] or
                    off != previous_end or off + stored > pidx_offset):
                raise ValueError("Noncanonical GPK entry: " + name)
            f.seek(off)
            h = hashlib.sha256()
            remaining = stored
            while remaining:
                piece = f.read(min(1024 * 1024, remaining))
                if not piece:
                    raise IOError("Truncated archived entry")
                h.update(piece)
                remaining -= len(piece)
            if h.hexdigest() != item["sha256"]:
                raise ValueError("Archived entry differs from source " + name)
            previous_end = off + stored
        if previous_end != pidx_offset or index[at:] != b"\x00\x00\x00\x00":
            raise ValueError("GPK entry chain or PIDX terminator invalid")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-overlay", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()
    report = create(args.source_overlay.resolve(), args.output.resolve(),
                    args.manifest.resolve())
    print("INTEGRATED_LXH_GPK_ROUNDTRIP_PASS", report["entryCount"],
          report["archiveBytes"], report["archiveSha256"])
    print("ARCHIVE", report["archive"])


if __name__ == "__main__":
    main()
