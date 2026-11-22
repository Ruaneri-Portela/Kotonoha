"""Verify the School Days bootstrap APK's engine assets and ARM64 ELF alignment."""
import argparse
import hashlib
from pathlib import Path
from struct import unpack_from
from zipfile import ZipFile


def inspect(apk: Path, allow_local_launcher_art: bool = False) -> None:
    required = {"SDL3", "SDL3_ttf", "imgui", "kotonoha_engine", "SchoolDays", "c++_shared"}
    with ZipFile(apk) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate APK entries")
        for name in names:
            lower = name.lower()
            if lower.endswith((".gpk", ".ktnroute", ".cmap")):
                raise ValueError(f"Unexpected game resource: {name}")
            if lower.endswith("/schooldayshq-launcher.png") and not allow_local_launcher_art:
                raise ValueError(f"Unexpected game resource: {name}")
        libraries = {f"lib/arm64-v8a/lib{name}.so" for name in required}
        actual = {name for name in names if name.startswith("lib/")}
        if actual != libraries:
            raise ValueError(f"Unexpected native library set: {actual ^ libraries}")
        assets = {name for name in names if name.startswith("assets/")}
        if "assets/packaged-assets.sha256" not in assets:
            raise ValueError("Missing asset fingerprint")
        root = Path(__file__).resolve().parents[2] / "assets"
        permitted = {"assets/packaged-assets.sha256"}
        splash_source = root / "SPLASH.PNG"
        if splash_source.is_file():
            splash_entry = "assets/assets/SPLASH.PNG"
            permitted.add(splash_entry)
            if archive.read(splash_entry) != splash_source.read_bytes():
                raise ValueError(f"Unexpected asset contents: {splash_entry}")
        for family in ("Anime4K", "ArtCNN"):
            for source in (root / family).iterdir():
                if source.suffix in (".glsl", ".txt"):
                    entry = "assets/assets/" + family + "/" + source.name
                    permitted.add(entry)
                    if archive.read(entry) != source.read_bytes():
                        raise ValueError(f"Unexpected asset contents: {entry}")
        if allow_local_launcher_art and "assets/assets/SchoolDaysHQ-Launcher.png" in assets:
            permitted.add("assets/assets/SchoolDaysHQ-Launcher.png")
        if assets != permitted:
            raise ValueError(f"Unexpected APK assets: {assets ^ permitted}")
        digest = hashlib.sha256()
        for entry in sorted(assets - {"assets/packaged-assets.sha256"}):
            digest.update(entry[len("assets/"):].encode("utf-8"))
            digest.update(archive.read(entry))
        if archive.read("assets/packaged-assets.sha256").strip() != digest.hexdigest().encode("ascii"):
            raise ValueError("Incorrect staged asset fingerprint")
        for name in sorted(libraries):
            data = archive.read(name)
            if data[:6] != b"\x7fELF\x02\x01" or unpack_from("<H", data, 18)[0] != 183:
                raise ValueError(f"Expected ARM64 ELF64: {name}")
            pos = unpack_from("<Q", data, 32)[0]
            size, count = unpack_from("<HH", data, 54)
            loads = 0
            for index in range(count):
                kind, _, offset, addr, _, _, _, align = unpack_from("<IIQQQQQQ", data, pos + index * size)
                if kind == 1:
                    loads += 1
                    if align < 16384 or (addr - offset) % 16384:
                        raise ValueError(f"Invalid 16 KiB LOAD alignment: {name}")
            if not loads:
                raise ValueError(f"Missing LOAD segments: {name}")
        print(f"PASS: {len(libraries)} ARM64 libraries, 16 KiB LOAD alignment, {len(assets)} permitted assets")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--allow-local-launcher-art", action="store_true")
    args = parser.parse_args()
    inspect(args.apk, allow_local_launcher_art=args.allow_local_launcher_art)
