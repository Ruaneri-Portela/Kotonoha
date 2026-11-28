"""Generate a private C++ header from the approved options UI resources."""

import argparse
import hashlib
from pathlib import Path
import zlib


# Only the final user-authored Display panel/atlas and generated interaction
# maps are eligible by default. Retail-derived General/Sound artwork requires
# an explicit private QA opt-in, even if present in the same source directory.
APPROVED = {
    "Option_Display.png": "dcee4c073875d0c1dc7f9e5f2a4a1b0a5c98604d593460dd887f67ee63c62161",
    "Option_Display_Chip.png": "c9ff07389bdf9bcd45ef861f9644b3bf906a5fdd91ea277ec440f265d279a6d8",
    "Option_Display.cmap": "0fc89d0b3d129623b11bd2e2aa0dec93e929bb4f35bb872259723e5c7086be51",
    "Option_Display_Wide.cmap": "2958d401330a35a3c639fb7ccfc8a343d50c5d7c973ee936994a3c5001075950",
    "Option_Display_Wide_Full.cmap": "c88ef65f688c7c4d60ef92666f6a246a1251363412d10f200b5a1b5c2ab8b3d4",
    "Option_Display_Wide_Note.cmap": "ed37f6e5a1ea0a3ccc27bc7682d734f239063a894cb1b372712cac69cae78439",
}
PRIVATE_OPTION_ART = {
    "Option_Def.png": "0981a9418aebdce22a5c83def2675781c39c6734ec9cf1563442395ba272896a",
    "Option_Def_Chip.png": "f7889cf91160c4cc9b79b8e9d3037e911c70803062fcfb1a33ae2443a99d26c8",
    "Option_Sound.png": "5f1ce6b327c64585b088fec8fc459bf7d1c47487e2cacd48967cb58cfd9d13de",
    "Option_Sound_Chip.png": "4622787742e1c6345bde5e4ae48045628a54322e279c6510efd0d713a69f754d",
}


def generate(source: Path, output: Path, private_option_art: bool = False) -> None:
    lines = [
        "// Generated from approved private Display UI files. Do not commit.",
        "#pragma once",
        "#include <cstddef>",
        "namespace Kotonoha::EmbeddedDisplay {",
        "struct Resource { const char* logical; const unsigned char* data;",
        "  std::size_t compressedSize; std::size_t size; };",
    ]
    entries = []
    approved = {**APPROVED, **PRIVATE_OPTION_ART} if private_option_art else APPROVED
    for index, (name, digest) in enumerate(approved.items()):
        data = (source / "System" / "Option" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Unapproved Display UI asset: {name}")
        packed = zlib.compress(data, level=9)
        symbol = f"resource{index}"
        lines.append(f"inline constexpr unsigned char {symbol}[] = {{")
        for offset in range(0, len(packed), 24):
            lines.append("  " + ",".join(f"0x{byte:02x}" for byte in packed[offset:offset+24]) + ",")
        lines.append("};")
        entries.append(f'  {{"system/option/{name.lower()}", {symbol}, sizeof({symbol}), {len(data)}}},')
    lines.extend(["inline constexpr Resource resources[] = {", *entries, "};", "}"])
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="ascii")
    temporary.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--private-option-art", action="store_true")
    args = parser.parse_args()
    generate(args.source, args.output, args.private_option_art)
