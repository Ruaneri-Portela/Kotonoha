"""Read-only audit of the installed School Days HQ retail GPK indexes/ORS files."""

from __future__ import annotations

import collections
import pathlib
import re
import struct
import sys
import zlib


KEY = bytes.fromhex("82 EE 1D B3 57 E9 2C C2 2F 54 7B 10 4C 9A 75 49")
PACKS = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
    r"D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\Packs")


def index(path: pathlib.Path):
    with path.open("rb") as source:
        source.seek(-32, 2)
        trailer = source.read(32)
        assert trailer[:12] == b"STKFile0PIDX" and trailer[16:] == b"STKFile0PACKFILE"
        length = struct.unpack_from("<I", trailer, 12)[0]
        source.seek(-32 - length, 2)
        encrypted = source.read(length)
    decoded = bytes(value ^ KEY[pos % 16] for pos, value in enumerate(encrypted))
    expected = struct.unpack_from("<I", decoded)[0]
    data = zlib.decompress(decoded[4:])
    assert len(data) == expected
    result = {}
    cursor = 0
    while cursor < len(data) - 4:
        units = struct.unpack_from("<H", data, cursor)[0]
        cursor += 2
        name = data[cursor:cursor + units * 2].decode("utf-16le")
        cursor += units * 2
        _, _, _, offset, stored_len, magic, raw_len, head_len = struct.unpack_from(
            "<HHHII4sIB", data, cursor)
        cursor += 23
        prefix = data[cursor:cursor + head_len]
        cursor += head_len
        result[name.replace("\\", "/")] = (offset, stored_len, magic, raw_len, prefix)
    assert data[cursor:] == b"\0\0\0\0"
    return result


def read_entry(pack: pathlib.Path, entry):
    offset, stored_len, magic, raw_len, prefix = entry
    with pack.open("rb") as source:
        source.seek(offset)
        stored = prefix + source.read(stored_len - len(prefix))
    assert len(stored) == stored_len
    if magic == b"DFLT":
        result = zlib.decompress(stored)
        assert len(result) == raw_len
        return result
    assert magic == b"    "
    return stored


def main():
    packs = sorted(PACKS.glob("*.GPK"), key=lambda path: path.name.lower())
    all_entries = {}
    collisions = []
    for pack in packs:
        for name, entry in index(pack).items():
            logical = (pack.stem + "/" + name).replace("\\", "/")
            folded = logical.casefold()
            if folded in all_entries and all_entries[folded][0] != logical:
                collisions.append((logical, all_entries[folded][0]))
            all_entries[folded] = (logical, pack, entry)
    print("packs", len(packs), "entries", len(all_entries), "casefold collisions", len(collisions))
    print("collision samples", collisions[:10])
    scripts = [(logical, pack, entry) for logical, pack, entry in all_entries.values()
               if logical.lower().startswith("script/") and logical.lower().endswith(".ors")]
    print("ORS", len(scripts))
    missing = collections.Counter()
    examples = collections.defaultdict(list)
    commands = collections.Counter()
    inspected = 0
    for logical, pack, entry in scripts:
        body = read_entry(pack, entry).decode("utf-8", "replace")
        for line in body.splitlines():
            match = re.search(r"\[([^]]+)\]", line)
            if not match:
                continue
            command = match.group(1)
            payload = line[match.end():]
            if not payload.startswith("="):
                continue
            fields = payload[1:].split("\t")
            commands[command] += 1
            names = []
            if command == "CreateBG" and len(fields) >= 3:
                names = [fields[2] + ".PNG"]
            elif command == "PlayMovie" and len(fields) >= 2:
                names = [fields[1] + ".wmv"]
            elif command == "EndRoll" and len(fields) >= 2:
                names = [fields[1] + ".wmv"]
            elif command == "PlayVoice" and len(fields) >= 2:
                names = [fields[1] + ".OGG"]
            elif command == "PlaySe" and len(fields) >= 3:
                names = [fields[2] + ".OGG"]
            elif command == "PlayBgm" and len(fields) >= 2:
                names = [fields[1] + "_int.ogg", fields[1] + "_loop.ogg"]
            elif command == "EndBgm" and len(fields) >= 2:
                names = [fields[1] + ".ogg"]
            for name in names:
                if not name.strip():
                    continue
                name = name.replace("\\", "/")
                if name.lower().startswith("music/bgm/"):
                    name = "BGM/SD_BGM/" + name[10:]
                elif name.lower().startswith("music/vocal/"):
                    name = "BGM/VOCAL/" + name[12:]
                elif name.lower().startswith("se/se"):
                    name = name[3:]
                if name.casefold() not in all_entries:
                    missing[(command, name)] += 1
                    if len(examples[(command, name)]) < 3:
                        examples[(command, name)].append(logical)
        inspected += 1
    print("inspected", inspected, "commands", commands.most_common(12))
    print("missing distinct", len(missing), "total references", sum(missing.values()))
    print("missing by command", collections.Counter({command: sum(count for (kind, _), count in missing.items() if kind == command) for command, _ in missing}))
    for (command, name), count in missing.items():
        if " " not in name:
            continue
        compact = name.replace(" ", "")
        if compact.casefold() in all_entries:
            print("RECOVERABLE_SPACE_TYPO", command, count, repr(name),
                  "->", all_entries[compact.casefold()][0])
    for (command, name), count in missing.most_common():
        print("MISSING", command, count, name, ",".join(examples[(command, name)]))


if __name__ == "__main__":
    main()
