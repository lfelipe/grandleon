#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Writes memory card images this game must refuse and must not write to.

A memory card is shared. Every other game the player owns has its files on the
same fifteen blocks, and the directory that says which blocks are whose is
data the card hands over rather than anything this game controls. So the one
thing `psx_card.cpp` must never do is take a block it does not own and commit
over it. That is somebody's save, and it does not come back.

Two cards, each the shortest one that asks its question:

  chained   the directory holds a file under *this game's* name whose first
            block chains on into a block another game's file starts in. A
            reader that bounded the link and stopped there would adopt both
            blocks and overwrite the second one on the first save.
  damaged   the directory is full of another game's files and the one entry
            that says it is free does not match its own checksum. An entry the
            card cannot vouch for may still belong to the file whose entry was
            the thing that got damaged, so it is not a block to hand out.

Both must end as a refusal with nothing written, which the run script checks by
taking each image's digest before and after.

    platform/playstation/scripts/write-foreign-card.py chained <path>
    platform/playstation/scripts/write-foreign-card.py damaged <path>

The layout is the one `platform/playstation/src/psx_card.h` documents in full;
everything here is that format and nothing else.
"""

import sys
from typing import Callable

FRAME_BYTES = 128
CARD_FRAMES = 1024
BLOCK_FRAMES = 64
DIRECTORY_BLOCKS = 15

# The name this game recognises its own file by, out of psx_card.h.
OUR_NAME = b"BESLES-00000CAMPAIGN"
# Somebody else's, in the same twenty-byte convention.
THEIR_NAME = b"BASCUS-94163SOMEONE1"

# What the other game's blocks are filled with.
THEIR_FILL = 0xC7


def checksummed(frame: bytearray) -> bytearray:
    """The exclusive-or of the 127 bytes before it, in the last one."""
    total = 0
    for byte in frame[: FRAME_BYTES - 1]:
        total ^= byte
    frame[FRAME_BYTES - 1] = total
    return frame


def directory_entry(state: int, size: int, nxt: int, name: bytes) -> bytearray:
    frame = bytearray(FRAME_BYTES)
    frame[0] = state
    frame[4:8] = size.to_bytes(4, "little")
    frame[8:10] = nxt.to_bytes(2, "little")
    frame[10 : 10 + len(name)] = name
    return checksummed(frame)


def free_entry() -> bytearray:
    frame = bytearray(FRAME_BYTES)
    frame[0] = 0xA0
    frame[8:10] = (0xFFFF).to_bytes(2, "little")
    return checksummed(frame)


def formatted_card() -> tuple[bytearray, Callable[[int, bytes], None]]:
    """An empty formatted card, and the setter for one of its frames."""
    image = bytearray(CARD_FRAMES * FRAME_BYTES)

    def put(frame_index: int, frame: bytes) -> None:
        at = frame_index * FRAME_BYTES
        image[at : at + FRAME_BYTES] = frame

    # Frame zero: the card is formatted, so the game gets past the header and
    # has to decide on the directory rather than on two letters.
    header = bytearray(FRAME_BYTES)
    header[0:2] = b"MC"
    put(0, checksummed(header))
    return image, put


def build_chained() -> bytearray:
    image, put = formatted_card()

    # Frame one describes block one: this game's file, first block, chaining
    # on to the entry after it. The link is a data-block index counted from the
    # first data block, so 1 names block two.
    put(1, directory_entry(0x51, 2 * BLOCK_FRAMES * FRAME_BYTES, 1, OUR_NAME))

    # Frame two describes block two: another game's file, and the *first* block
    # of it. In range, and not this file's to take.
    put(2, directory_entry(0x51, BLOCK_FRAMES * FRAME_BYTES, 0xFFFF, THEIR_NAME))

    for block in range(3, DIRECTORY_BLOCKS + 1):
        put(block, free_entry())

    # Their block's contents, so the block this game must not adopt is legible
    # in the image rather than indistinguishable from the empty card around it.
    for frame in range(2 * BLOCK_FRAMES, 3 * BLOCK_FRAMES):
        put(frame, bytes([THEIR_FILL]) * FRAME_BYTES)

    return image


def build_damaged() -> bytearray:
    image, put = formatted_card()

    # Thirteen blocks another game holds, each its own one-block file, so the
    # card has exactly the two blocks left that this game's file needs.
    for block in range(1, DIRECTORY_BLOCKS - 1):
        name = THEIR_NAME[:-2] + (b"%02d" % block)
        put(block, directory_entry(0x51, BLOCK_FRAMES * FRAME_BYTES, 0xFFFF, name))
        for frame in range(block * BLOCK_FRAMES, (block + 1) * BLOCK_FRAMES):
            put(frame, bytes([THEIR_FILL]) * FRAME_BYTES)

    # The fourteenth is free and says so intact.
    put(DIRECTORY_BLOCKS - 1, free_entry())

    # The fifteenth says it is free while failing its own checksum. The card
    # cannot vouch for the byte that says so, and a game that took the block on
    # that byte alone would be writing over whatever the entry used to
    # describe. Two blocks are needed and only one is on offer, so the right
    # answer is that the card is full.
    damaged = free_entry()
    damaged[FRAME_BYTES - 1] ^= 0xFF
    put(DIRECTORY_BLOCKS, damaged)
    for frame in range(DIRECTORY_BLOCKS * BLOCK_FRAMES,
                       (DIRECTORY_BLOCKS + 1) * BLOCK_FRAMES):
        put(frame, bytes([THEIR_FILL]) * FRAME_BYTES)

    return image


CARDS = {"chained": build_chained, "damaged": build_damaged}


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in CARDS:
        print(f"usage: {argv[0]} {{{'|'.join(CARDS)}}} <path>", file=sys.stderr)
        return 2
    with open(argv[2], "wb") as out:
        out.write(CARDS[argv[1]]())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
