# SPDX-License-Identifier: MIT
"""Losslessly pack transparent sprite TIMs with bounded zero/literal runs.

A control byte covers 1..128 bytes. Its high bit selects zeros; otherwise
those literal bytes follow. The runtime decodes directly into one shared,
word-aligned upload buffer, never a permanently expanded texture bank.
"""
from __future__ import annotations


def encode_zero_runs(source: bytes) -> bytes:
    output = bytearray()
    offset = 0
    while offset < len(source):
        zeros = 0
        while offset + zeros < len(source) and source[offset + zeros] == 0 and zeros < 128:
            zeros += 1
        if zeros >= 2:
            output.append(0x80 | (zeros - 1))
            offset += zeros
            continue
        start = offset
        offset += 1
        while offset < len(source) and offset - start < 128:
            if source[offset:offset + 2] == b"\0\0":
                break
            offset += 1
        output.append(offset - start - 1)
        output.extend(source[start:offset])
    return bytes(output)


def decode_zero_runs(source: bytes, expected_bytes: int) -> bytes:
    """Reference decoder for export validation; reject short/overlong streams."""
    output = bytearray()
    offset = 0
    while offset < len(source):
        token = source[offset]
        offset += 1
        count = (token & 127) + 1
        if len(output) + count > expected_bytes:
            raise ValueError("decoded texture exceeds its declared size")
        if token & 128:
            output.extend(b"\0" * count)
        else:
            if len(source) - offset < count:
                raise ValueError("truncated literal run")
            output.extend(source[offset:offset + count])
            offset += count
    if len(output) != expected_bytes:
        raise ValueError("decoded texture is shorter than its declared size")
    return bytes(output)
