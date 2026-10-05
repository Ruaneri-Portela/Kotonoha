#!/usr/bin/env python3
"""Reference encoder/decoder for the KTRF binary container envelope v0.1.

This module intentionally handles only the generic container and STRS payload.
Semantic entity record layouts are added in later binary-format phases.
"""

from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass
from typing import Iterable, Mapping, Sequence


MAGIC = b"KTRF"
FORMAT_MAJOR = 0
FORMAT_MINOR = 1
HEADER_SIZE = 96
SECTION_ENTRY_SIZE = 48
BYTE_ORDER_LITTLE = 1
HASH_SHA256 = 1
NULL_INDEX = 0xFFFFFFFF
DEFAULT_ALIGNMENT = 8
MAX_ALIGNMENT = 4096

HEADER_FLAGS_KNOWN = 0x00000000
SECTION_REQUIRED = 0x00000001
SECTION_COMPRESSED = 0x00000002
SECTION_NON_SEMANTIC = 0x00000004
SECTION_FLAGS_KNOWN = SECTION_REQUIRED | SECTION_COMPRESSED | SECTION_NON_SEMANTIC

HEADER_STRUCT = struct.Struct("<4sHHHHIIIQQBBHI32s16s")
SECTION_STRUCT = struct.Struct("<4sIQQQIIII")

CANONICAL_SECTION_ORDER = (
    b"META",
    b"STRS",
    b"NSPC",
    b"FEAT",
    b"ENTR",
    b"VARS",
    b"RSRC",
    b"HOOK",
    b"ENDG",
    b"EXPR",
    b"EFFT",
    b"CHOI",
    b"NODE",
    b"TRAN",
    b"EXTN",
    b"DBUG",
)
SECTION_RANK = {code: index for index, code in enumerate(CANONICAL_SECTION_ORDER)}


class KtrfBinaryError(ValueError):
    pass


@dataclass(frozen=True)
class Section:
    type_code: bytes
    payload: bytes
    flags: int = SECTION_REQUIRED
    item_count: int = 0
    alignment: int = DEFAULT_ALIGNMENT
    decoded_size: int | None = None


@dataclass(frozen=True)
class SectionEntry:
    type_code: bytes
    flags: int
    offset: int
    stored_size: int
    decoded_size: int
    item_count: int
    alignment: int
    crc32: int


@dataclass(frozen=True)
class ParsedContainer:
    format_major: int
    format_minor: int
    flags: int
    file_size: int
    content_sha256: bytes
    entries: tuple[SectionEntry, ...]
    sections: Mapping[bytes, bytes]


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def _validate_fourcc(code: bytes) -> None:
    if not isinstance(code, bytes) or len(code) != 4:
        raise KtrfBinaryError(f"section type must be exactly four bytes: {code!r}")
    if any(byte < 0x20 or byte > 0x7E for byte in code):
        raise KtrfBinaryError(f"section type must be printable ASCII FourCC: {code!r}")


def _canonical_key(section: Section) -> tuple[int, bytes]:
    rank = SECTION_RANK.get(section.type_code)
    if rank is None:
        return (len(CANONICAL_SECTION_ORDER), section.type_code)
    return (rank, section.type_code)


def _validate_section(section: Section) -> None:
    _validate_fourcc(section.type_code)
    if section.flags & ~SECTION_FLAGS_KNOWN:
        raise KtrfBinaryError(
            f"section {section.type_code!r} has unknown flags 0x{section.flags:08X}"
        )
    if section.flags & SECTION_COMPRESSED:
        raise KtrfBinaryError("compression is not standardized in binary v0.1")
    if not _is_power_of_two(section.alignment) or section.alignment > MAX_ALIGNMENT:
        raise KtrfBinaryError(
            f"section {section.type_code!r} alignment must be a power of two <= {MAX_ALIGNMENT}"
        )
    if section.item_count < 0 or section.item_count > 0xFFFFFFFF:
        raise KtrfBinaryError("item_count does not fit u32")
    decoded_size = len(section.payload) if section.decoded_size is None else section.decoded_size
    if decoded_size != len(section.payload):
        raise KtrfBinaryError(
            "without compression, decoded_size must equal stored payload size"
        )


def build_container(sections: Iterable[Section], *, flags: int = 0) -> bytes:
    if flags & ~HEADER_FLAGS_KNOWN:
        raise KtrfBinaryError(f"unknown header flags 0x{flags:08X}")

    ordered = sorted(list(sections), key=_canonical_key)
    seen: set[bytes] = set()
    for section in ordered:
        _validate_section(section)
        if section.type_code in seen:
            raise KtrfBinaryError(f"duplicate section type {section.type_code!r}")
        seen.add(section.type_code)

    directory_offset = HEADER_SIZE
    directory_size = len(ordered) * SECTION_ENTRY_SIZE
    cursor = directory_offset + directory_size
    entries: list[SectionEntry] = []

    for section in ordered:
        cursor = _align(cursor, section.alignment)
        payload = bytes(section.payload)
        decoded_size = len(payload) if section.decoded_size is None else section.decoded_size
        entries.append(
            SectionEntry(
                type_code=section.type_code,
                flags=section.flags,
                offset=cursor,
                stored_size=len(payload),
                decoded_size=decoded_size,
                item_count=section.item_count,
                alignment=section.alignment,
                crc32=zlib.crc32(payload) & 0xFFFFFFFF,
            )
        )
        cursor += len(payload)

    file_size = cursor
    body = bytearray(file_size - HEADER_SIZE)

    for index, entry in enumerate(entries):
        start = index * SECTION_ENTRY_SIZE
        body[start : start + SECTION_ENTRY_SIZE] = SECTION_STRUCT.pack(
            entry.type_code,
            entry.flags,
            entry.offset,
            entry.stored_size,
            entry.decoded_size,
            entry.item_count,
            entry.alignment,
            entry.crc32,
            0,
        )

    for section, entry in zip(ordered, entries):
        start = entry.offset - HEADER_SIZE
        body[start : start + entry.stored_size] = section.payload

    content_sha256 = hashlib.sha256(body).digest()
    zero_crc_header = HEADER_STRUCT.pack(
        MAGIC,
        FORMAT_MAJOR,
        FORMAT_MINOR,
        HEADER_SIZE,
        SECTION_ENTRY_SIZE,
        flags,
        len(entries),
        0,
        directory_offset,
        file_size,
        BYTE_ORDER_LITTLE,
        HASH_SHA256,
        0,
        0,
        content_sha256,
        b"\0" * 16,
    )
    header_crc32 = zlib.crc32(zero_crc_header) & 0xFFFFFFFF
    header = HEADER_STRUCT.pack(
        MAGIC,
        FORMAT_MAJOR,
        FORMAT_MINOR,
        HEADER_SIZE,
        SECTION_ENTRY_SIZE,
        flags,
        len(entries),
        0,
        directory_offset,
        file_size,
        BYTE_ORDER_LITTLE,
        HASH_SHA256,
        0,
        header_crc32,
        content_sha256,
        b"\0" * 16,
    )
    return header + bytes(body)


def _check_zero(data: bytes, start: int, end: int, label: str) -> None:
    if end > start and any(data[start:end]):
        raise KtrfBinaryError(f"non-zero bytes in {label} padding [{start}, {end})")


def parse_container(
    data: bytes,
    *,
    known_section_types: Sequence[bytes] | None = None,
    require_canonical: bool = True,
) -> ParsedContainer:
    if len(data) < HEADER_SIZE:
        raise KtrfBinaryError("file is smaller than the 96-byte KTRF header")

    (
        magic,
        format_major,
        format_minor,
        header_size,
        section_entry_size,
        flags,
        section_count,
        reserved0,
        directory_offset,
        file_size,
        byte_order,
        hash_algorithm,
        reserved1,
        header_crc32,
        content_sha256,
        reserved2,
    ) = HEADER_STRUCT.unpack_from(data, 0)

    if magic != MAGIC:
        raise KtrfBinaryError(f"bad magic: {magic!r}")
    if (format_major, format_minor) != (FORMAT_MAJOR, FORMAT_MINOR):
        raise KtrfBinaryError(
            f"unsupported KTRF binary version {format_major}.{format_minor}"
        )
    if header_size != HEADER_SIZE:
        raise KtrfBinaryError(f"unexpected header_size {header_size}")
    if section_entry_size != SECTION_ENTRY_SIZE:
        raise KtrfBinaryError(f"unexpected section_entry_size {section_entry_size}")
    if flags & ~HEADER_FLAGS_KNOWN:
        raise KtrfBinaryError(f"unknown header flags 0x{flags:08X}")
    if reserved0 != 0 or reserved1 != 0 or reserved2 != b"\0" * 16:
        raise KtrfBinaryError("reserved header fields must be zero")
    if directory_offset != HEADER_SIZE:
        raise KtrfBinaryError(f"canonical v0.1 directory offset must be {HEADER_SIZE}")
    if file_size != len(data):
        raise KtrfBinaryError(f"file_size mismatch: header={file_size} actual={len(data)}")
    if byte_order != BYTE_ORDER_LITTLE:
        raise KtrfBinaryError(f"unsupported byte_order code {byte_order}")
    if hash_algorithm != HASH_SHA256:
        raise KtrfBinaryError(f"unsupported hash_algorithm code {hash_algorithm}")

    zero_crc = bytearray(data[:HEADER_SIZE])
    zero_crc[0x2C:0x30] = b"\0\0\0\0"
    expected_header_crc = zlib.crc32(zero_crc) & 0xFFFFFFFF
    if header_crc32 != expected_header_crc:
        raise KtrfBinaryError(
            f"header CRC32 mismatch: stored=0x{header_crc32:08X} expected=0x{expected_header_crc:08X}"
        )

    actual_content_hash = hashlib.sha256(data[HEADER_SIZE:]).digest()
    if content_sha256 != actual_content_hash:
        raise KtrfBinaryError("content SHA-256 mismatch")

    directory_end = directory_offset + section_count * SECTION_ENTRY_SIZE
    if directory_end > file_size:
        raise KtrfBinaryError("section directory extends beyond file")

    entries: list[SectionEntry] = []
    seen: set[bytes] = set()
    known = set(known_section_types) if known_section_types is not None else None
    previous_key: tuple[int, bytes] | None = None

    for index in range(section_count):
        offset = directory_offset + index * SECTION_ENTRY_SIZE
        (
            type_code,
            section_flags,
            payload_offset,
            stored_size,
            decoded_size,
            item_count,
            alignment,
            crc32,
            reserved,
        ) = SECTION_STRUCT.unpack_from(data, offset)

        _validate_fourcc(type_code)
        if type_code in seen:
            raise KtrfBinaryError(f"duplicate section type {type_code!r}")
        seen.add(type_code)
        if section_flags & ~SECTION_FLAGS_KNOWN:
            raise KtrfBinaryError(
                f"section {type_code!r} has unknown flags 0x{section_flags:08X}"
            )
        if section_flags & SECTION_COMPRESSED:
            raise KtrfBinaryError("compressed sections are unsupported in v0.1")
        if reserved != 0:
            raise KtrfBinaryError(f"section {type_code!r} reserved field must be zero")
        if not _is_power_of_two(alignment) or alignment > MAX_ALIGNMENT:
            raise KtrfBinaryError(f"section {type_code!r} has invalid alignment {alignment}")
        if payload_offset % alignment != 0:
            raise KtrfBinaryError(f"section {type_code!r} offset is not aligned")
        if decoded_size != stored_size:
            raise KtrfBinaryError(
                f"section {type_code!r}: decoded_size must equal stored_size without compression"
            )
        if payload_offset < directory_end or payload_offset + stored_size > file_size:
            raise KtrfBinaryError(f"section {type_code!r} payload is out of bounds")
        if known is not None and type_code not in known and section_flags & SECTION_REQUIRED:
            raise KtrfBinaryError(f"unknown required section {type_code!r}")

        if require_canonical:
            key = (SECTION_RANK.get(type_code, len(CANONICAL_SECTION_ORDER)), type_code)
            if previous_key is not None and key <= previous_key:
                raise KtrfBinaryError("section directory is not in canonical order")
            previous_key = key

        entries.append(
            SectionEntry(
                type_code=type_code,
                flags=section_flags,
                offset=payload_offset,
                stored_size=stored_size,
                decoded_size=decoded_size,
                item_count=item_count,
                alignment=alignment,
                crc32=crc32,
            )
        )

    by_offset = sorted(entries, key=lambda entry: entry.offset)
    cursor = directory_end
    for entry in by_offset:
        if entry.offset < cursor:
            raise KtrfBinaryError(f"section {entry.type_code!r} overlaps previous data")
        _check_zero(data, cursor, entry.offset, "section")
        payload = data[entry.offset : entry.offset + entry.stored_size]
        actual_crc = zlib.crc32(payload) & 0xFFFFFFFF
        if actual_crc != entry.crc32:
            raise KtrfBinaryError(
                f"section {entry.type_code!r} CRC32 mismatch: stored=0x{entry.crc32:08X} actual=0x{actual_crc:08X}"
            )
        cursor = entry.offset + entry.stored_size
    if cursor != file_size:
        _check_zero(data, cursor, file_size, "trailing")
        raise KtrfBinaryError("canonical v0.1 files must not contain trailing padding")

    sections = {
        entry.type_code: data[entry.offset : entry.offset + entry.stored_size]
        for entry in entries
    }
    return ParsedContainer(
        format_major=format_major,
        format_minor=format_minor,
        flags=flags,
        file_size=file_size,
        content_sha256=content_sha256,
        entries=tuple(entries),
        sections=sections,
    )


def encode_strs(strings: Iterable[str]) -> tuple[bytes, Mapping[str, int]]:
    unique: set[bytes] = {b""}
    for value in strings:
        if "\x00" in value:
            raise KtrfBinaryError("STRS forbids embedded U+0000")
        encoded = value.encode("utf-8")
        unique.add(encoded)

    ordered = [b""] + sorted(item for item in unique if item != b"")
    offsets = [0]
    blob = bytearray()
    for item in ordered:
        blob.extend(item)
        offsets.append(len(blob))

    header = struct.pack("<II", len(ordered), len(blob))
    offset_table = struct.pack(f"<{len(offsets)}I", *offsets)
    payload = header + offset_table + bytes(blob)
    mapping = {item.decode("utf-8"): index for index, item in enumerate(ordered)}
    return payload, mapping


def decode_strs(payload: bytes) -> tuple[str, ...]:
    if len(payload) < 12:
        raise KtrfBinaryError("STRS payload is too small")
    string_count, byte_count = struct.unpack_from("<II", payload, 0)
    table_bytes = (string_count + 1) * 4
    data_offset = 8 + table_bytes
    if data_offset + byte_count != len(payload):
        raise KtrfBinaryError("STRS size fields do not match payload length")
    offsets = struct.unpack_from(f"<{string_count + 1}I", payload, 8)
    if not offsets or offsets[0] != 0 or offsets[-1] != byte_count:
        raise KtrfBinaryError("STRS boundary offsets are invalid")
    if any(left > right for left, right in zip(offsets, offsets[1:])):
        raise KtrfBinaryError("STRS offsets must be nondecreasing")

    blob = payload[data_offset:]
    values: list[str] = []
    for start, end in zip(offsets, offsets[1:]):
        raw = blob[start:end]
        if b"\0" in raw:
            raise KtrfBinaryError("STRS contains embedded NUL")
        try:
            values.append(raw.decode("utf-8"))
        except UnicodeDecodeError as exc:
            raise KtrfBinaryError("STRS contains invalid UTF-8") from exc

    if not values or values[0] != "":
        raise KtrfBinaryError("STRS index 0 must be the empty string")
    if len(set(values)) != len(values):
        raise KtrfBinaryError("STRS contains duplicate strings")
    encoded_tail = [value.encode("utf-8") for value in values[1:]]
    if encoded_tail != sorted(encoded_tail):
        raise KtrfBinaryError("STRS is not in canonical UTF-8 byte order")
    return tuple(values)
