# Experimental package-format module

This is a format probe rather than a compatibility commitment: the envelope
below is what the repository builds and reads today, and the open questions at
the end are the ones that must be answered before anybody publishes a package
against it.

This module tests the smallest useful binary boundary between independently built
games and runtimes. It owns container version values, stable section and record
identifiers, bounded parsing, compatibility checks, and stable load-error
categories. It does not own authoring schemas, gameplay rules, source diagnostics,
compression, asset conversion, or platform I/O.

## Mock envelope

All integers are little-endian and all section payloads begin at a four-byte
boundary.

| Field | Size |
|---|---:|
| Magic (`GLPK`) | 4 |
| Container major/minor | 4 |
| Header size and total package size | 8 |
| Stable game ID | 16 |
| Game content revision | 4 |
| Minimum/maximum engine-contract versions | 12 |
| Target profile | 4 |
| Required feature bits | 8 |
| Section count and directory offset | 8 |
| Envelope and directory checksum | 4 |

The fixed envelope is 72 bytes. It is followed by 32-byte directory entries:

| Field | Size |
|---|---:|
| Stable section type | 4 |
| Section schema major/minor | 4 |
| Flags, including `required` | 4 |
| Record count | 4 |
| Offset | 4 |
| Stored and unpacked sizes | 8 |
| Section checksum | 4 |

Each section contains variable-length records with a 64-bit stable ID, 32-bit
payload size, and aligned opaque payload. Classes, unit types, weapons, items,
maps, dialogue, and presentation are separate sections, so their counts scale
independently. The mock tests 5,000 definitions in one section; actual target
profiles impose explicit loader and content budgets rather than format constants.

Semantic schemas will define record payloads and cross-references. For example, a
unit type will reference class, weapon, and item definitions by stable ID rather
than directory position or pointer.

## The presentation section

The presentation section is the first one whose payload this document defines,
because it is the first one a renderer must read to draw a game the way its
author wrote it. It carries what an author chose about appearance, and what the
content itself looks like, and nothing a rule reads.

It is **optional**: its directory entry carries no `required` flag, so a
reader that does not know it skips it, and a package that carries no
presentation section loads without one. That is the test for every section
added after it: **required** when a runtime that skipped it would play a
different battle, **optional** when skipping it changes only how the same
battle is drawn. Its schema version is `1.0`. It holds records about the whole
game, addressed by stable IDs the **format** owns rather than by values a game
could collide with, the way the manifest section does. The list is append-only.

### Record 1: the project

The season the ground is drawn in, and the colour each faction's characters
wear.

| Field | Size |
|---|---:|
| Theme index, into the art library's theme menu | 1 |
| Faction count | 2 |
| Per faction: stable ID | 8 |
| Per faction: colour index, into the art library's colour menu | 1 |

A faction's colour is written **resolved**. The rule that a faction which chose
no colour takes the menu colour at its own position in the project's faction
list is authoring semantics, applied by `tools/game_content` before the package
is written, so no reader re-derives it and no two readers can disagree.

### Records 2 and 3: what the content looks like

A client that holds only a package can identify a map cell and a unit type but
cannot draw either, because both answers live in names it does not have: a
cell's identity is a hash of its authored terrain name, and a unit type's
archetype follows from the words in its class name. These two records carry the
joins, already resolved, for the same reason a faction's colour is resolved.

Record `2` is terrain identity to art-library terrain kind. Record `3` is unit
type identity to art-library archetype. Both share one shape:

| Field | Size |
|---|---:|
| Entry count | 4 |
| Per entry: stable ID | 8 |
| Per entry: art-library index | 1 |

Entries ascend by stable ID and each ID appears once, so a client binary
searches. A terrain kind may be the registry's "no keyword matched this name"
value; an archetype is always one the art library draws, because a unit type
naming none takes the roster's first.

### Reading them

`engine/package_runtime` decodes each record with the same bounded reader every
other section uses, and requires each payload to be consumed exactly: a record
that stops part-way through an entry, one whose declared count the remaining
bytes could not hold, one carrying bytes beyond what it declared, and one
naming a stable ID twice are all refused rather than partly believed. A later
field inside an existing record is therefore a schema minor bump and a reader
that knows it; a further record is neither, because a reader asks for the IDs
it knows and is unaffected by the ones it does not.

## The dialogue section

One record per scene, keyed by the scene's own stable ID.

| Field | Size |
|---|---:|
| Name, as a 16-bit length and its bytes | 2 + n |
| Line count | 2 |
| Per line: speaker, as a 16-bit length and its bytes | 2 + n |
| Per line: text, as a 16-bit length and its bytes | 2 + n |
| Backdrop index into the art library's backdrop menu, **plus one** | 0 or 1 |
| Cast size, 1 to 255 | 0 or 1 |
| Per cast entry: the unit type identity that speaker is | 0 or 8 each |
| Per line: which cast entry speaks it, **plus one** | 0 or 1 each |

Everything from the backdrop down is optional, and the optionality is the
record's own length rather than a flag. The reader consumes the payload
exactly and tells three cases apart by what is left after the last line:

| Left over | The scene |
|---|---|
| nothing | names no backdrop and casts nobody |
| exactly one byte | names a backdrop and casts nobody |
| more than one byte | casts somebody, and states its backdrop in the first of those bytes; zero there, and only there, means it names none |

Neither field costs a byte to a project that authors neither. The cast is
what lets a client draw *who is speaking*. Only the compiler sees the lines
and the cast together, so it makes the join from a speaker string to a cast
entry: a client reads a resolved index and compares no strings. What it does
with the unit type identity is the presentation section's business. The
**plus one** is what makes the states distinguishable: zero is not a valid
encoding of any backdrop, so a record padded or truncated to a zero byte is
refused rather than read as the menu's first entry, and a byte naming an entry
the reader's art library does not hold resolves to no backdrop rather than to
a neighbour.

## Compatibility experiment

Versions are intentionally independent:

- Container version controls the envelope and directory interpretation.
- Engine-contract range declares which runtime behavior the game requires.
- Every section has its own schema version.
- Content revision identifies a particular game build without changing schemas.
- Required feature bits negotiate optional engine capabilities.

The mock reader:

- accepts its container major and no newer container minor
- requires the running engine version to be inside the package range
- accepts portable packages or packages matching the active target profile
- rejects unsupported required feature bits
- skips unknown optional sections
- rejects unknown required sections and required schema majors
- validates metadata and payload checksums, non-overlapping bounds, configured
  counts, and duplicate IDs before returning data
- measures every declared count against the bytes actually present before
  reserving for it, so a small file cannot ask for a large allocation
- requires the alignment padding between sections to be zero and the last
  section to end the file, so two files with the same content cannot differ

FNV-1a is used only as a fast corruption probe. It is not a security boundary and
is not a final checksum decision.

The envelope hash covers the header and the directory and each section hashes
its own extent, which leaves the alignment padding between sections covered by
nothing. So the padding is required to be zero and the last section is required
to end the file. Without that a package could be respelled: two byte-different
files could decode to the same content, and every claim in this repository that
a package's md5 identifies its content would be a claim about one spelling of
it.

## Who owns a package's bytes

Two entry points, one validator.

`load_mock_package` takes a `std::vector<std::uint8_t>` and keeps a copy in
`LoadedPackage::bytes`, which is what nearly every caller wants.
`load_mock_package_in_place` takes a `PackageBytes` (pointer and size) and
copies nothing; the caller owes it that the bytes stay put and stay unchanged
for the package's lifetime. A cartridge is why it exists: the baked package
is a `constexpr` array in ROM, and reading it there is what lets a machine
with kilobytes of heap publish a campaign's board.

The owning load is the borrowing load plus a copy, so the two cannot drift,
and a damaged package is refused identically by both. The rule they share:
**nothing decoded out of a package holds a pointer into its bytes.** Every
`RecordView` addresses its payload by offset and resolves it against
`byte_data()` at the moment of asking. That is what makes a `LoadedPackage`
safe to copy and an owning package safe to append to while records are
assembled one at a time: a reallocation moves the buffer and changes no
answer.

## Questions this probe must answer

- Should the canonical package be little-endian, or should each target receive a
  native-endian derivative with a canonical gameplay-manifest comparison?
- Is four-byte alignment sufficient for N64 DMA and target asset sections?
  (Measured for the PlayStation: yes. MIPS-I has no 64-bit load or store, so
  the R3000A needs nothing wider, and the conformance executable checks it.
  The N64 DMA half is still open.)
- Should the directory permit multiple sections of one type for streaming/banks?
- Which hash/checksum balances tooling diagnostics and console cost?
- Which sections need compression, paging, or a secondary ID index?
- What maximum counts and working-set budgets belong to each target profile?
- How are schema migrations and optional fields represented inside record payloads?

No production package should be published until the host and every console
probe provide evidence for the applicable decisions.

## Tests

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -R grandleon.package_format --output-on-failure
```

The contract tests cover category round-trip and lookup, large record counts,
engine, container, schema and feature compatibility, optional evolution,
truncation, corrupt checksums, invalid offsets, duplicates, configured loader
budgets, and that a package read where its bytes already are decodes byte for
byte to what the same package copied decodes to. Both are refused identically
when the package is damaged.
