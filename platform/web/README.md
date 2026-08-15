# Web platform

The browser target for the authoritative engine. This directory contains one
thing: the WebAssembly binding that lets the editor's Play mode run the real
C++ simulation instead of a JavaScript reimplementation of it.

This is deliberately not a full platform adapter. There is no rendering, input,
audio, timing, package acquisition, or storage here. The editor's existing Vue
application remains the shell; only the rules are crossed over.

## Contents

- [Toolchain](#toolchain)
- [Building](#building)
- [Binding surface](#binding-surface)
- [The campaign session](#the-campaign-session)
- [Conformance](#conformance)
- [Loading it](#loading-it)

## Toolchain

The build runs inside a pinned Emscripten SDK container so that the checked-in
module is byte-stable across machines and does not depend on whatever a
contributor has installed.

| | |
|---|---|
| Image | `emscripten/emsdk:4.0.23` |
| Digest | `sha256:86537645c51e44899812d29820ee3b64b96c321ebb2aba4416a04ceeb1bcde62` |

The digest is verified before every build. A moved tag fails the build rather
than silently producing different bytes. Both are set in
`cmake/GrandleonWebAssembly.cmake` and can be overridden with
`-DGRANDLEON_EMSDK_IMAGE=` and `-DGRANDLEON_EMSDK_DIGEST=`.

## Building

Emscripten is never part of the default host build. The targets are opt-in and
excluded from `ALL`:

```sh
# Rebuild the module and update the editor's checked-in copy.
cmake --build build --target grandleon_wasm

# Fail if the checked-in copy is not what the pinned toolchain produces.
cmake --build build --target grandleon_wasm_check
```

Both shell out to `platform/web/scripts/build-wasm.sh`, which can also be run
directly from the repository root. It configures the ordinary top-level
`CMakeLists.txt` under `emcmake`, so the WebAssembly build compiles the same
library targets as the native build rather than a parallel definition of them.
Under Emscripten the root project stops after the engine libraries, the content
compiler and this directory: the command-line tools and the installed SDK are
host concerns.

The compiled artifact is packed into `editor/src/generated/simulation-module.ts`
by `editor/scripts/generate-wasm-module.mjs`. That generated module is the only
checked-in copy; `build-wasm/` is ignored.

### C++ subset

The engine already avoided the constructs a browser target makes expensive, and
**the WebAssembly build required no engine source changes at all**. The module
compiles with `-fno-exceptions -fno-rtti` under `-Wall -Wextra -Wpedantic
-Wconversion -Werror` and links with `-Oz`, `-sSTANDALONE_WASM`, and
`-sFILESYSTEM=0`.

The result imports nothing:

```
IMPORTS: []
```

There is no host surface through which browser state, a clock, or a random
source could reach the simulation. The only exports are `memory`, the WASI
reactor `_initialize`, and the `gl_*` entry points below.

## Binding surface

The narrowest surface that drives an encounter (create one from a definition,
apply a command, read a snapshot, read the canonical hash), plus the campaign
flow around it: `package_runtime`'s campaign loader, cursor, and dialogue
decoder, fed the same compiled record encodings the console reads, so branch
conditions, scenes, and endings are never re-derived in the browser. The package
container format is not bound; the content compiler is, and the section
below says what it costs.

| Export | Meaning |
|---|---|
| `gl_sim_io_buffer() -> u32` | Address of the shared scratch buffer |
| `gl_sim_io_capacity() -> u32` | Size of that buffer in bytes |
| `gl_sim_create(payload_size: u32) -> u32` | Create from a definition; returns a handle or `0` |
| `gl_sim_destroy(handle: u32)` | Release a handle |
| `gl_sim_apply(handle: u32, payload_size: u32) -> u32` | Apply a command; returns bytes written |
| `gl_sim_snapshot(handle: u32) -> u32` | Write a snapshot; returns bytes written |
| `gl_sim_canonical_hash(handle: u32) -> u64` | Canonical 64-bit state hash |
| `gl_sim_create_error_name(error: u32) -> u32` | Engine's own name for a `CreateError` |
| `gl_sim_command_error_name(error: u32) -> u32` | Engine's own name for a `CommandError` |
| `gl_core_stable_content_id(length: u32) -> u64` | Source key in the buffer to its stable content identity |
| `gl_ai_decide(handle: u32, payload_size: u32) -> u32` | Propose a command for an unattended unit |
| `gl_sim_forecast_attack(handle: u32, attacker: u64, target: u64, weapon: u64) -> u32` | Price one attack without committing it, with `0` meaning the weapon in hand; returns bytes written |
| `gl_sim_forecast_item(handle: u32, unit: u64, target: u64, item: u64) -> u32` | Price spending one carried item without committing it, with `0` for the target meaning the acting character; returns bytes written |
| `gl_sim_forecast_talk(handle: u32, unit: u64, target: u64) -> u32` | Ask whether one character may be talked off the board, without committing it; returns bytes written |
| `gl_sim_reachable_tiles(handle: u32, unit: u64) -> u32` | Tiles the unit could move to; returns bytes written |
| `gl_sim_danger_tiles(handle: u32, side: u32) -> u32` | Tiles a side could reach and strike; returns bytes written |
| `gl_sim_deployable_tiles(handle: u32, unit: u64) -> u32` | Tiles the unit may be arranged on during the deployment phase, empty once it closes; returns bytes written |
| `gl_sim_aimable_tiles(handle: u32, unit: u64, kind: u32, weapon: u64, ability: u64) -> u32` | Tiles the unit could aim one gesture at, with `kind` the `Gesture` value; returns bytes written |
| `gl_sim_gesture_available(handle: u32, unit: u64, kind: u32, weapon: u64, ability: u64) -> u32` | Whether the unit could make that gesture at all, whatever it were aimed at; returns bytes written |
| `gl_sim_area_tiles(handle: u32, ability: u64, x: i32, y: i32) -> u32` | Tiles an area cast aimed at one tile would cover; returns bytes written |
| `gl_campaign_create(payload_size: u32) -> u32` | Load a compiled campaign record and create a cursor; returns a handle or `0` |
| `gl_campaign_destroy(handle: u32)` | Release a campaign handle |
| `gl_campaign_add_dialogue(handle: u32, payload_size: u32) -> u32` | Attach one compiled dialogue record to a campaign |
| `gl_campaign_state(handle: u32) -> u32` | Write the cursor's current node; returns bytes written |
| `gl_campaign_advance(handle: u32, payload_size: u32) -> u32` | Advance past an encounter node from its outcome and objective results |
| `gl_campaign_advance_story(handle: u32) -> u32` | Advance past a story node |
| `gl_campaign_dialogue(handle: u32, dialogue: u64) -> u32` | Decode an attached dialogue record; returns bytes written |
| `gl_campaign_error_name(error: u32) -> u32` | Engine's own name for a `CampaignError` |
| `gl_campaign_dialogue_error_name(error: u32) -> u32` | Engine's own name for a `DialogueError` |
| `gl_campaign_session_create(payload_size: u32) -> u32` | Create a persistent campaign session over an authored flow; returns a handle or `0` |
| `gl_campaign_session_destroy(handle: u32)` | Release a session handle and any battle it holds |
| `gl_campaign_session_add_unit_type(handle: u32, payload_size: u32) -> u32` | Attach one compiled unit type record, for its growth block |
| `gl_campaign_session_add_board(handle: u32, payload_size: u32) -> u32` | Attach one encounter's board, the authored placement identity of each unit on it, and how many of a company its author lets take the field |
| `gl_campaign_session_begin(handle: u32, payload_size: u32) -> u32` | Found the roster, enter the graph, and optionally resume a named slot |
| `gl_campaign_session_state(handle: u32) -> u32` | Where the campaign stands, and the roster it holds |
| `gl_campaign_session_advance_story(handle: u32) -> u32` | Complete a story node through the graph |
| `gl_campaign_session_board(handle: u32) -> u32` | Take the standing node's board through the roster and start the battle; returns an encounter handle or `0` |
| `gl_campaign_session_commit(handle: u32) -> u32` | Commit what the battle did and write the campaign to its slot |
| `gl_campaign_session_company(handle: u32) -> u32` | The company between battles, which members the next board has a place for, which of them would take its field, and how many it allows |
| `gl_campaign_session_manage(handle: u32, payload_size: u32) -> u32` | One management gesture: give, take, field or bench, committed and saved |
| `gl_storage_read(payload_size: u32) -> u32` | Read one slot's bytes off the module's slot device; returns bytes written |
| `gl_storage_write(payload_size: u32) -> u32` | Replace one slot's bytes on that device |
| `gl_storage_erase(payload_size: u32) -> u32` | Forget one slot on that device |
| `gl_campaign_session_error_name(error: u32) -> u32` | Client's own name for a `CampaignSessionError` |
| `gl_campaign_outcome_error_name(error: u32) -> u32` | Campaign's own name for an `OutcomeError` |
| `gl_campaign_roster_error_name(error: u32) -> u32` | Engine's own name for a `RosterError` |
| `gl_campaign_save_error_name(error: u32) -> u32` | Save format's own name for a `SaveError` |
| `gl_campaign_migration_error_name(error: u32) -> u32` | Migration registry's own name for a `MigrationError` |
| `gl_storage_error_name(error: u32) -> u32` | Device's own name for a `StorageError` |
| `gl_campaign_operation_name(kind: u32) -> u32` | Campaign's own name for one outcome operation kind, numbered from one |
| `gl_content_buffer() -> u32` | Address of the content compiler's own buffer |
| `gl_content_capacity() -> u32` | Size of that buffer in bytes |
| `gl_content_compile(source_size: u32) -> u32` | Compile an authored project's canonical source to a package; returns bytes written |

Two exports are not about running an encounter. `gl_ai_decide` proposes a
command for a unit nobody is steering, using `engine/tactics`; it is policy, and
the simulation still validates whatever it returns. The proposal may be an
ability command: the boundary hands the policy the encounter's own ability
definitions, so an unattended caster casts here exactly as it does natively.
`gl_core_stable_content_id`
is here because content identities are part of canonical state:
the editor has to derive the same identifiers the content compiler assigns, and
a second implementation of that mapping would silently change the canonical hash
without changing anything a reader would recognise as a rule.

Arguments and results are passed through one static little-endian scratch buffer
in linear memory. Nothing is passed by struct pointer, so the JavaScript side
never depends on the C++ ABI's padding, alignment, or field order. The wire
format below is the whole contract.

Two consequences worth stating explicitly:

- **Handles are validated indices, not pointers.** A caller cannot hand the
  module an arbitrary integer and have it dereferenced.
- **Every read is bounds-checked.** A truncated or malformed payload latches an
  overflow flag and produces a boundary status, never an out-of-bounds access.
  A declared unit count larger than the buffer could hold is rejected before
  anything is reserved.

The error vocabularies are read back out of the module at load time rather than
restated in TypeScript, so a new enumerator cannot silently come to mean
something else in the user interface.

### The content compiler, and what it cost

`gl_content_compile` is `tools/game_content`: `parse_source_project_json` then
`compile`, the same two translation units the command line runs and a Nintendo
64 cartridge runs on the console. It is linked in rather than reimplemented: a
package compiled twice is a package that can be compiled differently, and a
ROM built from the browser's copy would then not be the program this
repository checks. `editor/src/domain/content-compiler.test.ts` holds the
shipped Tarnholt project to a byte-for-byte match, by size and md5, with what
`grandleon_content_compile` writes natively.

The compiler is a large part of the module's size, and every editor load pays
for it, because the module is embedded in the bundle. The figure is
`simulationModuleByteLength` in `editor/src/generated/simulation-module.ts`,
which `grandleon_wasm_check` holds the pinned toolchain to. A second module
loaded on demand would be cheaper per load at the price of a second build
target, staleness check and instantiation path; the trade is not taken.

The compiler gets **its own buffer**, the one departure from "one static
scratch buffer". The scratch buffer carries payloads the wire format bounds
(one encounter, one command, one record), and the slot device publishes its
budget as `gl_sim_io_capacity()` less a header, so widening it would quietly
relax a refusal that has nothing to do with compiling. What travels here is a
whole authored project, larger than the 64 KiB scratch buffer.
`gl_content_capacity()` is 1,048,576 bytes and bounds the source going in and
the reply coming back. Both buffers are uninitialised statics, so neither
costs the module a file byte.

The source is written into the content buffer as UTF-8 and the reply is written
back over it:

| Field | Type |
|---|---|
| status | `u8` (0 compiled, 1 source rejected, 2 content rejected, 3 source too large) |
| diagnostic count | `u16` |
| per diagnostic: code name, path, detail | 3 × (`u16` length, bytes) |

and then, **only when the status is `0`**:

| Field | Type |
|---|---|
| character style | `u8` (the art library's menu index) |
| encounter count, then that many identities | `u16`, `u64`… |
| campaign count, then that many identities | `u16`, `u64`… |
| package size, then the package | `u32`, bytes… |

A diagnostic carries the compiler's own *name* for its code rather than a
number. The two stages have two vocabularies, `SourceDiagnosticCode` and
`DiagnosticCode`, and a caller that restated either could disagree with the
compiler about what it had just refused. `detail` is what the source parser
adds and is empty for a semantic diagnostic.

The identities are in the reply for the same reason `gl_core_stable_content_id`
is bound at all: a cartridge names its opening board and its campaign by stable
identity, and a second derivation of one is a second chance to derive it
differently.

### The slot device, and what it deliberately does not do

Every campaign session saves into one module-level
`storage::MemorySlotStorage`. It is a tab's memory, outliving a session and not
a page. The three `gl_storage_*` entry points let a caller with somewhere
durable mirror a slot: read it after a save, put it back before the next
`gl_campaign_session_begin`, carrying the `GLSV` envelope verbatim and opaque.

The mirroring is deliberately outside the module rather than a browser-backed
device inside it. A browser's store answers on a later event-loop turn and
`campaign::save_campaign` returns on this one, so a device that called out to it
would have to suspend the module in the middle of a save. Keeping it out here
also keeps `client::CampaignSession` ignorant of the browser: it writes to a
device, as it does on a desktop and will on a console, and one campaign loop
serves all three.

The device's budget is set from this buffer, `gl_sim_io_capacity()` less the
six-byte read header. A campaign too large to mirror is refused as `too_large`
when it is *written*, rather than saved into a slot nothing outside the module
could read back.

### Wire format

All integers are little-endian. `u64` values cross the boundary as BigInt
(`-sWASM_BIGINT`); note that an `i64` *result* arrives signed, so the canonical
hash is reinterpreted with `BigInt.asUintN(64, …)` on the JavaScript side.

Encounter definition, written before `gl_sim_create` (8-byte header, then one
variable-length record per unit, then the ability and objective registries and
the turn order):

| Field | Type |
|---|---|
| width, height | `u16`, `u16` |
| unit count | `u32` |
| per unit: id, unit type id | `u64`, `u64` |
| per unit: side | `u8` (0 first, 1 second) |
| per unit: x, y, health, strength, power, defense, resistance | 7 × `i16` |
| per unit: skill, luck, evasion, magic | 4 × `i16` |
| per unit: movement, action points, speed | 3 × `u8` |
| per unit: acts after attacking | `u8` |
| per unit: minimum reach, maximum reach | `u8`, `u8` |
| per unit: ability count, then that many ability ids | `u32`, `u64`… |
| per unit: carried weapon count, then that many weapon ids | `u32`, `u64`… |
| per unit: crossings | `u8` (bit 0 water, bit 1 heights, bit 7 flight) |
| per unit: accuracy | `u8` (whole percent; 100 always lands) |
| per unit: carried item count, then that many id-and-count pairs | `u32`, (`u64`, `u16`)… |
| ability count | `u32` |
| per ability: id | `u64` |
| per ability: kind, damage type, area shape | 3 × `u8` |
| per ability: power | `i16` |
| per ability: minimum reach, maximum reach, radius | 3 × `u8` |
| per ability: accuracy | `u8` (whole percent; 100 always lands) |
| weapon count | `u32` |
| per weapon: id | `u64` |
| per weapon: power | `i16` |
| per weapon: minimum reach, maximum reach | `u8`, `u8` |
| per weapon: accuracy | `u8` (whole percent; 100 always lands) |
| item count | `u32` |
| per item: id | `u64` |
| per item: kind | `u8` (0 no effect, 1 restores health) |
| per item: power | `i16` |
| objective count | `u32` |
| per objective: id | `u64` |
| per objective: kind, side | `u8`, `u8` |
| per objective: target unit id | `u64` |
| turn order | `u8` (0 alternating, 1 side blocks, 2 initiative) |
| terrain count, then that many cells | `u32`, `u8`… (0 open, 1 water, 2 heights) |
| movement cost count, then that many cells | `u32`, `u8`… (steps charged to enter, at least 1) |
| deployment tile count, then that many tiles | `u32`, (`i16`, `i16`)… |

A terrain count of zero is an all-open board, which is what a caller with no
terrain to give means and what content authored before terrain had a meaning
plays as. Any other count must equal width × height or the engine refuses the
encounter as an invalid map.

A movement cost count of zero is a board where every step costs one, which is
what a caller with no price to give means and what content authored before
ground had a price plays as. Any other count must equal width × height, and
every cell must charge at least one; a cell charging nothing is an invalid map
rather than a free one.

A deployment tile count of zero is an encounter with no deployment phase, which
is what every board that authors no region means. It is written rather than
left off, unlike the tails elsewhere in this format: a campaign session appends
its own placement table after this record, so an absent tail and a present one
would be indistinguishable there.

Command, written before `gl_sim_apply`:

| Field | Type |
|---|---|
| type | `u8` (0 move, 1 attack, 2 wait, 3 ability, 4 use item, 5 deploy, 6 begin battle) |
| unit id | `u64` |
| destination x, y | `i16`, `i16` |
| target id | `u64` |
| ability id | `u64` |
| weapon id | `u64` (0 means the weapon in hand) |
| item id | `u64` (no in-hand default: a use naming nothing is refused) |

An unrecognised discriminator is sent through as an out-of-range byte rather
than rejected at the boundary. The engine checks the acting unit, its health,
and its side *before* it looks at the command type, and that precedence is a
rule the binding must not pre-empt.

Command result, written by `gl_sim_apply`:

| Field | Type |
|---|---|
| boundary status | `u8` |
| command error | `u8` |
| event count | `u32` |
| per event: type | `u8` |
| per event: unit id, related unit id | `u64`, `u64` |
| per event: x, y, amount | 3 × `i16` |
| per event: outcome | `u8` |
| per event: content id | `u64` (0 for every event that names no definition) |

Attack forecast, written by `gl_sim_forecast_attack`. The command error is
the refusal `gl_sim_apply` would return for the same attack; when zero, `hit
chance` is the very percentage `gl_sim_apply` rolls against and the numbers
above it are what a landed roll delivers. A miss takes zero. Forecasting
never changes state and never draws:

| Field | Type |
|---|---|
| boundary status | `u8` |
| command error | `u8` |
| damage | `i16` |
| target health after | `i16` |
| lethal | `u8` |
| counter | `u8` |
| counter damage | `i16` |
| attacker health after | `i16` |
| counter lethal | `u8` |
| hit chance | `u8` (whole percent) |
| counter chance | `u8` (whole percent) |

Item forecast, written by `gl_sim_forecast_item`. Spending an item draws no
random number, so there is no chance byte and `restored` is exactly what
`gl_sim_apply` will deliver, already clamped to the health the character is
missing. A character at full health is forecast as restoring nothing, and
spending the item anyway spends it:

| Field | Type |
|---|---|
| boundary status | `u8` |
| command error | `u8` |
| kind | `u8` (0 no effect, 1 restores health) |
| restored | `i16` |
| target health after | `i16` |
| remaining after | `u16` |

Tile list, written by `gl_sim_reachable_tiles`, `gl_sim_danger_tiles`,
`gl_sim_aimable_tiles`, `gl_sim_area_tiles` and `gl_sim_deployable_tiles`.
All five are read-only queries
over the current snapshot, so none changes state or the canonical hash, and all
answer in the engine's row-major order. The first returns every tile the unit
could occupy after one accepted move command. That is the same traversal
`gl_sim_apply` judges a move against, so a client that lights these tiles can
never disagree with what the engine will accept. The second returns the enemy
danger zone: every tile a side's living units could reach and strike with any
weapon they carry or any damaging ability they know, honouring minimum reach.
Its `side` argument is `0` for the first side and any other value for the
second, the encoding `gl_sim_create` reads.

The third answers the same question about aiming that the first answers about
walking: a tile is in the result exactly when the command committing that
gesture there would be accepted. Its `kind` is the `Gesture` value (`0` walk,
`1` strike, `2` cast, `3` talk). `weapon` is read only by a strike, with `0`
meaning the weapon in hand, and `ability` only by a cast; any other `kind` is a
malformed payload rather than an empty answer. An empty list is not a refusal:
a strike with nobody in reach lights nothing and is still a gesture the
character may make, which is `gl_sim_gesture_available`'s question.

The fourth returns the splash of an area cast aimed at one tile, clipped to the
board and including that tile, using the same membership test `gl_sim_apply`
walks the units against. It is the *cover* and not the casualty list: a
damaging cast covers a tile the caster's own side is standing on and takes
nothing off whoever is standing there. It is empty for an ability no registry
resolves and for a single-tile one, whose splash is the tile aimed at. It asks
nothing about whether the cast may be aimed there; that is
`gl_sim_aimable_tiles`'s question, and a client drawing a splash outside the
band would be drawing a cast the engine will refuse. A centre outside the range
a board coordinate is carried in is a malformed payload:

| Field | Type |
|---|---|
| boundary status | `u8` |
| tile count | `u32` |
| per tile: x, y | `i16`, `i16` |

Gesture availability, written by `gl_sim_gesture_available`. Read-only like the
tile queries, and asked with the same three aim arguments. It draws the line
between the gesture and its aim: `0` means every command carrying this gesture
is refused before the engine looks at what it named (a character who has
already walked this turn, one whose turn is over, one on the side that is not
acting, a weapon it is not carrying, an ability it does not know). `1` means
the gesture itself is accepted and only the aim is left to judge. A menu
offers its row on this byte and lights its tiles from `gl_sim_aimable_tiles`,
which are two different facts a player is told two different ways:

| Field | Type |
|---|---|
| boundary status | `u8` |
| available | `u8` (0 no, 1 yes) |

Snapshot, written by `gl_sim_snapshot`. The last byte of each unit, **on the
board**, is `simulation::on_board` (the engine's own answer to "is this
character standing there") sent rather than composed. Health, departure and
arrival are all on the wire above it, so a browser could spell the predicate
itself, which is exactly why it is not asked to: a restated copy would be a
rule free to drift, and the drift it prevents is a character drawn on a tile
the engine refuses every command aimed at:

| Field | Type |
|---|---|
| boundary status | `u8` |
| width, height | `u16`, `u16` |
| active side, outcome | `u8`, `u8` |
| active unit id | `u64` |
| remaining action points | `u8` |
| round | `u32` |
| activation count | `u64` |
| unit count | `u32` |
| per unit: id, unit type id | `u64`, `u64` |
| per unit: side | `u8` |
| per unit: x, y, health, maximum health, strength, power, defense, resistance | 8 × `i16` |
| per unit: skill, luck, evasion, magic | 4 × `i16` |
| per unit: movement, action points, speed | 3 × `u8` |
| per unit: acts after attacking, has acted | `u8`, `u8` |
| per unit: minimum reach, maximum reach | `u8`, `u8` |
| per unit: ability count, then that many ability ids | `u32`, `u64`… |
| per unit: carried weapon count, then that many weapon ids | `u32`, `u64`… |
| per unit: carried item count, then that many id-and-remaining pairs | `u32`, (`u64`, `u16`)… |
| per unit: what it would leave behind, and how often | `u64`, `u8` |
| per unit: reach bonus | `u8` (already inside maximum reach above) |
| per unit: what talking to it records, whether it has been talked off | `u64`, `u8` |
| per unit: arrival round, whether it has arrived | `u32`, `u8` |
| per unit: has moved, spent action points | `u8`, `u8` |
| per unit: on the board | `u8` |
| objective count | `u32` |
| per objective: id, state | `u64`, `u8` |
| drop count, then what fell, whose body, and who claims it | `u32`, (`u64`, `u64`, `u64`)… |
| deployment phase open | `u8` |
| deployment tile count, then that many tiles | `u32`, (`i16`, `i16`)… |

Campaign creation, written before `gl_campaign_create`. The campaign record
payload is byte-for-byte the encoding `tools/game_content` compiles into a
package's campaigns section; it is handed unopened to
`package_runtime::load_campaign`, so every structural rule (combinators, the
single unconditional transition, reference existence) is enforced by the
same code the console runs. The encounter identities exist only to satisfy
the loader's reference check; battles are still created via `gl_sim_create`:

| Field | Type |
|---|---|
| campaign id | `u64` |
| record size, then the compiled campaign record | `u32`, bytes… |
| encounter count, then that many encounter ids | `u16`, `u64`… |

On failure the buffer holds a boundary status byte and a `CampaignError`
byte, so a rule-level refusal is the engine's own. Dialogue records attach
one at a time before `gl_campaign_add_dialogue` (`u64` dialogue id, `u32`
record size, then the compiled dialogue record), and are decoded on demand by
`package_runtime::load_dialogue` via `gl_campaign_dialogue`, which writes a
status byte, a `DialogueError` byte, and on success the name and lines as
`u16`-length-prefixed strings. `gl_campaign_state` writes status, a
completion flag, the node id (`u64`), its kind (`u8`: 1 encounter, 2
terminal, 3 story), its encounter id (`u64`), and its dialogue ids in
authored order (`u16` count, then `u64` each). `gl_campaign_advance` reads
the encounter outcome (`u8`) and the objective results exactly as
`gl_sim_snapshot` reported them (`u32` count, then `u64` id and `u8` state
per objective), so branch predicates are evaluated by the engine over the
engine's own values.

The boundary status is `0` for success, and otherwise reports a malformed
payload, an unknown handle, or a buffer overflow. It is kept distinct from
`CreateError`, `CommandError`, `CampaignError`, and `DialogueError` so that a
malformed payload can never be mistaken for a game rule outcome.

## The campaign session

The thirteen `gl_campaign_session_*` exports are one object:
`client::CampaignSession` in `platform/client`, the same found-or-resume,
exclude, play, derive, commit, save and walk the terminal client runs. They
are separate calls because the browser cannot block: the loop is outside and
the steps are here.

The one thing this caller cannot supply is a package, because the editor plays
content that was never compiled. Two things therefore travel over the wire that
a native client reads out of a mounted package:

- **Boards.** `gl_campaign_session_add_board` sends one encounter's
  definition in exactly the layout `gl_sim_create` reads, then a `u32`
  placement count and one `u64` source key per unit in definition order, then
  a `u16` deployment capacity. The source key is the authored placement's own
  identity, which is what a roster joins through;
  `campaign_runtime::join_campaign_roster` is what runs. The capacity travels
  beside the definition because the simulation never learns it; zero caps
  nothing, and it is written on every board: a caller with nothing to say
  and a truncated payload must not be the same bytes.
- **Growth blocks.** `gl_campaign_session_add_unit_type` sends one compiled
  unit type record exactly as `tools/game_content` encodes it. Only the
  tail is read (experience award, per-level threshold, ten growth chances,
  the drop), but the name and identity lists must still be written empty,
  because the compiler's own decoder reads them.

The session's storage device is one module-level `storage::MemorySlotStorage`
shared by every session, so a save outlives the session that wrote it; it is
a tab's memory and does not survive a reload.

**The company, between battles.** `gl_campaign_session_company` answers what
the company is, which members the next board places, which would take its
field, and how many its author allows out; `gl_campaign_session_manage` makes
one gesture (give, take, field, bench), each one
`campaign::CampaignOutcomeBatch` committed and written to the slot before the
call returns, so there is no pending state to reconcile. The reply carries
the committed operations and the roster and store as left. A refusal is an
`OutcomeError` read back by name. One refusal is deliberately made by the
screen instead: a `field` past the board's capacity, refused out of the
`fielded` and `capacity` this export publishes, under
`gl_campaign_roster_error_name(RosterError::over_deployment_capacity)`. That is
an early copy of the engine's gate, which `gl_campaign_session_board` still
enforces however the screen counted. The engine never benches anybody; the
player answers a cap.

A battle a session starts is an ordinary encounter handle driven by the
`gl_sim_*` exports; the one difference is that the module keeps its events,
because `derive_battle_progression` reads them. Every other encounter
records nothing.

## Conformance

`editor/src/domain/encounter-simulation.test.ts` runs the shared reference
vector through this module and asserts the initial and completed canonical
hashes recorded natively in `tests/simulation/encounter_test.cpp`. The vector
and its two numbers are defined in
[engine/simulation/README.md](../../engine/simulation/README.md).

Those two values are the contract between the native, browser and console
builds. A change to the simulation is incomplete until all of them agree.

## Loading it

`editor/src/domain/encounter-simulation.ts` instantiates the module and exposes
the encounter API. The module is embedded as base64 in a generated TypeScript
file rather than fetched as a separate asset, so it loads identically in the
browser, under Vitest in Node, from a non-root deployment sub-path, and offline.
There is no asset URL to resolve and no MIME configuration to get wrong.

Instantiation is asynchronous, so `initEncounterEngine()` must resolve before an
encounter can be created. The editor calls it at startup and gates Play on
`isEncounterEngineReady()`.
