# Storage

Named slots of opaque bytes. A save goes in; the same bytes come back out.

This is the platform half of persistent campaign state. `engine/campaign` turns
a campaign into a byte vector and back (`save.hpp`); this directory is where
that vector goes when the program stops running. The two never meet in a header:
the save layer does not know a device exists, and the storage layer does not
know a save has a magic number.

## Two libraries, on purpose

| Target | What it is | What it links |
| --- | --- | --- |
| `grandleon_storage` | the `SlotStorage` interface, the slot-name rules, the error vocabulary, the in-memory implementation, and the slot directory a cartridge's flat bytes are read through | nothing |
| `grandleon_storage_filesystem` | one file per slot in one directory, for the desktop | `grandleon_storage` |

The split is the point of this README.

`FilesystemSlotStorage` is the first thing in this repository below the tools
layer that touches a filesystem. Everything else that opens a file is a host
tool. The dependency rule of [DESIGN.md](../../DESIGN.md) §3.1 says why: the
engine must build for a cartridge, and a cartridge has no `<filesystem>`, no
`fopen`, and no path. A single `grandleon_storage` target holding both the
interface and the desktop implementation would put `#include <filesystem>` on
the far side of a link line that a Nintendo 64 build follows, and a convenience
function added to the interface's own translation unit would be discovered by
the cross build.

So the interface is one target with no dependencies at all, and the filesystem
is a second target that the engine libraries never link, that the cross builds
never configure, and that a desktop binary chooses explicitly by constructing
one and passing the base-class reference along. Neither is in the installed
package SDK: a game package is content, and content does not open save files.

The consoles came next, and they did not need a target each.

## The cartridge, which is a directory and not a device

A Nintendo 64 cartridge with SRAM is thirty-two kilobytes on the peripheral
bus, with no name, no length, and no second slot: it has addresses. So what a
console adapter is missing is not a device but a **directory**. A directory
written once per console is one that can disagree with itself about what a slot
list is.

`ByteWindowSlotStorage` (`byte_window_storage.hpp`) is that directory, written
once, in the target that links nothing. It keeps the whole contract below
against a `ByteWindow`: a fixed-size region of bytes with `read`, `write` and
`commit`, and no pointer.
The absence of the pointer is the point. On a Nintendo 64 the cartridge bus is
not memory the CPU may `memcpy`; a read is a DMA and so is a write. An
interface handing out a `std::uint8_t*` would be one no console could
implement.

What the Nintendo 64 adds is `platform/nintendo64/src/sram_window.h`: a shadow
of the cartridge in RDRAM, one DMA in at boot and one DMA out per commit. It is
about forty lines and it says nothing about what a slot is.

**Hardware the CPU can store straight into needs less than that, and that it
needs less is what proves the seam was cut in the right place.** Such a window
overrides `read` and `write`, keeps no shadow at all, and leaves `commit()`
unoverridden because a byte is on the device the instant it is stored. Either
shape says nothing about what a slot is, and both share every line of the
directory, the budget arithmetic, the ordering and the refusals. A save one
device holds is a save another reads, byte for byte.

The layout is fixed and big-endian throughout: sixteen bytes of header
(magic, format version, slot count, payload length, an FNV-1a checksum over
everything after it), then one thirty-six byte directory entry per slot, then
the payloads in the same order. Big-endian because the console is and the host
suite is not, and a field whose byte order is a property of whoever wrote it is
a save that only round-trips on one machine.

Two decisions in it are worth stating rather than inferring:

- **A window that does not parse is an empty device, not a broken one.** A
  cartridge nobody has saved to holds whatever it holds: all zeroes, all ones,
  a previous game's save. Every one of those has to read as "no slots" rather
  than as a directory, or the first save menu a player sees is built out of
  noise. `was_formatted()` is how a caller tells the two apart when it cares.
- **The body is written before the header.** A device that dies between them
  leaves a header describing the image that *was* there, whose checksum then
  fails. The failure mode is an empty card rather than a plausible wrong one.
  It is the same argument the filesystem adapter's rename makes, made with the
  only ordering a cartridge has.

The device runs on a host, against `VectorByteWindow`, through the same
`the_slot_contract` and `the_budget_contract` the other two pass. It runs at a
cartridge's own budget, so the out-of-space paths are the cartridge's. That
leaves an emulator run only the part a host cannot prove: that the region
survives the power switch.

## What the interface refuses to be

There are no directories, no paths, no handles, no seeking, no partial reads,
and no timestamps. That is not minimalism for its own sake; it is the
intersection of the targets. A Nintendo 64 has EEPROM of 512 or 2,048
bytes addressed in sixty-four-bit blocks, or a byte-wide battery-backed SRAM window.
A browser has a key-value store whose values are strings. "A short name, a blob,
all of it at once" is what all of those can do, so that is the interface. The
desktop adapter is the one that gives something up, rather than the consoles
inventing something they do not have.

Slot names follow from the same reasoning. Lowercase ASCII letters, digits, `_`
and `-`, one to thirty-one characters, validated by `is_valid_slot_name` in the
shared library rather than by each adapter. A name containing `/` or `..` is a
directory traversal on exactly one target and meaningless on the others, so it
is refused as a *name*. The filesystem adapter is not the only thing standing
between a slot name and the rest of the disk.

## What every implementation promises

The contract tests in `tests/storage/storage_contract_test.cpp` are one function
run against every implementation, because a promise only one adapter keeps is
not a contract:

- a slot that was written reads back byte for byte, including an empty one;
- writing a slot that exists replaces it entirely;
- reading or erasing a slot that is not there is `not_found`, not an empty
  success;
- an invalid name is refused by every operation, and `contains` answers no;
- `slots()` is ascending, and holds exactly what was written and not erased;
- a payload over `maximum_slot_bytes` is `too_large`, and one that would push
  the device past `maximum_total_bytes` or `maximum_slots` is `out_of_space`,
  with the slot left holding what it held before;
- overwriting a save with a smaller one never fails for space, because the
  budget is measured over what the device holds *after* the write.

`FilesystemSlotStorage` keeps the last of those the hard way: it writes
`<slot>.gls.tmp` and renames it over `<slot>.gls`. A save written directly into
its final file leaves a truncated save behind if the process dies mid-write, and
the player finds out the next time they load. The rename means the slot holds
the old save or the new one and never half of each.

## The browser, which keeps bytes on a later turn

A browser has durable storage and cannot answer within the call. IndexedDB
resolves on a subsequent turn of the event loop; `campaign::save_campaign`
returns on this one. So there is no browser adapter here, and there should not
be one: a device that waited for the browser would have to suspend the whole
WebAssembly module in the middle of a save.

The browser uses `MemorySlotStorage` instead, with the *caller* moving the
bytes. That is the target-without-a-device case this interface was written to
cover. `platform/web` exposes three entry points over its module-level device
(read a slot, replace one, forget one), and the editor mirrors a slot into
IndexedDB after each save the session makes and puts it back before a session
begins. The session is unchanged and unaware; it writes an envelope to a
`SlotStorage`, as it does on a desktop and on a cartridge.

The lesson for a future adapter is the general one: a host whose store cannot
complete inside the call does not get a new interface, it gets a mirror outside
the session. `platform/web/README.md` states it from the other side.

## The budget

`StorageBudget` is stated rather than discovered, because a console's answer is
a hardware constant and a desktop's is a policy, and a caller asking "will this
save fit" deserves the same kind of answer from both. `MemorySlotStorage` takes
its budget as a constructor argument for exactly this reason: give it thirty-two
kilobytes and the out-of-space paths of a Nintendo 64 SRAM cartridge are
exercised on a host, years before the cartridge adapter exists.

The number to hold it against is measured in `tests/campaign/save_test.cpp`. A
representative campaign (twelve roster members carrying two item stacks each,
eight shared-store stacks, eight objectives, eight typed world values,
twenty-two committed outcome ids, and one package requirement) encodes to
**2868 bytes**, 3128 standing in its graph. That fits an SRAM cartridge many
times over and does not fit a Nintendo 64 EEPROM of 512 or 2,048 bytes, a fact
to hold before picking a save device rather than after.
