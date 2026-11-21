"""Create a new synthetic folder for School Days launcher metadata tests."""
import argparse
import struct
import zlib
from pathlib import Path


def test_art() -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(kind + payload)
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", 480, 240, 8, 2, 0, 0, 0)
    row = b"\x00" + bytes((57, 106, 143)) * 480
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(row * 240)) + chunk(b"IEND", b""))


def create(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=False)
    art = root / "game-assets" / "launcher-art"
    art.mkdir(parents=True)
    (art / "SchoolDaysHQ-Launcher.png").write_bytes(test_art())
    (root / "game-assets" / "school-days-hq.ktnroute").write_bytes(b"SYNTHETIC QA ONLY")
    jsons = root / "translations" / "jsons"
    jsons.mkdir(parents=True)
    for name in ("pt-BR.json", "pt-BR-lxh.json", "en.json"):
        (jsons / name).write_text("{}", encoding="ascii")
    (root / "Packs").mkdir()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("new_directory", type=Path)
    create(parser.parse_args().new_directory)
