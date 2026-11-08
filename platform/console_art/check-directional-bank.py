#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Focused round-trip, fallback, corruption and budget checks for the bank."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import directional_bank as bank


def frame(seed: int) -> bytes:
    texels = bytes((seed + offset) & 0xFF for offset in range(bank.TEXEL_BYTES))
    palette = b"\0\0" + bytes((seed * 7 + offset) & 0xFF for offset in range(30))
    return texels + palette


def record(identity: bank.Identity, seed: int) -> bank.SourceRecord:
    return bank.SourceRecord(identity, tuple(frame(seed + at) for at in range(4)))


def refuses(raw: bytes, words: str) -> None:
    try:
        bank.decode(raw)
    except bank.DirectionalBankError:
        return
    raise AssertionError(f"directional bank accepted {words}")


def repaired(candidate: bytearray) -> bytes:
    """Return corrupted bytes with a matching CRC, to exercise structure checks."""
    candidate[bank.CRC_OFFSET:bank.CRC_OFFSET + 4] = b"\0\0\0\0"
    checksum = bank._checksum(bytes(candidate))
    candidate[bank.CRC_OFFSET:bank.CRC_OFFSET + 4] = checksum.to_bytes(4, "little")
    return bytes(candidate)


def main() -> None:
    first = record(bank.Identity(0, 0, 0, 0), 3)
    second = record(bank.Identity(2, 1, 6, 4), 19)
    encoded = bank.encode((second, first))
    assert encoded == bank.encode((first, second)), "record order changed bank bytes"
    decoded = bank.decode(encoded)
    assert tuple(item.identity for item in decoded.records) == (
        first.identity, second.identity,
    )
    assert bytes(decoded.lookup(second.identity, 3) or b"") == second.frames[3]
    assert decoded.lookup(bank.Identity(1, 0, 0, 0), 0) is None
    try:
        decoded.lookup(bank.Identity(7, 0, 0, 0), 0)
    except bank.DirectionalBankError:
        pass
    else:
        raise AssertionError("directional lookup accepted an invalid identity")
    try:
        decoded.lookup(first.identity, 4)
    except bank.DirectionalBankError:
        pass
    else:
        raise AssertionError("directional lookup accepted a fifth frame")

    duplicate = replace(second, identity=first.identity)
    try:
        bank.encode((first, duplicate))
    except bank.DirectionalBankError:
        pass
    else:
        raise AssertionError("directional encoder accepted a duplicate identity")

    refuses(encoded[:-1], "a truncated payload")
    damaged = bytearray(encoded)
    damaged[-1] ^= 0x80
    refuses(bytes(damaged), "a damaged payload")
    damaged = bytearray(encoded)
    damaged[0] ^= 0x01
    refuses(bytes(damaged), "a damaged magic")
    damaged = bytearray(encoded)
    damaged[bank.HEADER_SIZE + 4] ^= 0x08
    refuses(repaired(damaged), "an out-of-sequence record offset with a repaired CRC")
    damaged = bytearray(encoded)
    damaged[bank.HEADER_SIZE] = 7
    refuses(repaired(damaged), "an out-of-range identity with a repaired CRC")
    damaged = bytearray(encoded)
    palette = decoded.records[0].offset + bank.TEXEL_BYTES
    damaged[palette] = 1
    # Repairing the whole-file checksum must not make an invalid palette legal.
    refuses(repaired(damaged), "an opaque palette index zero with a repaired CRC")

    assert bank.FRAME_BYTES == 2080
    assert bank.index_bytes(112) == 896
    assert bank.payload_bytes(112) == 931_840
    assert bank.bank_bytes(112) == 932_776
    try:
        bank.decode(encoded, maximum_records=1)
    except bank.DirectionalBankError:
        pass
    else:
        raise AssertionError("directional decoder ignored its caller bound")

    print(
        "directional bank check passed: deterministic round-trip, exact "
        "fallback, corruption rejection; full index 896 bytes, payload "
        "931840 bytes, bank 932776 bytes"
    )


if __name__ == "__main__":
    main()
