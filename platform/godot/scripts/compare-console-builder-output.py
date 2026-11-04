#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare a sealed builder's artifact with the ordinary repository pipeline."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


SECTOR_BYTES = 2352
USER_OFFSET = 24
USER_BYTES = 2048
GNU_BUILD_ID_NOTE = struct.pack("<III", 4, 20, 3) + b"GNU\0"


def fail(message: str) -> None:
    raise ValueError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def one_artifact(path: Path, suffix: str) -> Path:
    if path.is_file():
        if path.suffix.lower() != suffix:
            fail(f"expected a {suffix} artifact: {path}")
        return path
    matches = sorted(
        candidate for candidate in path.rglob(f"*{suffix}") if candidate.is_file()
    )
    if len(matches) != 1:
        fail(f"expected exactly one {suffix} artifact below {path}; found {len(matches)}")
    return matches[0]


def disc_pair(path: Path) -> tuple[Path, Path]:
    binary = one_artifact(path, ".bin")
    cue = binary.with_suffix(".cue") if path.is_file() else one_artifact(path, ".cue")
    if not cue.is_file():
        fail(f"disc cue is missing: {cue}")
    return binary, cue


def user_data(raw: bytes) -> bytearray:
    if not raw or len(raw) % SECTOR_BYTES:
        fail("PlayStation image is not a whole number of raw sectors")
    return bytearray(
        b"".join(
            raw[offset + USER_OFFSET : offset + USER_OFFSET + USER_BYTES]
            for offset in range(0, len(raw), SECTOR_BYTES)
        )
    )


def u32_both_endian(data: bytearray, offset: int) -> int:
    little = int.from_bytes(data[offset : offset + 4], "little")
    big = int.from_bytes(data[offset + 4 : offset + 8], "big")
    if little != big:
        fail("ISO 9660 both-endian integer disagrees")
    return little


def normalize_directory_dates(
    data: bytearray, extent: int, length: int, seen: set[int]
) -> None:
    if extent in seen:
        return
    seen.add(extent)
    start = extent * USER_BYTES
    end = start + length
    if start < 0 or end > len(data):
        fail("ISO 9660 directory is outside the image")
    offset = start
    while offset < end:
        record_length = data[offset]
        if record_length == 0:
            offset = ((offset // USER_BYTES) + 1) * USER_BYTES
            continue
        if record_length < 34 or offset + record_length > end:
            fail("ISO 9660 directory record is malformed")
        record_end = offset + record_length
        data[offset + 18 : offset + 25] = b"\0" * 7
        name_length = data[offset + 32]
        if offset + 33 + name_length > record_end:
            fail("ISO 9660 directory name is malformed")
        name = bytes(data[offset + 33 : offset + 33 + name_length])
        if data[offset + 25] & 0x02 and name not in (b"\0", b"\1"):
            child_extent = u32_both_endian(data, offset + 2)
            child_length = u32_both_endian(data, offset + 10)
            normalize_directory_dates(data, child_extent, child_length, seen)
        offset = record_end


def normalize_playstation_user_data(data: bytearray) -> int:
    pvd = 16 * USER_BYTES
    if data[pvd : pvd + 7] != b"\x01CD001\x01":
        fail("PlayStation image lacks its ISO 9660 primary volume descriptor")
    root = pvd + 156
    if data[root] < 34:
        fail("ISO 9660 root directory record is malformed")
    root_extent = u32_both_endian(data, root + 2)
    root_length = u32_both_endian(data, root + 10)
    data[root + 18 : root + 25] = b"\0" * 7
    for offset in (813, 830, 847, 864):
        data[pvd + offset : pvd + offset + 17] = b"\0" * 17
    normalize_directory_dates(data, root_extent, root_length, set())

    notes = []
    position = data.find(GNU_BUILD_ID_NOTE)
    while position >= 0:
        notes.append(position)
        position = data.find(GNU_BUILD_ID_NOTE, position + 1)
    if len(notes) != 1:
        fail(f"expected one GNU build-id note; found {len(notes)}")
    description = notes[0] + len(GNU_BUILD_ID_NOTE)
    data[description : description + 20] = b"\0" * 20
    return notes[0]


def compare_playstation(payload: Path, reference: Path) -> str:
    payload_bin, payload_cue = disc_pair(payload)
    reference_bin, reference_cue = disc_pair(reference)
    if payload_cue.read_bytes() != reference_cue.read_bytes():
        fail("PlayStation cue sheets differ")
    payload_raw = payload_bin.read_bytes()
    reference_raw = reference_bin.read_bytes()
    if len(payload_raw) != len(reference_raw):
        fail("PlayStation raw images have different lengths")

    changed_sectors = []
    for sector, offset in enumerate(range(0, len(payload_raw), SECTOR_BYTES)):
        left = payload_raw[offset : offset + SECTOR_BYTES]
        right = reference_raw[offset : offset + SECTOR_BYTES]
        if left[:USER_OFFSET] != right[:USER_OFFSET]:
            fail(f"PlayStation sector header differs at sector {sector}")
        left_user = left[USER_OFFSET : USER_OFFSET + USER_BYTES]
        right_user = right[USER_OFFSET : USER_OFFSET + USER_BYTES]
        if left_user == right_user:
            if left != right:
                fail(
                    "PlayStation sector coding differs without a user-data "
                    f"change at sector {sector}"
                )
        else:
            changed_sectors.append(sector)

    payload_user = user_data(payload_raw)
    reference_user = user_data(reference_raw)
    payload_note = normalize_playstation_user_data(payload_user)
    reference_note = normalize_playstation_user_data(reference_user)
    if payload_note != reference_note:
        fail("GNU build-id note moved within MAIN.EXE")
    if payload_user != reference_user:
        first = next(
            index
            for index, (left, right) in enumerate(zip(payload_user, reference_user))
            if left != right
        )
        fail(f"PlayStation user data differs after normalization at byte {first}")
    return (
        f"PlayStation normalized match: {len(payload_raw) // SECTOR_BYTES} sectors, "
        f"metadata changes in {len(changed_sectors)} sectors, "
        f"normalized SHA-256 {sha256(payload_user)}"
    )


def directory_record(extent: int, length: int, name: bytes, date: bytes) -> bytes:
    size = 33 + len(name) + (1 if len(name) % 2 == 0 else 0)
    record = bytearray(size)
    record[0] = size
    record[2:6] = extent.to_bytes(4, "little")
    record[6:10] = extent.to_bytes(4, "big")
    record[10:14] = length.to_bytes(4, "little")
    record[14:18] = length.to_bytes(4, "big")
    record[18:25] = date
    record[32] = len(name)
    record[33 : 33 + len(name)] = name
    return bytes(record)


def self_test() -> None:
    def fixture(date: bytes, build_id: bytes, semantic: int = 0) -> bytearray:
        data = bytearray(24 * USER_BYTES)
        pvd = 16 * USER_BYTES
        data[pvd : pvd + 7] = b"\x01CD001\x01"
        root = directory_record(22, USER_BYTES, b"\0", date)
        data[pvd + 156 : pvd + 156 + len(root)] = root
        data[pvd + 813 : pvd + 830] = b"2026010203040500\0"
        entries = root + directory_record(23, 128, b"MAIN.EXE;1", date)
        data[22 * USER_BYTES : 22 * USER_BYTES + len(entries)] = entries
        note = 23 * USER_BYTES + 64
        data[note : note + len(GNU_BUILD_ID_NOTE)] = GNU_BUILD_ID_NOTE
        start = note + len(GNU_BUILD_ID_NOTE)
        data[start : start + 20] = build_id
        data[note + 48] = semantic
        return data

    left = fixture(b"~\x01\x02\x03\x04\x05\x00", b"A" * 20)
    right = fixture(b"}\x06\x07\x08\x09\x0a\x00", b"B" * 20)
    normalize_playstation_user_data(left)
    normalize_playstation_user_data(right)
    assert left == right
    changed = fixture(b"}\x06\x07\x08\x09\x0a\x00", b"B" * 20, semantic=1)
    normalize_playstation_user_data(changed)
    assert left != changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--target", choices=("nintendo64", "playstation"))
    parser.add_argument("--payload", type=Path)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("console builder output comparator self-test passed")
        return 0
    if not args.target or args.payload is None or args.reference is None:
        parser.error("--target, --payload, and --reference are required")
    try:
        if args.target == "nintendo64":
            payload = one_artifact(args.payload, ".z64").read_bytes()
            reference = one_artifact(args.reference, ".z64").read_bytes()
            if payload != reference:
                fail("Nintendo 64 ROMs differ")
            print(
                f"Nintendo 64 exact match: {len(payload)} bytes, "
                f"SHA-256 {sha256(payload)}"
            )
        else:
            print(compare_playstation(args.payload, args.reference))
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
