# From the editor to a console

Four surfaces play or draw a Grandleon game. Each module's README describes its
own link; this document describes the chain they form.

## The chain

```
 project.json          the canonical authored source, in the editor or on disk
      │
      │  tools/game_content: the compiler
      │    validates, resolves names to identities, resolves choices to indices
      ▼
   game.gpk            the versioned container: sections, records, checksums
      │
      │  engine/package_format: the envelope
      │  engine/package_runtime: encounters, campaigns, dialogue, presentation
      ▼
 EncounterDefinition   plus the presentation the package chose
      │
      │  engine/simulation: the rules, and the only authoritative state
      ▼
 EncounterSnapshot     immutable, hashable, identical on every architecture
      │
      │  platform/view: the shared board presentation model
      │    camera, elevation projection, autotile convention, draw order
      ▼
   a renderer          SVG, SDL, RDP, or the PlayStation GPU
```

Two invariants hold the chain together: **only `engine/simulation` holds
authoritative state**, so nothing downstream re-derives a movement range, a
danger zone, a damage number or a refusal; and **`engine/core` and
`engine/simulation` may not depend on a platform SDK, a renderer, a game package
or a host tool**, which is what makes a cross-compile a build-system question
rather than a porting question. [../CODING.md](../CODING.md), "Architectural
invariants", states them with the rest of the set and what enforces each.

## How an author's choice reaches a console

A cell's terrain identity in a compiled package is a hash of its authored name,
and a hash cannot be turned back into `"river"`. So **the compiler resolves
choices to indices before the package is written**:

| The package carries | Resolved to |
|---|---|
| the project's theme | an index into the art library's theme menu |
| the project's character style | an index into the style menu |
| each faction's colour | an index into the colour menu |
| each unit type | an index into the archetype roster, and the colour it inherits |
| each terrain identity | an index into the terrain registry |

Each has an explicit "the package says nothing about this" value, and a package
carrying no presentation section resolves to the defaults. The menus are ordered
and **append-only**: the index is what every client agrees on, so the default
stays first and a new entry goes last.

- **Presentation cannot move a canonical hash.** The simulation never sees it,
  and a project restyled from medieval to sci-fi plays a bit-identical battle.
- **A console needs nothing baked beside its package.** `platform/playstation`
  names its encounter by authored path through `core::stable_content_id_v1` and
  takes its theme and joins out of the presentation section.
- **The editor is the exception, and unverified against the package.** It
  resolves art through its own generated TypeScript tables rather than through
  the presentation section, so it is the one surface answering this twice.

## The four targets

Not stages of one port; each answers a different question.

| | |
|---|---|
| **The browser** | where a game is made and first played. It runs the C++ simulation compiled to WebAssembly, so there is one set of rules rather than two agreeing sets. |
| **The desktop client** | the reference front end for the presenter seam, playing a real compiled package; the console front ends are built on the same `platform/client` session. |
| **The Nintendo 64** | the deepest console port (full campaign, title screen, cutscenes, controller, overlays, forecasts, abilities), and the only target that parses authored JSON *on the console* rather than consuming a package compiled on the host. |
| **The PlayStation** | the widest architectural spread from the host: 32-bit little-endian MIPS-I, no `clz`, no `movn`, no 64-bit load. It plays both shipped campaigns end to end from a controller over bit-banged SIO0 and keeps them on a memory card. |

The PlayStation cannot compile the content path at all, for want of a linkable
unwinder, so the Nintendo 64's is the repository's only target-side test of the
JSON parser.

## Back the other way: from the editor to a cartridge or a disc

An author's own game, in a file they can run, is the same chain again with the
real build on the end rather than a patched template ROM:

```
 the editor's project.json
      │  POST /api/<console>/build  (relative path; connect-src stays 'self')
      ▼
 tools/rom_service           refuses what it cannot serve before a container
      │                        starts, and compiles on the host first, because
      │                        one console compiles on the machine and the
      │                        other embeds what the host compiled — either way
      ▼                        a bad project would otherwise cost a build
 build-n64.sh --project <staged> --targets grandleon_n64_campaign
      ▼                        │
   a .z64                      │  build-playstation.sh --project <staged>
                               ▼  then build-disc.sh
                            a .bin and its .cue
```

Neither image belongs to a particular game: a generated `project_identity.h`
derives the campaign key and save slot from whichever project was built, from
one derivation both consoles share
(`cmake/GrandleonProjectIdentity.cmake`), and each reads the name to draw off
the package it is holding. `tools/rom_service/README.md` gives the argument
against patching, every refusal by name, and the lanes that hold the build
route up.

**The two consoles do not accept the same projects, and the difference is one
refusal.** The Nintendo 64 embeds the drawings its content actually draws, so a
project drawing characters in two styles is served. The PlayStation includes one
generated character header, so the same project is refused by name before a
container starts. That is an art-library limit rather than a console one, and
`cmake/GrandleonCharacterStyle.cmake` says what lifting it would take.

**A disc is offered on different terms from a ROM, and the surface says so.**
The image carries no licence sector, because that data is Sony's. PCSX-Redux
boots it and `grandleon_playstation_disc_check` proves that; a stock
PlayStation reads the licence area, finds nothing, and refuses the disc; and
nothing here has ever run on real hardware of either console, so no claim is
made about anything else that reads a disc.

**The browser compiles too.** `gl_content_compile` binds `tools/game_content`
through the web ABI: the same `parse_source_project_json` and `compile` the
command line runs, and the same two a Nintendo 64 cartridge runs on the console.
It is linked in, not reimplemented, and held to it:
`editor/src/domain/content-compiler.test.ts` compiles the shipped Tarnholt
project through the WebAssembly build and requires the package to match, in
byte count and in md5, what `grandleon_content_compile` writes natively.

## The verification story

Six layers, each catching something the one below it structurally cannot.

| | |
|---|---|
| **The rules, on the host** | `tests/simulation` and `tests/tactics` exercise the engine directly, including `forecasts_attacks_exactly`, which holds `forecast_attack` to whatever `apply` actually does. |
| **The same rules, elsewhere** | Every target that links the engine replays the shared reference vector defined in [engine/simulation/README.md](../engine/simulation/README.md) and must reproduce its two canonical hashes bit for bit. That covers a 64-bit little-endian host, a 64-bit big-endian console, a 32-bit little-endian console, and `platform/web` in a browser. The hash covers the board, what its ground allows and what it charges, every unit's state, the objectives and the random state, so a rule that behaves differently anywhere moves it. |
| **The picture, per pixel** | A console's frame is held to the colour of every visible cell with no tolerance: the PlayStation by making three independent quantities agree and reading the GPU's display registers out of a save state, the Nintendo 64 by a census the ROM runs on itself out of RDRAM. No frame digest is pinned: a per-cell claim says where a picture went wrong, which a digest cannot. Each pixel gate carries a **negative control**: the same harness pointed at a program that never touches the GPU is required to fail, every invocation. |
| **The turn, played** | A fixed controller script is played into the emulator's own pad ports and the program photographs itself at every checkpoint. What it must produce is derived on a different architecture, never on the console: host tools replay the same presses through the same `platform/client` session against the real engine, so a run cannot be made to pass by adjusting an expectation. |
| **The browser, in a browser** | `happy-dom` cannot enforce a Content Security Policy against WebAssembly compilation, so the Playwright suite builds the production bundle, serves it through `vite preview`, and drives a pinned Chromium against it. |
| **Drift gates** | Every committed artefact that could go stale has a check that fails when it does: the WebAssembly module against the pinned Emscripten container, the generated source-schema TypeScript, the generated art, and the linked PlayStation executable's ELF header against `mips1`. |

## Where to read further

| | |
|---|---|
| [platform/client/README.md](../platform/client/README.md) | the presenter seam every front end shares |
| [platform/view/README.md](../platform/view/README.md) | the board arithmetic every renderer shares |
| [platform/sheet/README.md](../platform/sheet/README.md) | the unit information sheet every client draws |
| [engine/simulation/README.md](../engine/simulation/README.md) | the rules, the queries, and the reference vector |
| [engine/package_format/README.md](../engine/package_format/README.md) | the container and the presentation section |
| [tools/game_content/README.md](../tools/game_content/README.md) | the compiler that resolves choices to indices |
| [platform/nintendo64/README.md](../platform/nintendo64/README.md) | the deepest console port, and ares |
| [platform/playstation/README.md](../platform/playstation/README.md) | the R3000A, the ISA trap, and three-channel pixel verification |
