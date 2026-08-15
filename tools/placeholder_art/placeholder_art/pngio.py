# SPDX-License-Identifier: MIT
"""A deterministic PNG encoder.

Pillow is used for rendering support elsewhere, but output is written here so
that the byte layout is fully under our control:

* chunks are exactly ``IHDR``, optional ``PLTE``/``tRNS``, ``IDAT``, ``IEND``;
* no ``tIME``, ``tEXt``, ``pHYs``, or any other metadata chunk is emitted;
* every scanline uses filter type 0 (None), so the filtered stream depends only
  on the pixels;
* deflate runs at a fixed level with fixed window and memory parameters.

The remaining variable is the zlib build itself. Within one environment, and
therefore within CI and within ``--check``, output is byte-identical. The
pinned ``requirements.txt`` records the environment the checked-in bytes were
produced with.

Sub-byte depths are packed here too, 4bpp being what CI4 is, and that is the
other reason not to delegate to a library: an indexed profile needs the exact
bit order its target expects, high nibble leftmost.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path
from typing import Iterable, Sequence, Tuple

_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def _deflate(raw: bytes) -> bytes:
    compressor = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=15, memLevel=8,
                                  strategy=zlib.Z_DEFAULT_STRATEGY)
    return compressor.compress(raw) + compressor.flush()


def _assemble(width: int, height: int, bit_depth: int, colour_type: int,
              scanlines: bytes, extra: Iterable[bytes] = ()) -> bytes:
    header = struct.pack(">IIBBBBB", width, height, bit_depth, colour_type, 0, 0, 0)
    parts = [_SIGNATURE, _chunk(b"IHDR", header)]
    parts.extend(extra)
    parts.append(_chunk(b"IDAT", _deflate(scanlines)))
    parts.append(_chunk(b"IEND", b""))
    return b"".join(parts)


def encode_rgba(width: int, height: int, pixels: Sequence[Tuple[int, int, int, int]]) -> bytes:
    """Encode 8-bit RGBA (colour type 6)."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        base = y * width
        for x in range(width):
            rows.extend(pixels[base + x])
    return _assemble(width, height, 8, 6, bytes(rows))


def encode_indexed(
    width: int,
    height: int,
    indices: Sequence[int],
    palette: Sequence[Tuple[int, int, int]],
    transparent: Sequence[int] = (),
    bit_depth: int = 8,
) -> bytes:
    """Encode an indexed image (colour type 3) at 1, 2, 4, or 8 bits per pixel.

    ``transparent`` lists palette indices that are fully transparent; a ``tRNS``
    chunk covering the palette prefix up to the highest such index is emitted.
    """
    assert bit_depth in (1, 2, 4, 8), f"unsupported bit depth {bit_depth}"
    assert len(palette) <= (1 << bit_depth), (
        f"palette of {len(palette)} entries does not fit {bit_depth}bpp"
    )
    rows = bytearray()
    per_byte = 8 // bit_depth
    for y in range(height):
        rows.append(0)
        base = y * width
        if bit_depth == 8:
            rows.extend(indices[base:base + width])
            continue
        accumulator = 0
        filled = 0
        for x in range(width):
            accumulator = (accumulator << bit_depth) | (indices[base + x] & ((1 << bit_depth) - 1))
            filled += 1
            if filled == per_byte:
                rows.append(accumulator)
                accumulator = 0
                filled = 0
        if filled:
            rows.append(accumulator << (bit_depth * (per_byte - filled)))

    plte = bytearray()
    for red, green, blue in palette:
        plte.extend((red, green, blue))
    extra = [_chunk(b"PLTE", bytes(plte))]
    if transparent:
        alpha = bytearray(255 for _ in range(max(transparent) + 1))
        for index in transparent:
            alpha[index] = 0
        extra.append(_chunk(b"tRNS", bytes(alpha)))
    return _assemble(width, height, bit_depth, 3, bytes(rows), extra)


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
