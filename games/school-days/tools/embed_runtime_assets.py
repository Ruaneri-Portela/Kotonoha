"""Generate read-only native data for the exact approved private assets/ pack."""

import argparse
import hashlib
from pathlib import Path
import zlib


APPROVED_COUNT = 50
APPROVED_DIGEST = "a0ed3c8d021e93ba567e1095a53cd55bee8e27a096e71245596bf2595c628b9a"


def collect(source: Path):
    files = sorted((p for p in source.rglob("*") if p.is_file()),
                   key=lambda p: p.relative_to(source).as_posix())
    digest = hashlib.sha256()
    resources = []
    for path in files:
        if path.is_symlink():
            raise ValueError(f"Symlink not allowed: {path}")
        logical = path.relative_to(source).as_posix()
        data = path.read_bytes()
        digest.update(logical.encode("utf-8") + b"\0" + hashlib.sha256(data).digest())
        resources.append((logical, data))
    if len(resources) != APPROVED_COUNT or digest.hexdigest() != APPROVED_DIGEST:
        raise ValueError("Runtime assets do not match the approved 50-file private pack")
    return resources


def generate(source: Path, output: Path):
    resources = collect(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(".tmp")
    try:
        with temp.open("w", encoding="ascii", newline="\n") as stream:
            stream.write("// Generated private assets. Do not commit.\n#pragma once\n"
                         "#include <cstddef>\nnamespace Kotonoha::EmbeddedAssetsData {\n"
                         "struct Resource { const char* logical; const unsigned char* data; "
                         "std::size_t compressedSize; std::size_t size; };\n")
            entries = []
            for i, (logical, data) in enumerate(resources):
                packed = zlib.compress(data, 9)
                name = f"resource{i}"
                stream.write(f"inline constexpr unsigned char {name}[] = {{\n")
                for offset in range(0, len(packed), 32):
                    stream.write("  " + ",".join(f"0x{x:02x}" for x in packed[offset:offset+32]) + ",\n")
                stream.write("};\n")
                entries.append(f'  {{"{logical}", {name}, sizeof({name}), {len(data)}}},\n')
            stream.write("inline constexpr Resource resources[] = {\n" + "".join(entries) + "};\n}\n")
        temp.replace(output)
    finally:
        if temp.exists():
            temp.unlink()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
