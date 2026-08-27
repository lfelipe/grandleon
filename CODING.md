# Coding Grandleon

This document is for contributors working on the engine, editor, schemas,
package tools, platform adapters, or project documentation. The root
[README.md](README.md) is intentionally written for people developing games, and
[CONTRIBUTING.md](CONTRIBUTING.md) says how to propose a change and under what
licence contributions are accepted.

- [Prerequisites](#prerequisites)
- [Build and test](#build-and-test)
- [Reproducible toolchains](#reproducible-toolchains)
- [Module documentation](#module-documentation)
- [Architectural invariants](#architectural-invariants)
- [Contract drift gates](#contract-drift-gates)
- [Repository layout](#repository-layout)

## Prerequisites

[docs/SETUP.md](docs/SETUP.md) installs all of this on a bare machine and ends
at a green gate.

| Tool | Version | Needed for |
|---|---|---|
| CMake | 3.20 or newer | engine, tools, tests |
| C++ compiler | any C++17 | engine, tools, tests |
| Clang | any C++17-capable | second `-Werror` leg of the gate; warning sets differ from GCC and each catches what the other misses |
| SDL2 development headers | 2.x (`libsdl2-dev`) | optional; the desktop client's windowed presenter, `grandleon_play --sdl`. Without it `grandleon_play` builds and plays in the terminal, and the only host lane that disappears is `grandleon.sdl_presenter`. The gate compiles that configuration so it stays true |
| ccache | any | optional; used automatically when present, so the gate's second cold build of an unchanged file is a lookup. Absent, everything builds identically and more slowly |
| Node.js | 22.12 or newer | editor, schema tooling |
| Python3 | any, with `venv` (`python3`, `python3-venv`) | the placeholder-art virtual environment `scripts/setup.sh` builds, the art and board-asset staleness legs, and `scripts/check_links.py`, which needs the interpreter alone |
| Container runtime | any Docker-compatible | WebAssembly, Nintendo 64, and PlayStation builds (§Reproducible toolchains) |
| Chromium via Playwright | pinned by `editor/package-lock.json` | real-browser verification |

The last two are pinned rather than "whatever is installed", because their
output is checked in or gates a release-shaped behaviour. See **Reproducible
toolchains** below before installing either by hand.

## Build and test

Prepare the checkout once, then build:

```sh
scripts/setup.sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts/setup.sh` installs both Node dependency trees (`editor` and
`tools/source_schema`, which are separate and the second of which is the one
people forget), the pinned Chromium, and the placeholder-art virtual
environment. It is safe to re-run, it reports what is already in place rather
than redoing it, and `scripts/setup.sh --check` reports what is missing without
changing anything. What it deliberately leaves alone is the container
toolchains of **Reproducible toolchains** below: those are pinned by digest,
pulled or built by their own targets on first use, and cost minutes rather than
seconds, so preparing them eagerly would tax every change that never touches a
console.

If `ccache` is on the machine, the configure step finds it and compiles through
it. `-DGRANDLEON_CCACHE=OFF` turns that off, and a machine without ccache
builds identically. It is worth having, because the gate compiles every
translation unit twice, under two compilers, in a clone made for that run. The
containerised console toolchains do not carry one and are unaffected.

**A cache is kept only while it is shown to be serving.** A cache that serves
nothing reports the same verdict as one that serves everything, so the claim
above is a measurement rather than a property: take it across two gate runs,
because each clones HEAD into a directory whose path differs, and a path in a
compiler's output is exactly what stops a hit from being a hit.

### Verification gate

The gate every change has to pass, in one command:

```sh
scripts/local-ci.sh --preview-port 4521
```

It clones HEAD into a temporary directory and runs, in order: both native legs
under `-Werror`, the desktop client built without SDL2, the editor's production
build, typecheck and unit tests, the real-browser suite, the development server
in a real browser, the startup, deployment
and offline smoke tests, the ROM build service's refusal paths, the staleness
checks over the generated art, the editor's board assets and the WebAssembly module, the provided-art
refusal battery, `scripts/check_links.py`, and `git diff --check`. Run it
before every push, and give it a preview port nobody else is using.

It is a clone of HEAD, so uncommitted work is not in it. Commit before you run
it.

The pieces it is made of, if you need one on its own:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
npm run --prefix editor typecheck
npm test --prefix editor
git diff --check
```

The console checks are not in that list because they build cross toolchains and
emulators from source; run them from the repository root, never from a
subdirectory, or `build` resolves somewhere else and every check silently does
nothing:

```sh
cmake --build build --target grandleon_n64_check_all
cmake --build build --target grandleon_playstation_check
cmake --build build --target grandleon_playstation_render_check
```

A change is not complete until the behaviour it claims is demonstrably true, not
merely until it compiles.

### Worktrees

A `git worktree` starts with none of the above, and two pieces of it are
expensive to obtain a second time: Chromium is about 650 MB, and the
placeholder-art environment is a pinned Pillow build. `scripts/setup.sh`
borrows both from the primary checkout by symlink instead of fetching them
again; their versions are pinned by lockfiles, not by the checkout they sit in,
so a fresh worktree reaches the gate in seconds:

```sh
git worktree add ../grandleon-topic -b topic
cd ../grandleon-topic
scripts/setup.sh
```

Parallel worktrees must not share the browser suite's preview port: it refuses
to reuse a server it did not start, so two runs on one port collide rather than
quietly photographing each other's build. Pass your own:

```sh
scripts/local-ci.sh --preview-port 4531
```

`scripts/local-ci.sh` also runs from a worktree that has not been set up,
borrowing the same two artefacts directly.

Verification runs locally, under both GCC and Clang with `-DGRANDLEON_WERROR=ON`.
Both legs matter: the two compilers catch different things, and Clang's
`-Wbraced-scalar-init` found a real fixture bug GCC accepted silently.

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON
cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error

CXX=clang++ cmake -S . -B build-clang -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_WERROR=ON
cmake --build build-clang && ctest --test-dir build-clang --output-on-failure --no-tests=error
```

`--no-tests=error` because `ctest` finding nothing to run exits 0. An empty
suite, a build that produced no test executables, and a `-R` filter matching
nothing all print "No tests were found!!!" and pass.

`GRANDLEON_WERROR` defaults to `OFF` so an in-progress edit is not blocked by a
warning, and it never applies to `games/` targets, which are separately built
consumers. It applies to the test suite as well as to the engine and the tools:
`tests/CMakeLists.txt` walks its own executables and gives every one of them the
warning flags, so a test added tomorrow is covered without anybody remembering.
One warning is off there and only there: `-Wmissing-field-initializers`,
because a fixture states the part of a definition it is about and leaves the
rest to value-initialise.

**GitHub Actions is disabled** (`workflow_dispatch` only), because the hosted
runners' compiler versions differ from the ones this project pins and the
difference shows up as failures that say nothing about the change.
`scripts/local-ci.sh` is the gate instead: it clones HEAD into a temporary
directory and runs everything there, so untracked files and stale generated
output cannot make a change look ready. Run it before every push.

The consoles run as part of it, whenever a container runtime answers. Each
builds a cross toolchain and an emulator from source the first time it is asked
to, so the first run is slow; where no runtime is installed they are skipped and
the run names what it did not verify.

```sh
scripts/local-ci.sh --preview-port 4531                 # everything available
scripts/local-ci.sh --preview-port 4531 --no-consoles   # the host gate alone
scripts/local-ci.sh --preview-port 4531 --n64           # only the N64 of the two
scripts/local-ci.sh --preview-port 4531 --playstation   # only the PlayStation
```

They were behind flags until 2026-08-27, and the cost of that is on the record:
the PlayStation port stopped building on 2026-08-17 and nothing said so for
fifty commits, over which the weapon triangle shipped and left both consoles'
scripts asserting numbers the rules had stopped producing.

## Reproducible toolchains

Five parts of the build deliberately do not use host tooling, because their
output is either committed to the repository or is the only place a
release-shaped failure can appear. All of them install into the checkout and are
gitignored, so nothing lands outside the repository and nothing needs `sudo`.

### WebAssembly (pinned container)

The engine's browser build runs inside `emscripten/emsdk:4.0.23`, whose digest
is verified before every build. Nothing needs installing by hand; the target
pulls the image on first use:

```sh
cmake --build build --target grandleon_wasm        # rebuild the module
cmake --build build --target grandleon_wasm_check  # fail if it is stale
```

Artifacts land in the gitignored `build-wasm/`. The compiled module is packed
into `editor/src/generated/simulation-module.ts`, which **is** committed, so the
editor builds and tests without a container. Full detail, including the ABI, is
in [platform/web/README.md](platform/web/README.md).

### Nintendo 64 (pinned container)

The console build runs inside a container built from
`platform/nintendo64/Containerfile`, which pins the published libdragon
toolchain image by digest and installs one pinned libdragon commit on top of it.
The published image carries the cross compiler only, so unlike Emscripten there
is nothing ready-made to pull:

```sh
cmake --build build --target grandleon_n64            # build the ROM
cmake --build build --target grandleon_n64_check      # build it and run it
cmake --build build --target grandleon_n64_check_all  # build once, run every check
```

Artifacts land in the gitignored `build-n64/`. Nothing produced by this target
is committed. `grandleon_n64_check` runs the ROM in a second pinned container,
headlessly, and fails unless the ROM reports that every conformance check
passed.

**ares is the authoritative emulator for this console.** It
lives in that container and is what `grandleon_n64_check`,
`grandleon_n64_play_check`, `grandleon_n64_autopilot_check` and
`grandleon_n64_campaign_check` run, through
`platform/nintendo64/ares/run-ares.sh`. Every one of those four builds every ROM
first, because every one of them needs every ROM, so running the four is one
build and three waits on the build script's lock;
`grandleon_n64_check_all` builds once and runs the four concurrently instead,
and is what `scripts/local-ci.sh --n64` asks for. The four stay for debugging
one of them.

**A check is judged on its assertion count, not on the word `PASS`.**
`RESULT PASS 0/0` contains the word and is what a ROM whose assertion table was
emptied prints, so `platform/nintendo64/scripts/check-n64.sh` states the number
of assertions each ROM must clear and
[scripts/assert-harness-verdict.sh](scripts/assert-harness-verdict.sh) decides.
The numbers are floors, not equalities, because the count moves between runs of
one ROM. That script also carries the negative control: `--self-test` feeds it
a battery of logs that measure nothing and requires every one to be refused,
and `check-n64.sh` runs it before it builds anything. It is the decision for
every console here, not this one: the PlayStation's six run scripts state
their own floors and run the same self-test before they start an emulator.

**A check compared against a stale answer is worse than no check.** The
PlayStation's two run scripts compare a run against a transcript derived on the
host beforehand, and a derivation older than the client it compiles answers for
a build that is no longer here: the comparison then prints dozens of mismatched
checkpoints in the exact shape a real regression takes, and a stale run that
happens to still match reports a green check that proved nothing. It has cost an
afternoon.
[scripts/assert-expectations-fresh.sh](scripts/assert-expectations-fresh.sh)
refuses the run rather than deriving the file itself, because a check that
quietly fixes its own inputs is a check that can hide a change; the refusal
prints the one command that puts it right. It carries the same `--self-test`
negative control, for the same reason. The CMake targets already regenerate
before comparing and are unchanged — it is the scripts, the path their own
headers recommend for iterating, that were unguarded.

**A change that shortens the gate leaves every check, every budget and every
wait intact**, and is justified by a before-and-after measurement of the same
lanes on the same host rather than by the argument for it. One that measures
no gain is dropped along with its measurement. Sharing one build between the
four Nintendo 64 checks is the worked example: the checks did not change, and
the numbers that bought it are in `platform/nintendo64/scripts/check-n64.sh`.

The autopilot check is also what produces the README's Nintendo 64 screenshot
and animation. There is deliberately only one emulator here: a second one blind
to the RDP could only witness a second renderer written for its benefit, and the
cross-check that earns its keep is host against console, not emulator against
emulator.

Configure with `-DGRANDLEON_N64_TESTS=ON` to add these runs to `ctest` as
`grandleon.nintendo64`, `grandleon.nintendo64_play`,
`grandleon.nintendo64_autopilot`, `grandleon.nintendo64_campaign`,
`grandleon.nintendo64_editor_rom` and `grandleon.nintendo64_other_game`; they
are off by default because they build a cross toolchain and an emulator from
source on first use. Full
detail is in [platform/nintendo64/README.md](platform/nintendo64/README.md), and
the evaluation that chose ares is in
[docs/ARES_VALIDATION.md](docs/ARES_VALIDATION.md).

### PlayStation (pinned container, pinned SDK, pinned BIOS)

The PlayStation targets build inside a container derived from PCSX-Redux's own
build image, pinned by digest, which already carries Debian's
`g++-mipsel-linux-gnu` and a hosted `libstdc++` for that ABI. The layer on top
adds CMake, Nugget (the MIT SDK, pinned by commit) and mkpsxiso, which writes
the disc:

```sh
cmake --build build --target grandleon_playstation                 # build the executables
cmake --build build --target grandleon_playstation_check           # conformance: run it
cmake --build build --target grandleon_playstation_render_check    # play: check the pixels
cmake --build build --target grandleon_playstation_turn_check      # play a turn into it
cmake --build build --target grandleon_playstation_card_check      # the memory card, across processes
cmake --build build --target grandleon_playstation_campaign_check  # both campaigns, twice over one card
cmake --build build --target grandleon_playstation_disc            # write a burnable .bin and .cue
cmake --build build --target grandleon_playstation_disc_check      # boot that disc through the BIOS shell
```

`scripts/local-ci.sh --playstation` runs those six checks.

Artifacts land in the gitignored `build-playstation/`. Nothing produced by this
target is committed. Configure with `-DGRANDLEON_PLAYSTATION_TESTS=ON` to add
the runs to `ctest` as `grandleon.playstation`, `grandleon.playstation_render`,
`grandleon.playstation_turn`, `grandleon.playstation_card`,
`grandleon.playstation_campaign`, `grandleon.playstation_disc` and
`grandleon.playstation_other_game`; they are off by default because they pull a
cross toolchain image and build an emulator from source on first use.

The last of those is the export path's lane, and it is the pair of
`grandleon.nintendo64_other_game`: it builds a game this repository does not
ship through the service the editor talks to, boots the disc that comes out
through the BIOS shell, and requires the machine to name that game's campaign
and title and neither shipped one's. Which game an image is, is decided at
build time, so nothing a host can check would catch a build that quietly kept
naming Tarnholt.

The disc image carries **no licence sector**. That data is Sony's, and none of
it is fetched or vendored. `platform/playstation/scripts/build-disc.sh` reads
those sectors back and fails if any byte of them is not zero. A stock
PlayStation reads them too, and will refuse the disc;
[platform/playstation/README.md](platform/playstation/README.md) says so and
does not work around it.

**The emulator here is not ares**, and for a stated reason rather than a
drift: ares marks its PlayStation core Experimental, where the cores this
repository uses it for carry no such marking. The gate is PCSX-Redux with
OpenBIOS, a reverse-engineered MIT BIOS built from the same pinned commit as
the emulator, so no copyrighted firmware is involved. A post-build step asserts
the linked executable's ELF header says `mips1`, because the toolchain's
prebuilt libraries are built for a later ISA and the R3000A's response to an
instruction it does not implement is a wrong answer rather than a crash. Full
detail is in [platform/playstation/README.md](platform/playstation/README.md).

### Real-browser verification (Playwright)

Chromium is the only engine that enforces a Content Security Policy against
WebAssembly compilation, so it is the only place a CSP regression is
observable. The `happy-dom` suites structurally cannot see one. It installs
into the gitignored `.playwright-browsers/` at the repository root, which
`scripts/setup.sh` does for you. By hand it is:

```sh
npm ci --prefix editor
PLAYWRIGHT_BROWSERS_PATH="$PWD/.playwright-browsers" \
  npx --prefix editor playwright install chromium
```

`editor/playwright.config.ts` defaults `PLAYWRIGHT_BROWSERS_PATH` to that
directory, so after the one-time install the suite needs no environment:

```sh
npm run --prefix editor test:browser
```

It builds the production bundle and serves it through `vite preview`, so the
browser sees the same CSP, bundling, and embedded WebAssembly module a
deployment does. Add `--with-deps` to the install command on a bare CI machine
to pull Chromium's system libraries; that one needs root and is why it is not
part of the local instructions.

### Whole environment (dev container)

`.devcontainer/` pins the host toolchain (compilers, CMake, Node, and
Chromium's shared libraries) for contributors who would rather not install
them. It mounts the host container runtime's socket so the Emscripten build
still works from inside; see
[.devcontainer/README.md](.devcontainer/README.md) for that trade-off. The dev
container is a convenience, not the definition of the build: the gate runs on
a plain machine with the versions in the table above.

The editor’s focused commands are:

```sh
npm run --prefix editor typecheck
npm test --prefix editor
npm run --prefix editor build
npm run --prefix editor test:startup
npm run --prefix editor test:deployment
npm run --prefix editor test:offline
npm run --prefix editor test:browser
npm run --prefix editor test:dev
```

`test:browser` and `test:dev` need the one-time Chromium install described
above; the others do not. `test:dev` is the only one that loads the development
server rather than a production build, which is the difference between a page
whose stylesheets are bundled files and one whose stylesheets are inline
elements a Content Security Policy can refuse. `editor/README.md` has the
detail.

`npm run --prefix editor preview` serves the built editor, which is what the
browser suite drives; both it and `dev` take `-- --port N`.

Before submitting a change, run the complete applicable gate and
`git diff --check`.

## Module documentation

Every module under `engine/`, `platform/`, `tools/`, `games/` and `editor/`
carries its own README describing what it owns. The ones a contributor reaches
for first:

- [docs/FROM_EDITOR_TO_CONSOLE.md](docs/FROM_EDITOR_TO_CONSOLE.md) describes the
  whole chain from authored source to compiled package to console pixel, how
  the target surfaces relate, and what each layer of verification proves. Start
  here if the module boundaries are not yet obvious.
- [engine/core/README.md](engine/core/README.md) describes the substrate:
  version, content identity, and the deterministic random stream.
- [engine/simulation/README.md](engine/simulation/README.md) describes the
  deterministic encounter contract and browser/native conformance.
- [engine/tactics/README.md](engine/tactics/README.md) describes how a unit
  nobody is steering chooses a command, and why that is policy rather than
  rules.
- [engine/package_format/README.md](engine/package_format/README.md) describes
  the experimental versioned container.
- [engine/package_runtime/README.md](engine/package_runtime/README.md) describes
  semantic decoding and campaign execution.
- [platform/client/README.md](platform/client/README.md) describes the shared
  session (campaign walk, battle loop, the opposing side) and the presenter
  seam a new front end implements.
- [platform/view/README.md](platform/view/README.md) describes the board's
  shared presentation model (projection, elevation, autotile lookup, draw
  order), which sits above the three renderers and below the presenter seam.
- [platform/sheet/README.md](platform/sheet/README.md) describes the unit
  information sheet every client draws and none of them composes, and the
  display-name table both console action menus read.
- [platform/desktop/README.md](platform/desktop/README.md) describes the
  desktop client and the presenter seam its front ends share.
- [platform/web/README.md](platform/web/README.md) describes the pinned
  Emscripten toolchain and the WebAssembly binding surface.
- [platform/nintendo64/README.md](platform/nintendo64/README.md) describes the
  pinned libdragon toolchain and the console conformance ROM.
- [platform/playstation/README.md](platform/playstation/README.md) describes the
  pinned Nugget toolchain, the R3000A entry gate, the ISA trap, and
  three-channel pixel verification.
- [tools/game_content/README.md](tools/game_content/README.md) describes the
  semantic compiler and installed SDK.
- [tools/source_schema/README.md](tools/source_schema/README.md) describes the
  canonical source schema and validator.
- [tools/placeholder_art/README.md](tools/placeholder_art/README.md) describes
  the art generator, its output profiles, and its reproducibility check.
- [tools/package_check/README.md](tools/package_check/README.md) describes the
  container-only validator for a compiled `.gpk`.
- [editor/README.md](editor/README.md) describes editor development and static
  deployment.

A few documents are records of one measurement or one evaluation:
`docs/ARES_VALIDATION.md` and the completed editor framework evaluation under
`editor/spikes/ui-framework/`. **A measured number in one of them is the number
that run produced, and rewriting it to agree with a later run destroys the only
thing it was worth.** If a measurement no longer holds, take the measurement
again and say what both runs measured. Do not quietly overwrite the figure.

## Architectural invariants

[DESIGN.md](DESIGN.md) §3 and §3.1 give the layering and the reasoning behind
it. This section is what that layering asks of a change: the rules a change has
to keep, stated as rules. They are separate from the architecture because these
are the ones that actually get broken. Nobody disagrees with them; they break
when a second copy of an answer appears somewhere it was cheaper to write than
to ask for.

Nothing here is checked by a linter, and there is no architecture check that
names a forbidden dependency edge. What enforces these is the shape of the
build (the link lines, the gate leg that builds the desktop client without
SDL2, and two cross compiles that fail the moment an inner module reaches
outward), together with the host-derived console expectations for everything
about what is drawn. A rule that survives only because somebody remembers it
belongs in a test instead; if you find one here that could be pinned, pin it.

### Where an answer is allowed to live

**Authoritative gameplay state has one owner, and it is `engine/simulation`.**
Presentation, renderers, platform adapters, tools and game packages reach it
through commands, immutable observations, events and resource interfaces, and
never mutate it. The same accepted command stream run under different
renderers, or none at all, reaches the same canonical hash.

**A rule the simulation answers is not re-derived by a client.** Where there is
a read-only query (reachability, the danger zone, the tiles an aimed gesture
may land on), every client draws that query's own tiles in that query's own
order. A client may decide whether to show an answer; it may not decide what
the answer is. A platform boundary carries the query rather than obliging a
client to copy the rule across it, which is why the WebAssembly ABI exposes the
queries instead of hiding a TypeScript reimplementation behind them. Spelling a
predicate out again is the same mistake in miniature: who is standing on the
board is `simulation::on_board`, never `health > 0`. `engine/tactics` is under
the rule too: a behaviour policy may choose among the moves the rules allow,
and asks the simulation what the ground allows rather than keeping its own
answer.

**A rule the simulation applies but exposes no query for gets exactly one
client copy, pinned to the engine by an agreement test.** The Manhattan reach
band and the Manhattan area of impact are the worked example, and the shape of
the answer is the lesson. A board being played derives neither: the engine
answers both (`gesture_available`, `aimable_tiles`, `area_tiles`), so nothing
on the play path computes a tile at all, and a board can never light a square
the engine will refuse. What is left is the surface that has no encounter to
ask, a shape picker drawing a shape and a radius before there is a caster or a
board, and it holds the rule exactly once
(`editor/src/domain/targeting-geometry.ts`). Its tests derive the expected
answer from the running engine (apply commands, observe what it accepts,
refuses and affects) rather than from what the author of the copy believed the
rule was. A second surface computing a band for itself is precisely the failure
those tests exist to catch.

**A refusal crosses a binding as a name, not as a number.** A binding carries
the refusing layer's own word for the refusal, read out of the module that
defines the vocabulary. Do not restate an enumeration on the far side of a
boundary, and never show a person a bare enumerator: a new refusal should reach
a surface without a second edit, and a refusal table copied into four places
diverges in four places.

**A campaign fact is derived once.** Turning a finished battle into campaign
consequences happens in `engine/campaign_runtime`, the one module that may read
compiled content and persistent campaign state at once; a client reads that
result and presents it rather than recomputing an experience total, a level or
an inventory movement in its own language. The sequence around it is
`CampaignSession`: founding or resuming a roster, excluding the unavailable,
deriving, committing as one outcome batch, saving, walking the graph edge. A
binding transports its steps rather than rebuilding them on the far side of the
boundary.

**Content identity is derived one way.** A client producing campaign state for
authored content derives that content's package identity and content revision
exactly as `tools/game_content` derives them from the same source project. This
is not bookkeeping: a growth roll is seeded from the completed battle's whole
encounter reference, package identity included, so a client that identified
content differently would hand out level-ups the compiled game will never roll.

**The campaign session does not know which platform it is saving on.** It
reaches durable storage only through the slot interface every platform
implements, and gains no code path, option or compilation mode because one host
keeps bytes in a particular way. Where a host's store cannot answer inside the
call (a store that replies on a later turn of an event loop, a device that must
be polled), the host carries bytes between that store and a slot device around
the session's own saves, rather than the session's save learning how to
suspend.

**The simulation does not learn that a campaign exists**, and gains no command,
event or snapshot field whose purpose is to end a side's turn or to name an
animation. A side's turn ends because every activation it held was spent,
exactly as it ends when a player spends them one at a time. Which of a side's
characters still owes an activation is answered once, in the platform-free
client layer, from the engine's own snapshot fields.

### What may be drawn, and what may not be invented

**Elevation is drawn, never simulated.** A terrain kind declares how many
levels it reads as; no rule reads that, no snapshot carries it, no canonical
hash observes it, and a client that ignores it draws a correct board. The
declared level count is content. Nothing caps or clamps it, in the art library
or in any table generated from it, because a consumer able to draw real relief
is entitled to know how tall a terrain *is*. Only the drawn lift is bounded.
Content whose terrain is all at level ground draws exactly as the same content
draws with elevation absent: same origin, same rectangles, same sampled
positions.

**The board's arithmetic and its motion have one implementation, and it has no
clock.** `platform/view` owns the camera, the cell-to-pixel projection, the
autotile lookup, the draw order, the frame counts, the sequence cells and the
gesture selection; every renderer derives them from there, and any probe that
reads a rendered board locates a cell through the projection that drew it.
Every quantity is counted in drawn frames. Nothing reads a clock, measures
elapsed time, or interpolates against a refresh rate, so one sequence of
presses produces one sequence of frames on every run and every emulator. The
model links no rendering SDK, no engine module and no host tooling, and
allocates nothing, which is what lets a console animate without a heap and the
whole of it be tested with no console, window or browser in the loop.

**A drawn route comes from the reachability query.** Every tile a token is
drawn standing on is a tile that query returned, and the route is no longer
than the query's own walk. Where no route through those tiles can be found,
draw the straight line rather than a route of your own devising. A route is
never simulated: no snapshot field carries one, no hash observes one, and a
client that draws none draws a correct board.

**Which gesture an attack is drawn as is derived, never announced.** The
simulation gains no field, event or snapshot member whose purpose is to name an
animation. A renderer derives the gesture through the shared model from what a
presenter already holds: the snapshot, the command's events, and the weapon and
ability records handed over before the first frame. Two clients drawing the
same attack out of the same snapshot pick the same gesture. Reaching further
does not by itself make a blow a shot: a weapon whose band covers both an
adjacent tile and a distant one is a long arm, not a launcher. A missed
attack is drawn as an attack that was thrown, with nothing knocked back, which
is the difference a miss exists to say.

**Anything that varies with time settles.** Every animation ends exactly at
rest: the last frame of a slide is its destination, the last frame of a flinch
is the struck token where it stands, a walk begins and ends on the standing
cell. Every periodic function returns, at phase zero, exactly what the same
board draws with that function absent. A terrain animation takes its phase from
the board's own frame counter and from nothing that survives the board being
left and re-entered, and moves not one byte of committed art. A projectile or
an effect drawn over the board costs no sequence cell and no palette entry, and
is absent again on the frame its gesture ends. This is not decoration: a
console check samples a settled frame and asserts pixels, so every assertion
that holds over the unanimated board has to keep holding over the animated one,
**without being loosened**.

**A settled frame keeps what was on it.** Settling the things that vary with
time must not repaint over what was drawn on top of the board. A menu, an
information sheet or a question is still there afterwards, because that is the
frame the player was looking at.

**A limit is refused, not written down.** The sequence-cell ceiling is a
`static_assert` carrying the arithmetic that produces it (cell size, bytes per
texel, texture memory available) rather than a sentence in a README, so a
client whose budget changes moves the limit honestly instead of silently
exceeding it. Prefer this shape wherever a constraint can be expressed as one.

**A rule about colour is a function, not a table.** How a spent character is
greyed lives in the shared presentation model as a pure integer function of the
colour handed to it, so a host deriving an expectation and a console drawing a
frame reach the identical answer and no list of grey colours is written down
anywhere. A client holding true-colour rather than indexed art implements a
palette-cycling animation as a permutation at draw time over the same colours
in the same order, rather than shipping a second copy of the art per phase.

**A menu offers a row exactly when the engine would accept the gesture.** Ask
the simulation whether a gesture is available; do not track for yourself what a
character has spent. A row is absent when the gesture behind it would be
refused whatever it were aimed at, and present when only its aim could be
refused, even when nothing on the board can currently be aimed at. That is what
the lit tiles are for, and a row that came and went for a reason the board does
not show teaches a rule nobody can see.

### Consoles

**A console's interactive client is shared, not copied.** The cursor, the
selection, the queries a selection asks for, the action menu, the information
sheet, the aiming state and the transcript a run reports live in one
translation unit every console compiles. It draws no pixel, reads no port and
names no machine; what a machine supplies is a seam of platform functions, and
every animation hook on that seam has a do-nothing default, so a build that
draws nothing walks the same code and reports the same transcript as a build
that draws everything. The button vocabulary a script is written in belongs to
that shared unit, and each console documents which of its own buttons it maps
onto each name.

**A console's checked run is derived before it is built.** Expectations come
from the engine's own queries over the state the shared session actually
reaches, computed on the host by a tool that compiles the same client source
and replays the same presses, and written *before* the console image is built.
The console additionally claims the coordinate and colour of pixels it believes
it drew, computed from the shared presentation model and the art library rather
than from anything the display hardware touched, and a joiner that links
nothing from this repository requires transcript and pixels to match with no
tolerance. Order matters and is the point: an expectation that could be
adjusted to fit a run is not an expectation.

**A check that samples a filtered layer still asserts equality.** Derive the
filtered value from the same art and the same arithmetic the hardware used and
compare exactly. Do not loosen to a tolerance, a neighbourhood or a colour
distance: a check that admits a range admits the failure it exists to catch.
Where a layer's art cannot survive filtering, draw it point-sampled and record
the evidence that settled it rather than leaving the next reader to infer it
from the other layer's choice.

Keep [DESIGN.md](DESIGN.md) for the architecture and the reasoning, this
section for the rules a change has to keep, and the module READMEs for concrete
usage. [platform/view/README.md](platform/view/README.md) and
[platform/client/README.md](platform/client/README.md) carry most of the detail
behind the drawing and console rules above.

## Contract drift gates

The editor build regenerates canonical source-schema TypeScript contracts in
check mode and fails when committed output is stale. The command-line source
validator and browser analyzer run against the same conformance fixtures and
compare stable diagnostic code/path pairs.

The editor's playtest and Play mode run the authoritative portable C++
simulation compiled to WebAssembly, not a JavaScript reimplementation of it. The
compiled module is checked in at `editor/src/generated/simulation-module.ts` and
is rebuilt inside a pinned Emscripten SDK container; the gate fails when the
checked-in copy is not what that toolchain produces. See
[platform/web/README.md](platform/web/README.md).

Every new authorable feature must update the source schema, compatibility rules,
shared validation, editor coverage, transactional commands, fixtures, runtime
mapping, public documentation, and applicable conformance tests.

## Repository layout

```text
engine/       core, simulation, tactics, package format, and package runtime
platform/     the shared client session and presenter seam, the board
              presentation model every renderer shares, and the adapters:
              SDL and terminal, web, libdragon, Nugget
tools/        source validation, package compilation, package checking,
              and the placeholder-art generator
editor/       local-first web authoring application
games/
  template/   reusable separately built game skeleton
  demo/       maintained two-team vertical slice, and the conformance reference
  tarnholt/   the larger sample campaign: six maps, abilities, branching flow
schemas/      the canonical source schema, version 1
tests/        cross-module and repository-level tests
cmake/        the opt-in console and WebAssembly target definitions
scripts/      setup, the local gate, and README screenshot generation
docs/         evaluations, measurements, and decisions
```
