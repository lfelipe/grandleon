#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Portable, file-backed CI4 directional character banks.

The bank is deliberately independent of either console renderer.  It keeps the
large north/south payload out of an executable while giving both targets one
small, deterministic index to validate before they allocate a cache.
"""

from __future__ import annotations

from bisect import bisect_left
from dataclasses import dataclass
import struct
import zlib
from typing import Iterable, Sequence


MAGIC = b"GLDIRC1\0"
VERSION = 1
SIDE = 64
FRAMES_PER_DRAWING = 4
PALETTE_ENTRIES = 16
TEXEL_BYTES = SIDE * SIDE // 2
PALETTE_BYTES = PALETTE_ENTRIES * 2
FRAME_BYTES = TEXEL_BYTES + PALETTE_BYTES
HEADER = struct.Struct("<8sHHHHHHIIIII")
RECORD = struct.Struct("<BBBBI")
HEADER_SIZE = HEADER.size
RECORD_SIZE = RECORD.size
CRC_OFFSET = HEADER_SIZE - 4

# Seven styles, two bodies, eight roles and six team colours is every identity
# the portable project format can ask a console bank to distinguish.
MAX_RECORDS = 7 * 2 * 8 * 6


class DirectionalBankError(ValueError):
    """The bank cannot be trusted or addressed safely."""


@dataclass(frozen=True, order=True)
class Identity:
    style: int
    figure: int
    archetype: int
    colour: int

    def validate(self) -> None:
        limits = (7, 2, 8, 6)
        values = (self.style, self.figure, self.archetype, self.colour)
        if any(value < 0 or value >= limit for value, limit in zip(values, limits)):
            raise DirectionalBankError(f"directional identity is out of range: {self}")


@dataclass(frozen=True)
class SourceRecord:
    identity: Identity
    frames: tuple[bytes, bytes, bytes, bytes]


@dataclass(frozen=True)
class BankRecord:
    identity: Identity
    offset: int


@dataclass(frozen=True)
class DirectionalBank:
    raw: bytes
    records: tuple[BankRecord, ...]
    checksum: int

    def lookup(self, identity: Identity, frame: int) -> memoryview | None:
        """Return one fixed-size frame, or ``None`` for exact side fallback."""
        identity.validate()
        if frame < 0 or frame >= FRAMES_PER_DRAWING:
            raise DirectionalBankError(f"directional frame is out of range: {frame}")
        keys = tuple(record.identity for record in self.records)
        at = bisect_left(keys, identity)
        if at >= len(self.records) or self.records[at].identity != identity:
            return None
        start = self.records[at].offset + frame * FRAME_BYTES
        end = start + FRAME_BYTES
        return memoryview(self.raw)[start:end]


def index_bytes(record_count: int) -> int:
    if record_count < 0 or record_count > MAX_RECORDS:
        raise DirectionalBankError(f"directional record count is out of range: {record_count}")
    return record_count * RECORD_SIZE


def payload_bytes(record_count: int) -> int:
    if record_count < 0 or record_count > MAX_RECORDS:
        raise DirectionalBankError(f"directional record count is out of range: {record_count}")
    return record_count * FRAMES_PER_DRAWING * FRAME_BYTES


def bank_bytes(record_count: int) -> int:
    return HEADER_SIZE + index_bytes(record_count) + payload_bytes(record_count)


def _header(record_count: int, file_size: int, checksum: int) -> bytes:
    return HEADER.pack(
        MAGIC, VERSION, HEADER_SIZE, SIDE, FRAMES_PER_DRAWING,
        PALETTE_ENTRIES, RECORD_SIZE, record_count, HEADER_SIZE,
        HEADER_SIZE + index_bytes(record_count), file_size, checksum,
    )


def _checksum(raw: bytes) -> int:
    candidate = bytearray(raw)
    candidate[CRC_OFFSET:CRC_OFFSET + 4] = b"\0\0\0\0"
    return zlib.crc32(candidate) & 0xFFFFFFFF


def encode(records: Iterable[SourceRecord]) -> bytes:
    """Encode sorted identity records into reproducible bytes."""
    ordered = sorted(records, key=lambda record: record.identity)
    if len(ordered) > MAX_RECORDS:
        raise DirectionalBankError("directional bank exceeds its identity bound")
    previous: Identity | None = None
    for record in ordered:
        record.identity.validate()
        if previous == record.identity:
            raise DirectionalBankError(f"duplicate directional identity: {record.identity}")
        previous = record.identity
        if len(record.frames) != FRAMES_PER_DRAWING:
            raise DirectionalBankError("directional record does not carry four frames")
        for frame in record.frames:
            if len(frame) != FRAME_BYTES:
                raise DirectionalBankError(
                    f"directional frame is {len(frame)} bytes, expected {FRAME_BYTES}"
                )
            if frame[TEXEL_BYTES:TEXEL_BYTES + 2] != b"\0\0":
                raise DirectionalBankError("directional palette index zero is not transparent")

    count = len(ordered)
    size = bank_bytes(count)
    data_offset = HEADER_SIZE + index_bytes(count)
    index = bytearray()
    payload = bytearray()
    for number, record in enumerate(ordered):
        identity = record.identity
        offset = data_offset + number * FRAMES_PER_DRAWING * FRAME_BYTES
        index.extend(RECORD.pack(
            identity.style, identity.figure, identity.archetype,
            identity.colour, offset,
        ))
        payload.extend(b"".join(record.frames))
    raw = _header(count, size, 0) + bytes(index) + bytes(payload)
    checksum = _checksum(raw)
    return _header(count, size, checksum) + raw[HEADER_SIZE:]


def decode(raw: bytes, maximum_records: int = MAX_RECORDS) -> DirectionalBank:
    """Validate a complete bank before exposing any lookup or frame slice."""
    if len(raw) < HEADER_SIZE:
        raise DirectionalBankError("directional bank is shorter than its header")
    (
        magic, version, header_size, side, frames_per_drawing,
        palette_entries, record_size, record_count, index_offset, data_offset,
        file_size, checksum,
    ) = HEADER.unpack_from(raw)
    if magic != MAGIC or version != VERSION:
        raise DirectionalBankError("directional bank has an unknown format")
    if (
        header_size != HEADER_SIZE or side != SIDE or
        frames_per_drawing != FRAMES_PER_DRAWING or
        palette_entries != PALETTE_ENTRIES or record_size != RECORD_SIZE
    ):
        raise DirectionalBankError("directional bank geometry does not match version 1")
    if maximum_records < 0 or record_count > min(maximum_records, MAX_RECORDS):
        raise DirectionalBankError("directional bank exceeds the caller's record bound")
    if (
        index_offset != HEADER_SIZE or
        data_offset != HEADER_SIZE + index_bytes(record_count) or
        file_size != bank_bytes(record_count) or file_size != len(raw)
    ):
        raise DirectionalBankError("directional bank offsets or length are inconsistent")
    if checksum != _checksum(raw):
        raise DirectionalBankError("directional bank checksum does not match its bytes")

    records: list[BankRecord] = []
    previous: Identity | None = None
    stride = FRAMES_PER_DRAWING * FRAME_BYTES
    for number in range(record_count):
        at = index_offset + number * RECORD_SIZE
        style, figure, archetype, colour, offset = RECORD.unpack_from(raw, at)
        identity = Identity(style, figure, archetype, colour)
        identity.validate()
        if previous is not None and identity <= previous:
            raise DirectionalBankError("directional index is duplicated or not sorted")
        if offset != data_offset + number * stride or offset + stride > len(raw):
            raise DirectionalBankError("directional record points outside its payload")
        for frame in range(FRAMES_PER_DRAWING):
            palette = offset + frame * FRAME_BYTES + TEXEL_BYTES
            if raw[palette:palette + 2] != b"\0\0":
                raise DirectionalBankError("directional palette index zero is not transparent")
        records.append(BankRecord(identity, offset))
        previous = identity
    return DirectionalBank(raw, tuple(records), checksum)
