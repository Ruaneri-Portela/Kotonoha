#!/usr/bin/env python3
"""Run the original Pure Mail from the supplied DVD files with CrossOver.

Creates an isolated patched executable. The disc files are linked read-only;
the original executable and game assets are never modified.
"""

import argparse
import hashlib
import pathlib
import struct
import subprocess


EXPECTED_SHA256 = "a9359d55f23831ebee3b4ca803c6aff1d28a37468de1ccb0e19f8d388ca0e654"
BOOT_CHECK_VA = 0x417F70
ORIGINAL_PROLOGUE = bytes.fromhex("81 ec 00 02 00 00")
RETURN_SUCCESS = bytes.fromhex("31 c0 c3")
DEFAULT_ASSETS = pathlib.Path("/Volumes/Nero 256GB/KotonohaAssets/PMAILDVD")
DEFAULT_CXSTART = pathlib.Path(
    "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/cxstart"
)


def file_offset_for_va(data: bytes, va: int) -> int:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("pmail.exe is not a PE executable")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    rva = va - image_base
    for index in range(sections):
        section = pe + 24 + optional_size + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, section + 8
        )
        if virtual_address <= rva < virtual_address + min(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError("boot check address is outside the executable")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=pathlib.Path, default=DEFAULT_ASSETS)
    parser.add_argument("--bottle", default="Master")
    parser.add_argument("--cxstart", type=pathlib.Path, default=DEFAULT_CXSTART)
    parser.add_argument("--prepare-only", action="store_true", help="Create the patched copy without launching it")
    args = parser.parse_args()

    source = args.assets / "data" / "pmail.exe"
    original = source.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"Unsupported pmail.exe SHA-256: {digest}")
    if not args.cxstart.is_file():
        raise SystemExit(f"CrossOver launcher not found: {args.cxstart}")

    output = pathlib.Path(__file__).resolve().parent.parent / "build" / "pmail-original" / "data"
    output.mkdir(parents=True, exist_ok=True)
    for asset in source.parent.iterdir():
        if asset.name.lower() == "pmail.exe":
            continue
        link = output / asset.name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(asset)

    patched = bytearray(original)
    offset = file_offset_for_va(patched, BOOT_CHECK_VA)
    if patched[offset : offset + len(ORIGINAL_PROLOGUE)] != ORIGINAL_PROLOGUE:
        raise SystemExit("Boot check signature changed; refusing to patch")
    patched[offset : offset + len(RETURN_SUCCESS)] = RETURN_SUCCESS
    executable = output / "pmail.exe"
    if not executable.is_file() or executable.read_bytes() != patched:
        executable.write_bytes(patched)

    if args.prepare_only:
        print(f"Prepared Pure Mail: {executable}")
        return

    subprocess.Popen(
        [
            str(args.cxstart),
            "--bottle", args.bottle,
            "--no-wait",
            "--workdir", str(output),
            str(executable),
        ]
    )
    print(f"Started Pure Mail: {executable}")


if __name__ == "__main__":
    main()
