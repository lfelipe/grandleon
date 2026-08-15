#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""The one-shot path that produced the golden migration fixtures.

The two `.glsv` files beside this script are saves this build's writer cannot
write, which is the whole reason they exist. A migration test whose input came
out of the current encoder proves that the current encoder round-trips; it
proves nothing at all about loading a save that predates it. So the old bytes
are produced here, once, by an encoder that is deliberately not the engine's,
and then committed.

This script is a record of how those bytes were made, not a step in the build.
Nothing in CI runs it. Re-running it must produce the committed files byte for
byte; if it does not, the thing to fix is this script or the change that moved
underneath it -- never the fixtures, and never by regenerating them so that a
failing test passes. A fixture regenerated to match new behaviour has been
turned inside out: it no longer pins anything.

The layouts below are `engine/campaign/include/grandleon/campaign/save.hpp`'s,
transcribed. `roster` schema 0 is the historical layout documented in
`engine/campaign/include/grandleon/campaign/migration.hpp`.

    python3 tests/campaign/fixtures/generate_legacy_saves.py
"""

import pathlib
import struct

FNV_OFFSET_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = 0xFFFFFFFFFFFFFFFF

HEADER_SIZE = 64
PACKAGE_ENTRY_SIZE = 32
DIRECTORY_ENTRY_SIZE = 32
SECTION_ALIGNMENT = 4
REQUIRED = 1

# core::ContentCategory
UNIT_TYPE = 3
ITEM = 5
OBJECTIVE = 12
CAMPAIGN = 14
CAMPAIGN_NODE = 15

# The engine that "wrote" these saves: older than this build, which is what an
# old save on a card looks like and what the current writer cannot stamp -- it
# always stamps core::engine_version().
WRITING_ENGINE = (0, 0, 9)
RULES_CONTRACT = 1


def fnv1a64(data):
    value = FNV_OFFSET_BASIS
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & MASK64
    return value


def package_id(marker):
    """The convention tests/campaign uses: a zero identity with one marker."""
    identity = bytearray(16)
    identity[15] = marker
    return bytes(identity)


def ref(package, category, stable_id):
    return package + struct.pack("<IQ", category, stable_id)


def stack(package, category, stable_id, quantity):
    return ref(package, category, stable_id) + struct.pack("<I", quantity)


def roster_schema_0(units):
    """id, definition, availability, one reserved byte, level. No experience,
    and no carried inventory: a schema 0 campaign held its whole stock in the
    shared store."""
    out = struct.pack("<I", len(units))
    for identity, definition, availability, level in units:
        out += struct.pack("<Q", identity)
        out += definition
        out += struct.pack("<BBH", availability, 0, level)
    return out


def roster_schema_1(units):
    out = struct.pack("<I", len(units))
    for identity, definition, availability, level, experience, carried in units:
        out += struct.pack("<Q", identity)
        out += definition
        out += struct.pack("<BBHII", availability, 0, level, experience, len(carried))
        for item in carried:
            out += item
    return out


def stacks_section(items):
    return struct.pack("<I", len(items)) + b"".join(items)


def objectives_section(records):
    out = struct.pack("<I", len(records))
    for definition, result in records:
        out += definition + struct.pack("<BBH", result, 0, 0)
    return out


def world_section(records):
    out = struct.pack("<I", len(records))
    for definition, kind, value in records:
        out += definition + struct.pack("<BBHq", kind, 0, 0, value)
    return out


def outcomes_section(ids):
    return struct.pack("<I", len(ids)) + b"".join(
        struct.pack("<Q", value) for value in ids
    )


def progression_section(campaign, active_node, history):
    out = campaign + active_node + struct.pack("<I", len(history))
    for node, cause in history:
        out += node + struct.pack("<Q", cause)
    return out


def envelope(packages, sections):
    """sections: list of (type, schema_major, schema_minor, flags, bytes),
    ascending type."""
    directory_offset = HEADER_SIZE + len(packages) * PACKAGE_ENTRY_SIZE
    metadata_end = directory_offset + len(sections) * DIRECTORY_ENTRY_SIZE

    body = bytearray()
    entries = []
    for section_type, major, minor, flags, payload in sections:
        while (metadata_end + len(body)) % SECTION_ALIGNMENT != 0:
            body.append(0)
        offset = metadata_end + len(body)
        body += payload
        entries.append(
            struct.pack(
                "<IHHIIIIQ",
                section_type,
                major,
                minor,
                flags,
                offset,
                len(payload),
                0,
                fnv1a64(payload),
            )
        )

    total = metadata_end + len(body)
    header = bytearray()
    header += b"GLSV"
    header += struct.pack("<HH", 1, 0)
    header += struct.pack("<II", HEADER_SIZE, total)
    header += struct.pack("<HHHH", *WRITING_ENGINE, 0)
    header += struct.pack("<I", RULES_CONTRACT)
    header += struct.pack("<II", len(packages), HEADER_SIZE)
    header += struct.pack("<II", len(sections), directory_offset)
    header += struct.pack("<I", 0)
    header += struct.pack("<QQ", 0, 0)
    assert len(header) == HEADER_SIZE

    table = b""
    for identity, revision, integrity in packages:
        table += identity + struct.pack("<IIQ", revision, 0, integrity)

    metadata = bytes(header) + table + b"".join(entries)
    assert len(metadata) == metadata_end
    # The envelope checksum covers the metadata with its own eight bytes zero,
    # which is what they still are.
    checksum = fnv1a64(metadata)
    metadata = metadata[:48] + struct.pack("<Q", checksum) + metadata[56:]
    return metadata + bytes(body)


def roster_schema_0_save():
    """Fixture one: the roster section at schema 0, everything else at 1.

    Pins the section-schema axis. The current writer stamps roster schema 1 and
    has no way to be asked for schema 0, so these bytes cannot come from it.
    """
    package = package_id(1)
    sections = [
        (
            1,
            0,
            0,
            REQUIRED,
            roster_schema_0(
                [
                    (1, ref(package, UNIT_TYPE, 0x1001), 2, 3),
                    (2, ref(package, UNIT_TYPE, 0x1002), 4, 1),
                ]
            ),
        ),
        (
            2,
            1,
            0,
            REQUIRED,
            stacks_section(
                [
                    stack(package, ITEM, 0x2001, 5),
                    stack(package, ITEM, 0x2002, 2),
                ]
            ),
        ),
        (3, 1, 0, REQUIRED, objectives_section([(ref(package, OBJECTIVE, 0x3001), 1)])),
        (4, 1, 0, REQUIRED, world_section([(ref(package, CAMPAIGN, 0x4001), 1, 1)])),
        (5, 1, 0, REQUIRED, outcomes_section([0x5001])),
        (
            6,
            1,
            0,
            0,
            progression_section(
                ref(package, CAMPAIGN, 0x6001),
                ref(package, CAMPAIGN_NODE, 0x7001),
                [(ref(package, CAMPAIGN_NODE, 0x7001), 0)],
            ),
        ),
    ]
    return envelope([(package, 1, 0xA1A2A3A4B1B2B3B4)], sections)


def content_revision_1_save():
    """Fixture two: current section schemas, content revision 1.

    Pins the content axis. Every section is at the schema this build writes;
    what is old is the content the references name. The mounted package is at
    revision 2, where `unit_type 0x1101` and `item 0x2101` no longer exist under
    those identities, so no campaign this build could assemble would encode
    them -- and the older writing-engine stamp is one the current writer cannot
    produce either.
    """
    package = package_id(2)
    sections = [
        (
            1,
            1,
            0,
            REQUIRED,
            roster_schema_1(
                [
                    (
                        1,
                        ref(package, UNIT_TYPE, 0x1101),
                        2,
                        4,
                        250,
                        [stack(package, ITEM, 0x2101, 2)],
                    ),
                    (2, ref(package, UNIT_TYPE, 0x1102), 4, 2, 90, []),
                ]
            ),
        ),
        (
            2,
            1,
            0,
            REQUIRED,
            stacks_section(
                [
                    stack(package, ITEM, 0x2101, 7),
                    stack(package, ITEM, 0x2105, 3),
                ]
            ),
        ),
        (3, 1, 0, REQUIRED, objectives_section([(ref(package, OBJECTIVE, 0x3101), 2)])),
        (
            4,
            1,
            0,
            REQUIRED,
            world_section([(ref(package, CAMPAIGN, 0x4101), 2, -1234)]),
        ),
        (5, 1, 0, REQUIRED, outcomes_section([0x5101])),
        (
            6,
            1,
            0,
            0,
            progression_section(
                ref(package, CAMPAIGN, 0x6101),
                ref(package, CAMPAIGN_NODE, 0x7101),
                [(ref(package, CAMPAIGN_NODE, 0x7101), 0)],
            ),
        ),
    ]
    return envelope([(package, 1, 0xC1C2C3C4D1D2D3D4)], sections)


def main():
    here = pathlib.Path(__file__).resolve().parent
    (here / "roster_schema_0.glsv").write_bytes(roster_schema_0_save())
    (here / "content_revision_1.glsv").write_bytes(content_revision_1_save())


if __name__ == "__main__":
    main()
