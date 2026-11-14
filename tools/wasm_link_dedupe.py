#!/usr/bin/env python3
"""Deduplicate identical vcpkg static archives for Emscripten SIDE_MODULEs.

CMake and pkg-config may spell one archive as an absolute path, a relative
path, or -lname. Wasm side-module linking can extract duplicate members.
Keep the final occurrence of each physical archive; pass everything else
through unchanged to the real linker driver.
"""
from pathlib import Path
import subprocess
import sys


def archive_identity(token, search_directories, cwd=None):
    base = Path(cwd) if cwd is not None else Path.cwd()
    if token.endswith(".a") and not token.startswith("-"):
        candidate = Path(token)
        if not candidate.is_absolute():
            candidate = base / candidate
        if candidate.is_file():
            return str(candidate.resolve())
    if token.startswith("-l") and len(token) > 2:
        name = token[2:]
        archive_name = name[1:] if name.startswith(":") else f"lib{name}.a"
        for directory in search_directories:
            candidate = Path(directory) / archive_name
            if not candidate.is_absolute():
                candidate = base / candidate
            if candidate.is_file():
                return str(candidate.resolve())
    return None


def normalize(arguments, cwd=None):
    search_directories = []
    # Linker -L arguments are search paths for the whole link, not only
    # for libraries appearing after them.
    for index, token in enumerate(arguments):
        if token == "-L" and index + 1 < len(arguments):
            search_directories.append(arguments[index + 1])
        elif token.startswith("-L") and len(token) > 2:
            search_directories.append(token[2:])

    identities = [archive_identity(arg, search_directories, cwd) for arg in arguments]
    last_index = {
        identity: index
        for index, identity in enumerate(identities)
        if identity is not None
    }
    return [
        token
        for index, token in enumerate(arguments)
        if identities[index] is None or last_index[identities[index]] == index
    ]


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: wasm_link_dedupe.py <link-driver> [arguments...]")
    return subprocess.call(normalize(sys.argv[1:]))


if __name__ == "__main__":
    raise SystemExit(main())
