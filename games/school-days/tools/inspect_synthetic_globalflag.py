#!/usr/bin/env python3
"""Read-only summary of a marked, isolated synthetic GlobalFlag.DAT profile."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

from make_synthetic_globalflag import (
    MARKER,
    MAX_FLAG_BYTES,
    decode_flags,
    guarded_root,
)


def inspect(root: Path) -> dict[str, int]:
    root = guarded_root(root)
    marker = root / ".qa-profile"
    if (not marker.is_file() or marker.is_symlink()
            or marker.read_bytes() != MARKER):
        raise ValueError("perfil sintético sem marcador .qa-profile válido")
    file = root / "Save" / "GlobalFlag.DAT"
    if file.is_symlink() or not file.is_file():
        raise ValueError("GlobalFlag.DAT sintético ausente ou link recusado")
    if file.stat().st_size > MAX_FLAG_BYTES:
        raise ValueError("GlobalFlag.DAT excede 32 MiB")
    values = decode_flags(file.read_bytes())
    completed = sum(
        value != 0 for key, value in values.items()
        if re.fullmatch(r'\[End\d{2}\]="', key)
    )
    replay = sum(value != 0 for key, value in values.items()
                 if key.startswith("REP"))
    return {
        "EndClear": values.get("EndClear", 0),
        "EndNo": values.get("EndNo", 0),
        "AllClear": values.get("AllClear", 0),
        "Finais": completed,
        "Replay": replay,
        "SP100": values.get("SP100", 0),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--documents", type=Path, required=True,
                        help="Perfil sintético marcado dentro de build/")
    args = parser.parse_args()
    try:
        flags = inspect(args.documents)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(2, f"ERRO: {error}\n")
    print("GlobalFlag.DAT — leitura de perfil QA sintético")
    for key, value in flags.items():
        print(f"{key}: {value}")
    print("EndBGView / EndingMax: parâmetros do título, não deste arquivo")
    print("Nenhum arquivo foi alterado.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
