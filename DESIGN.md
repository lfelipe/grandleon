# Grandleon: architecture

The shape of the engine and the reasons for the non-obvious choices.
[README.md](README.md) covers what a game is made of. This document and the
module READMEs are the architecture's own statement of itself, and each
module's README holds that module's contract. Where either disagrees with a
test, the test is right, because the test is what the machine reads.
[CODING.md](CODING.md), "Architectural invariants", states the rules a change
has to keep, and
[docs/FROM_EDITOR_TO_CONSOLE.md](docs/FROM_EDITOR_TO_CONSOLE.md) shows how the
layers fit together.

Section numbers are stable so that code comments can cite them. Renumbering one
means finding every citation of it.

- [1. Vision](#1-vision)
- [2. Design philosophy](#2-design-philosophy)
- [3. High-level architecture](#3-high-level-architecture)
- [3.1 Dependency rules](#31-dependency-rules)
- [3.2 Determinism contract](#32-determinism-contract)
- [3.3 Runtime and tooling profiles](#33-runtime-and-tooling-profiles)
- [3.4 Console constraints](#34-console-constraints)
- [4. Rendering and maps](#4-rendering-and-maps)
- [5. Battle-local and persistent state](#5-battle-local-and-persistent-state)
- [6. Packages and content](#6-packages-and-content)
- [7. Repository structure](#7-repository-structure)
- [8. Serialization](#8-serialization)
- [9. A networkable path](#9-a-networkable-path)
- [10. Non-goals](#10-non-goals)

---

# 1. Vision

Grandleon is a portable tactical RPG engine that separates gameplay mechanics
from game content. The engine contains no story, characters, dialogue, maps or
campaigns; it loads game packages that define all of it. Anyone should be able
to build a complete tactical RPG without recompiling the engine.

The engine runs on desktop (SDL and terminal front ends), in web browsers
through WebAssembly, on the Nintendo 64 and on the PlayStation. The same
package plays on all four. The cost of the next console should land in the
platform layer, not in the rules.

---

# 2. Design philosophy

Gameplay is authoritative; rendering visualizes it and the simulation never
knows how it is displayed. Everything that can be data is data: units,
classes, items, maps, abilities, dialogue, campaigns, opposing-side behaviour,
cutscenes, battle formulas. Gameplay code never depends on a graphics API,
operating system or console. The engine is deterministic: identical results on
every platform, which buys replay, small saves, easier debugging, a path to
multiplayer, and checkable console ports. An expectation derived on the host is
the expectation the console must meet.

---

# 3. High-level architecture

```text
                 Game Package
                      │
                      ▼
               Gameplay Engine
                      │
        ┌─────────────┴─────────────┐
        ▼                           ▼
   A front end                 Campaign state
        │
        ▼
 Platform renderer
        │
 SDL / terminal / WebAssembly / libdragon / Nugget
```

The engine owns the battle state. A front end issues commands, which the
engine may reject, and observes the result; it never mutates state directly.
A renderer draws what the front end read.

## 3.1 Dependency rules

Dependencies point inward; inner layers never include the headers of outer
ones. [CODING.md](CODING.md), "Architectural invariants", states what this
section costs a change, and what enforces it.

```text
core → simulation → tactics → package_runtime → client → a front end
  ↓                                  ↑
campaign                     package_format

view    board arithmetic with no engine dependency, below the presenter seam
```

* **`core`**: engine version, content identity, the deterministic random
  substrate.
* **`simulation`**: authoritative battle state and command validation. Depends
  only on `core`.
* **`tactics`**: proposes commands for units nobody is steering. Policy, not
  rules; replaceable without changing what the engine permits.
* **`campaign`**: persistent state that outlives a battle. A sibling of
  `simulation`, not a layer above it, and it links only `core`. The simulation
  must not know it exists: the day a rule reads a roster is the day a
  canonical hash depends on a save file.
* **`package_format`**, **`package_runtime`**: decode validated data. No
  gameplay rules.
* **`platform/client`**: the campaign walk, the battle loop, and the presenter
  seam every front end implements.
* **`platform/view`**: header-only board arithmetic (projection, elevation
  offset, autotile convention, draw order) with no engine dependency, so
  renderers agree on where a cell is without agreeing on anything else.

Two rules the diagram cannot show: a rule the simulation answers is never
re-derived by a client, and elevation is drawn but never simulated. No rule
reads elevation, no snapshot carries it, no canonical hash observes it.

There is no `presentation` module. A client receives an `EncounterSnapshot`
plus the map's terrain identities and interprets them itself. Events are facts
emitted after accepted state changes; commands are requests that may be
rejected.

## 3.2 Determinism contract

The authoritative simulation:

* advances only when given an explicit command or simulation step;
* uses fixed-width integers with explicitly defined overflow and rounding;
* does not use floating-point values for authoritative state or calculations;
* uses a project-owned pseudo-random number generator whose complete state is
  saved;
* iterates gameplay collections in stable, specified order;
* assigns stable numeric identifiers rather than using addresses or load
  order;
* serializes integers with an explicit byte order and versions every persisted
  schema;
* never reads the clock, filesystem, input devices, locale, or platform RNG
  directly.

A canonical state hash is a testing and replay diagnostic, calculated in the
same canonical field order as serialization, excluding presentation and cache
data. Commands apply atomically; a rejected command emits a diagnostic result
but no gameplay event, and leaves canonical state unchanged.

Two rules draw from the random substrate: a strike can miss (an attack, its
counter, or a damaging cast) and a defeated character can drop something. A
miss rolls against exactly the chance the forecast showed. Growth rolls are
drawn outside any battle, from a state seeded off the completed battle's
canonical hash, because a level is the campaign's business and the simulation
must not learn that campaigns exist.

## 3.3 Runtime and tooling profiles

There are four:

* **Host.** C++17 desktop runtime, asset tools, tests, sanitizers, editor.
* **WebAssembly.** The same engine libraries compiled by a pinned Emscripten
  SDK; this is what the editor's playtest and Play mode run. There is no
  second implementation of the simulation; the browser reproduces the native
  canonical hashes.
* **Nintendo 64.** libdragon with its MIPS GCC toolchain.
* **PlayStation.** The same engine libraries cross-compiled with
  `mipsel-linux-gnu-g++` into a freestanding executable, with no line of
  `engine/` changed.

No constrained C profile exists; the portable engine reaches a 1994 console
without a target-specific rewrite. The wall is memory, not language. Where a
target cannot hold the engine, the answer is a smaller declared content budget
or no port, not a second set of rules.

The portable engine API must not expose SDL, libdragon, native file handles,
host pointers, or standard-library types whose binary layout must cross a
package or save boundary. "Portable" does not mean every file compiles under
every toolchain; it means identical specified behaviour and compatible content
contracts, verified by the same conformance fixtures.

## 3.4 Console constraints

Console runtimes may not approximate rules silently. A feature is either
supported and passes the canonical fixture, or the target profile rejects the
package before play with an unsupported-feature diagnostic.

Measurements behind the two shipped console targets:

* **C++17 on the Nintendo 64 is confirmed, not assumed.** libdragon itself
  compiles and links exception and RTTI support into every C++ ROM. Only their
  code-size cost argues for restricting them, so the ROM translation unit adds
  `-fno-exceptions -fno-rtti` while the engine libraries do not. On the
  PlayStation both are off toolchain-wide: that toolchain's exception-handling
  archives cannot be linked into a freestanding executable at all.
* **The portable core is close to console-safe by construction.** `core`,
  `simulation`, `tactics`, `package_format` and `package_runtime` contain no
  exceptions, RTTI, iostreams, floating point or type punning, and all byte
  I/O is shift-based and therefore endian-independent.
* **Standard-library containers do cross module contracts.**
  `EncounterSnapshot`, `CommandResult`, `reachable_tiles`, `danger_tiles` and
  `LoadedPackage` all use `std::vector`. What must not cross a package or save
  boundary is their binary layout, and it does not.
* **Three package-format costs are prices, not blockers.** Section alignment
  is 4; `LoadedPackage` copies the whole package buffer, doubling peak memory
  at load; and duplicate checks build a `std::set`, allocating per element.
  Both consoles ship with all three in place and pass their gates. Alignment 4
  is sufficient: an argument for 8 is a Nintendo 64 DMA argument, not a
  portability one.
* **The stable libdragon branch and its `rdpq` 2D API is the target.** The
  board's projection is axis-aligned rather than isometric. The generated tiles
  are top-down squares, and no runtime transform makes a top-down tree read as
  an isometric one ([platform/view/README.md](platform/view/README.md)).
  A 3D mesh pipeline would additionally require libdragon's preview branch,
  which publishes nothing to pin against.
* **The emulator is part of the contract.** ares is the authoritative
  Nintendo 64 emulator, and [docs/ARES_VALIDATION.md](docs/ARES_VALIDATION.md)
  has the evaluation. The PlayStation gate is PCSX-Redux with OpenBIOS,
  because ares marks its PlayStation core experimental.

Per-console detail lives in
[platform/nintendo64/README.md](platform/nintendo64/README.md) and
[platform/playstation/README.md](platform/playstation/README.md).

---

# 4. Rendering and maps

Gameplay never issues drawing commands. It produces semantic facts: this
entity, this animation, this position. Each renderer (SDL, terminal, browser,
libdragon, Nugget) decides how to show them. Changing renderer must not affect
gameplay. There is no facing: a character crossing the board to the left is the
same pixels as one crossing to the right.

A map is two layers. The logical map carries terrain, passability, movement
cost and occupancy, and no graphics. The visual map carries tiles, overlays,
elevation and lighting, and is optional: a client that draws nothing plays the
same battle. Elevation is drawn, never simulated.

---

# 5. Battle-local and persistent state

Simulation state divides into battle-local state and persistent campaign
state. Battle-local changes produce explicit campaign outcomes: injury,
permanent death, recruitment, inventory consumption, relationship changes,
objective results, world flags. Applying those outcomes is an authoritative,
deterministic transition. **A permanently dead character cannot reappear
merely because a later map lists that character as available.** Save files
retain both the current state and the package and content identities needed to
interpret it.

Campaign progression is a directed graph. A campaign declares one entry node
and may branch after a completed node using closed, data-defined predicates
over committed objective results, inventory, and typed world flags. Branches
may target the same later node, so routes recombine; cycles may be authored,
but advancing follows at most one edge per completed-node outcome.

Transition selection never depends on array order: conditional transitions
have explicit unique priorities, the lowest matching priority wins, and a node
may declare at most one unconditional fallback. Conditions are evaluated
against one immutable snapshot taken after the outcome batch commits.
Expression strings and scripts are not campaign-flow conditions. A campaign's
shape must be provable by a validator that runs no game code. A build that
cannot execute campaign flow rejects the capability rather than silently
linearizing it.

---

# 6. Packages and content

A game ships as a package: a manifest, compiled content sections, and the
presentation choices the content made. The engine loads the package at
startup, and loading is atomic. Content is organized by independently
versioned categories: classes, unit types, factions, weapons, items,
abilities, maps, opposing-side behaviour, campaigns, scenes, presentation.

Relationships are explicit data, through stable identifiers typed by category
(class `42` and item `42` coexist). Records never reference another record by
pointer, file offset, directory position, or authoring filename. Compiled
records are sorted by ID, which makes output independent of authoring-file
order.

Composition, not inheritance: a unit type composes a class reference, base
statistics, and starting weapon and item references; a class holds stat
modifiers and the weapon types it permits; a weapon defines power, accuracy
and reach. Inheritance would add override order, cycles, editor complexity and
runtime resolution cost; it can come later if typed composition proves
insufficient.

There are no engine-wide maximum counts; each target profile supplies its own
content and working-set budgets and rejects content that exceeds them. Before
emitting any bytes the compiler validates identities, names, numerical
constraints, duplicate relationships and every cross-reference, with stable
diagnostic categories and semantic paths such as `unit_types[60].class_id`.

## The authoring format

The authoring format is the version-controlled project a creator edits; the
package is the compiled artefact the engine loads. The authoring format is
JSON with a JSON Schema Draft 2020-12 contract: one schema drives validation,
editor forms and reference documentation, and strict syntax avoids YAML's
implicit typing. JSON has no comments, so author notes use explicit `notes`
fields. The authoring format is never loaded by a console runtime.

Most creators should use the editor; the public format keeps projects
portable, diffable, scriptable and buildable by CI. See
[tools/source_schema/SOURCE_FORMAT.md](tools/source_schema/SOURCE_FORMAT.md).

## Version and compatibility dimensions

Container version, engine contract range, per-section schema version, feature
requirements, target profile and content revision all change independently.
Before exposing content, a runtime validates all of them plus bounds,
integrity data, identifier uniqueness and references. Unknown optional
sections may be skipped; unknown required sections are a compatibility error.
Major versions indicate incompatible interpretation; minor versions add
backward-compatible data. Persisted packages record concrete versions.

The container is described byte for byte in
[engine/package_format/README.md](engine/package_format/README.md).

---

# 7. Repository structure

```text
engine/     core, simulation, tactics, campaign, package_format, package_runtime,
            campaign_runtime
platform/   client, view, sheet, storage, desktop, web, nintendo64, playstation
tools/      source_schema, game_content, package_check, placeholder_art,
            rom_service
editor/     the local-first web authoring application
games/      demo, tarnholt, template
art/        replacement art a project supplies, and worked examples
schemas/    the canonical source schema, version 1
tests/      cross-module and repository-level tests
cmake/      the opt-in console and WebAssembly target definitions
scripts/    setup, the local gate, and screenshot generation
docs/       evaluations, measurements and decisions
```

Three absences are decisions:

* **Rendering, presentation, scripting, audio and input are not engine
  modules.** The engine stays headless; a renderer lives beside its platform,
  and the only arithmetic they share is `platform/view`.
* **There is one asset tool, not five.** Art is a closed generated vocabulary a
  project chooses from by name: class names choose archetypes, terrain names
  choose tilesets, side order chooses colours. The library grows upstream in
  the generator rather than as bytes an author hands over.
* **The editor is a web application and a core product track.** A content
  contract is incomplete until it can be validated headlessly and represented
  in the editor.

---

# 8. Serialization

A save is the campaign around the battles: persistent unit records retain
identity, availability (unrecruited, available, retired or dead), progression
and inventory, alongside the shared store, objective records, world variables
and how far the authored graph has been walked. References use stable package
and content identities, never memory addresses or transient array positions.
Loading validates package, schema and migration compatibility before mutating
live state.

A battle in progress is not written out. Its state is authoritative and
complete enough to be, but nothing serializes an encounter and a resumed
campaign re-enters the Stage at its opening.

---

# 9. A networkable path

Networking is out of scope, but the simulation contracts keep it reachable:
commands are explicit and serializable, authoritative state has a canonical
digest, random decisions come from recorded deterministic streams, and
presentation and wall-clock state never affect rules. Running one command
sequence through independent simulation instances and comparing digests is
already how browser-against-native conformance is checked.

A future crossover importing a team from another compatible game would be an
interchange operation: a versioned portable roster schema, validated, mapping
or rejecting unsupported content with diagnostics. It would not be permission
for one package to read another package's save representation.

---

# 10. Non-goals

The engine is not tied to a single game, never requires recompilation for a
content change, and does not assume: a specific art style, sprite-based
rendering, 3D rendering, square grids, one unit per tile, one battle system,
or one camera style.

Each of these is a constraint on the architecture, not a feature promise. Hex
grids, multiple floors, flying units, weather and fog of war are things the
design should be able to grow; none is a commitment that it will.
