# Nintendo 64

The console target for the authoritative engine. Three kinds of ROM. The
conformance ROM proves the portable engine runs on the hardware and reproduces
the host's canonical hashes. The play ROM is the Tarnholt campaign, playable,
drawing the generated CI4 art through the RDP, checked under ares. The campaign
ROM is the same campaign **kept**: written to the cartridge's SRAM after every
gesture that commits, and read back after the power switch.

## Contents

- [Toolchain](#toolchain)
- [Building](#building)
- [The optional 3D character prototypes](#the-optional-3d-character-prototypes)
- [A ROM of some other game](#a-rom-of-some-other-game)
- [C++ subset](#c-subset)
- [The conformance ROM](#the-conformance-rom)
- [Running the checks](#running-the-checks)
- [What is verified, and what is not](#what-is-verified-and-what-is-not)
- [The play ROM](#the-play-rom)
- [The campaign ROM: a save that survives the power switch](#the-campaign-rom-a-save-that-survives-the-power-switch)

## Toolchain

The build runs inside a pinned container. The published
`ghcr.io/dragonminded/libdragon` image carries only the `mips64-elf` GCC cross
compiler: no libdragon library, no `n64.mk`, no `n64tool`, no linker script. So
it is pinned by digest and used as a base, and `Containerfile` builds one
pinned libdragon commit on top of it.

| | |
|---|---|
| Base image | `ghcr.io/dragonminded/libdragon:trunk`, pinned by digest |
| Compiler | `mips64-elf-gcc` / `mips64-elf-g++` 14.4.0, newlib, libstdc++ |
| libdragon | one pinned commit, recorded in the image at `/n64_toolchain/LIBDRAGON_COMMIT` |

The base digest is verified before the derived image is built, and the build
script checks the recorded libdragon commit, so a moved tag or a stale local
image fails the build rather than silently supplying a different compiler.
Both pins live in `cmake/GrandleonNintendo64.cmake` and can be overridden with
`-DGRANDLEON_LIBDRAGON_BASE_DIGEST=` and `-DGRANDLEON_LIBDRAGON_COMMIT=`. One
gap: the derived image installs CMake from Ubuntu's archive unpinned. The
compiler and libdragon are pinned, the build system driving them is not.

The toolchain file is transcribed from libdragon's `n64.mk` at the pinned
commit, with two deviations:

- **`n64.mk`'s `-Wall -Werror` and its `-Wno-error=` escapes are dropped.**
  This repository applies its own warning discipline.
- **`n64.mk`'s `-I$(N64_INCLUDEDIR)` is dropped.** That directory is already
  the compiler's default system include path; naming it with `-isystem`
  reorders the system chain ahead of libstdc++, and `<cstdlib>`'s
  `#include_next <stdlib.h>` then finds nothing.

## Building

Never part of the default host build; every target is opt-in and excluded
from `ALL`:

```sh
cmake --build build --target grandleon_n64        # engine libraries + ROMs, for MIPS
cmake --build build --target grandleon_n64_check  # build, run, fail unless the ROM passes
```

Both shell out to scripts under `scripts/`, which can be run directly from
the repository root. `build-n64.sh` configures the ordinary top-level
`CMakeLists.txt` with `cmake/toolchains/Nintendo64.cmake`, so the console
build compiles the same library targets the native build does. Under
`-DGRANDLEON_N64=ON` the root project stops after the five engine libraries
and this directory. Artifacts land in the gitignored `build-n64/`; nothing
produced here is checked in.

## The 3D campaign and character diagnostics

0.4 carries a focused Tiny3D diagnostic alongside the complete Sevenfold 3D
campaign and roster viewer. The focused comparison reduces the newer Godot
commander and rogue meshes and shows them at strategic, tactical, and action
distances:

```sh
platform/nintendo64/scripts/build-character-prototype.sh
platform/nintendo64/scripts/check-character-prototype.sh
```

The first command creates
`build-n64-character-prototype/platform/nintendo64/grandleon-n64-3d-character-prototype.z64`.
Add `--bundle out/n64-3d-character-prototype` to stage the ROM with its README
and licence notices. The check rebuilds it, boots it in ares, and requires all
four allocation and frame-budget assertions to pass.

The Sevenfold 3D build adds a playable campaign renderer and a separate viewer
covering all 112 combinations of seven styles, eight archetypes, and
male/female body variants:

```sh
platform/nintendo64/scripts/build-sevenfold-3d.sh
platform/nintendo64/scripts/check-sevenfold-3d.sh
```

To build the playable sprite campaign, playable 3D campaign, and diagnostic
viewer together, with the debug Stage picker and live FPS enabled:

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

`build-n64-sevenfold-roms/` then holds `sevenfold-n64-sprite.z64`,
`sevenfold-n64-3d.z64`, `sevenfold-n64-3d-viewer.z64`, and a short manifest.
The first two are the same 1P/2P campaign with different renderers. The
`-viewer` ROM is the roster diagnostic described below.

It shows eight separated roles per strategic page, exercises red and blue team
colour replacement, and uses the seven varied terrain layouts from
`games/sevenfold`. The figures use camera-facing three-quarter headings rather
than exact side views, keeping their shields, bows, books, banners and paired
weapons visible. The baked vertex lighting also keeps a wider separation
between shadow and highlight faces without paying for runtime lights. **A**
switches between low and high detail; the d-pad changes style or body, and
**Z** cycles the normal board and a material-textured tactical board. On the
medieval page it also shows an experimental 2.5D board using the curated
character art. **B** plays that board's short tile movement.
**Start** opens a two-character high-detail attack view whose floor, skyline,
and sky colour are derived from the selected terrain. **B** then plays an
illustrative attack, hit, counter-attack, and damage callout. In that close
view, **Z** cycles the flat proxy, material-textured proxy, and, for the
medieval knight and mythical mage comparisons, a hybrid 2.5D presentation.
The hybrid uses 84×84 CI4 cutouts derived from the actual curated Godot frames
over the same textured 3D terrain. The generator fits one union crop for all
poses of a character and preserves Godot's ground pivot, so changing pose does
not resize or jitter the fighter. **B** plays idle, wind-up, follow-through,
hurt, recoil, and settle poses with eased movement. The first attacker remains
on screen left and the defender on screen right for the whole exchange. A
counter moves the right fighter into the left fighter; it never swaps their
screen sides.

The playable `sevenfold-n64-3d.z64` uses the same proxies differently: low
detail on the complete campaign board, actual 3D route movement, and two
high-detail figures when a real attack or miss occurs. The close view samples
the current map terrain, displays the simulation's damage, and naturally shows
a legal counter when its authoritative event follows. This is separate from
the viewer's illustrative animation.

Its ares check captures 28 roster-matrix comparisons, two same-team
three-quarter pages that expose all eight low-detail roles from opposite
headings, matched flat/material tactical boards for medieval and pirates,
an eight-role 2.5D tactical board and tile movement, seven terrain close
views, the proxy animation, and matched proxy/hybrid close views for medieval
and mythical representatives. It also records the canonical 8.15-second
cross-renderer scene: the same six-by-four forest/grass/paved board, knights,
health, damage, counter, and phase timing used by Godot and PlayStation. Its
schema-2 capture surrounds those active cells with the shared twelve-by-ten
forest composition, fills the native 320×240 image, and removes the diagnostic
title and footer. Only in-world health, selection/target marks, and transient
damage remain. The current run passes `63/63` and records the complete
checkpoint trail. The two
low-poly tactical material views measure 78 and 74 fps,
versus 81 and 76 fps for their identical flat views. Textured
proxy close views measure 153 fps for medieval and 129 fps for mythical. The
hybrid idle/impact views measure 389–503 fps, and its five-pose attack/counter
exchange peaks at 2.68 ms. The full 2.5D board measures 396 fps while static
and 287 fps at its slowest animated frame. The schema-2 canonical scene peaks
at 2.71 ms; its first sixteen-tile backdrop frame takes 1.87 ms. These are
renderer-throughput measurements in
ares, not display rate or timings from original hardware.

Both texture paths are deliberately confined to the diagnostic viewer while
they are evaluated. The terrain-quality pass uses original, deterministic
64×64 CI4 material and prop atlases. The material atlas has four 32-pixel
regions for grass, paved stone, earth and water-like surfaces. Stable per-cell
offsets break up repetition, while a slight tone change at the edge of a
connected terrain cluster makes transitions easier to see without hiding the
tactical grid. A separate alpha-clipped pass groups sparse grass, shrub, rock
and tree props around board edges and close-view backgrounds. It does not put
props over active units or movement highlights. The material and prop atlases
total 4,432 embedded bytes; either atlas occupies 2,080 bytes of TMEM, well
inside the 4 KiB limit. Loading the two sprite objects copies those same 4,432
bytes into RDRAM for the viewer.
The live close-view strip adds one 64×32 CI4 grass/paved atlas: 1,192 embedded
and RDRAM bytes, with a 1,056-byte TMEM peak. It is repeated only across the
lower 72 scanlines so the pre-rendered plate and active terrain meet without a
large flat 3D foreground slab.

The matched close view adds a data-driven pre-rendered backdrop proof. Its
source is the shared set of Godot-rendered 1280×720 panorama, transition,
midground, and prop layers. Three approved forest/grass/paved variants are
downsampled to 320×160 and split into sixteen independently paletted 80×40 CI4
tiles each. A tile uses 1,632 bytes of TMEM, so the renderer streams ordinary
sprites instead of requiring the whole plate to fit at once. Selection keys on
dominant, secondary and transition terrain IDs, transition orientation, and a
stable seed; this showcase resolves variant 2 and keeps it frozen through the
attack and counter. The plate owns the sky and distant scenery, while the live
modular grass/paved floor strip, character cutouts, shadows, hit effect and
compact health/damage overlays remain dynamic. All three variants occupy
84,864 bytes in ROM and the same amount when loaded in the diagnostic viewer's
RDRAM. Their
addition moves the aligned viewer ROM from 1,261,568 to 1,294,336 bytes.

The close candidate embeds
75,664 bytes for five poses per fighter, two styles, shadows, and impact. One
84×84 CI4 cutout uses 4,064 bytes of TMEM after the RDP's row alignment,
leaving 32 bytes free. CI8 at that resolution cannot fit. The board keeps its
useful 56×56 native footprint but uses CI8: 256 colours improve the small
silhouettes more than extra pixels would. Its eighteen idle/walk objects total
68,112 bytes and use 3,648 bytes of TMEM per character. Eight characters and
shadows upload about 30.8 KiB per frame. The terrain atlases and grouped prop
pass grow the aligned viewer ROM by 16 KiB, from 1,245,184 to 1,261,568 bytes,
over the previous 2.5D quality checkpoint. Units remain sorted by foot
position, with smaller far-row figures to keep silhouettes apart.

The close canvas is the useful CI4 limit rather than an arbitrary number. An
84-pixel row rounds to 48 bytes in TMEM; 84 rows plus the 16-colour palette use
4,064 of 4,096 bytes. Moving to CI8 would limit a square cutout to roughly 56
pixels after its 512-byte palette, which loses visibly more edge detail than
the extra colours recover. On the board the character is only about 40–50
screen pixels tall, so 56-pixel CI8 is the complementary quality choice.
The playable 3D campaign remains unchanged until the material and hybrid
treatments pass MiSTer or original-style hardware review.

The checked-in cutouts can be regenerated from the exact curated sheet and
team-mask hashes. The terrain atlases are generated locally from original
procedural art; their manifest pins both the generator and output hashes:

```sh
platform/nintendo64/scripts/generate-sevenfold-hybrid-assets.py
platform/nintendo64/scripts/generate-sevenfold-terrain-assets.py
platform/nintendo64/scripts/generate-sevenfold-backdrop-assets.py
```

The full-roster models are deterministic procedural console proxies; the
curated character sheets do not contain hidden 3D source meshes. The 3D
campaign is now an end-to-end production test path, while the sprite build
remains a useful visual and gameplay baseline. The 3D graphics will continue
to improve across releases.

[Console 3D](../../docs/CONSOLE_3D_EXPERIMENT.md) gives the plain-language
status, build outputs, measurements, and ongoing graphics priorities.

## A ROM of some other game

The ROM's content is a build input. `GRANDLEON_N64_PROJECT` chooses the
project the play and campaign ROMs embed, defaulting to the shipped one:

```sh
platform/nintendo64/scripts/build-n64.sh \
    --project games/demo/source/project.json \
    --targets grandleon_n64_campaign
```

That is the whole mechanism behind the editor's **Nintendo 64 ROM** button:
`tools/rom_service/` stages an author's project inside the repository and runs
exactly that line. There is no separate content path for an authored game: this
console parses and compiles authored JSON on the machine.

Two consequences, both checked:

- The ROM takes its campaign key and save-slot name from a generated
  `project_identity.h`, not from
  `games/tarnholt/autopilot/campaign_expectations.h`, which is about what a
  scripted run of one game should observe. The autopilot build asserts the
  derivation still produces the shipped game's two strings.
- Every ROM reports which game it is, before the title screen:
  `game campaign=… slot=… source=…`. `grandleon.nintendo64_other_game` boots a
  ROM built for `games/demo` under ares and is judged on that line. Only a
  boot can say which campaign the running program asked for.

With no `--project`, the script clears the cache entry rather than leaving
it, so a tree in which somebody once built an author's game does not go on
building it.

## C++ subset

**The Nintendo 64 build required no engine source changes.** All five libraries
(`core`, `simulation`, `tactics`, `package_format`, `package_runtime`) compile
under `-Wall -Wextra -Wpedantic -Wconversion -Werror` with no warnings, no
`#ifdef`, no target-specific code.

The subset is wider than the WebAssembly one: a real GCC with a real
libstdc++, so `std::vector`, `std::set`, `std::map`, `std::deque` and
`std::string_view` all work, with a working heap behind `operator new`.
Exceptions and RTTI stay enabled for the engine libraries, since libdragon
compiles its own C++ support with them. The ROM translation units add
`-fno-exceptions -fno-rtti`.

## The conformance ROM

`src/conformance_rom.cpp` replays the shared reference vector from
`tests/simulation/encounter_test.cpp` (same definition, same five commands) and
compares `canonical_hash()` against the initial and completed values that
vector pins. [engine/simulation/README.md](../../engine/simulation/README.md)
defines the vector and holds the two numbers. It also checks that the
target is big-endian, that a rejected command leaves canonical state
byte-identical, that the tactics search proposes a command the simulation
accepts, and that a package written by `write_mock_package` loads back,
fails to load when one byte is flipped, and is declined by the encounter and
campaign loaders when a needed section is absent.

`check-n64.sh` holds the run to a minimum check count
(`GRANDLEON_N64_ARES_MIN_CHECKS`), so a check that stops being made fails the
lane rather than passing quietly. The ROM prints each check and then a single
machine-checkable `RESULT PASS n/n` line, repeated once a second for ever,
because a ROM has nowhere to return to. Output goes to `printf`; libdragon
fans standard output to the on-screen console and the ISViewer channel.

## Running the checks

```sh
cmake --build build --target grandleon_n64_check_all       # all five checks
cmake --build build --target grandleon_n64_check           # conformance ROM
cmake --build build --target grandleon_n64_play_check      # render probe
cmake --build build --target grandleon_n64_autopilot_check # autopilot
cmake --build build --target grandleon_n64_coop_check      # both controller ports
cmake --build build --target grandleon_n64_campaign_check  # the save survives a restart
platform/nintendo64/ares/run-ares.sh                       # any ROM, under ares
```

**Use the first unless you are debugging.** Four checks share the supported
Tarnholt ROM build. The co-op check builds Emberbound separately because its
authored Player 1 and Player 2 opening is what the test needs. The combined
target builds each project once, then runs all five emulator checks together,
failing by name. It is not a cache: the builds happen on every run and finish
before any emulator starts. Each check writes a verdict beside its log, and a
check that leaves none is reported as not having run rather than passed over.
The single-check targets and the five `grandleon.nintendo64*` ctest lanes ask
`check-n64.sh` for their check by name, so which ROM runs under what budget is
written in one place.

Everything runs under **ares**, this console's authoritative emulator and the
only pinned harness that can run these ROMs at all: the board is drawn through
the RDP on libdragon's rspq
microcode, and ares runs the real pipeline (CPU/RSP recompilers, paraLLEl-RDP
on software Vulkan) and writes rendered pixels back to RDRAM where the in-ROM
probes sample them. There is deliberately no second emulator: mupen64plus's
pinned `rsp-hle` cannot execute rspq microcode, so it can see nothing the RDP
draws. The cross-check that carries weight is host against console: every hash
and campaign expectation these ROMs assert is derived independently in the
native suite.

`ctest` does not run these by default: they need a container runtime and
build a cross toolchain and an emulator from source on first use. Configure
with `-DGRANDLEON_N64_TESTS=ON`.

## What is verified, and what is not

Verified, by running it: the five libraries compile for `mips64-elf` under
full warnings with no source changes; they link into a bootable `.z64`; the
engine reproduces both pinned hashes on big-endian MIPS; rejected commands
are atomic; allocation-heavy paths work against libdragon's heap; the package
container round-trips and rejects corruption. A dedicated Emberbound ROM also
drives both controller ports through one full side block under ares and checks
independent ownership, cursors, selections, accepted commands, co-op-only
placements, the opponent response, and the next allied round.

Not verified, and not claimed:

- **Nothing has run on real hardware.** Every result comes from ares.
- **No performance number is claimed** for this target.
- **Saving is verified under one emulator, and only under emulation.** The
  power-switch check restarts the emulator process to prove the bytes outlive
  it, but nothing has written to a real battery. The PI domain-2 timing
  registers this ROM configures are the part an emulator does not need and
  hardware does; they are set, and they are untested.

## The play ROM

`src/play_rom.cpp` is the campaign, playable. The checked-in Tarnholt source
project is embedded as bytes and **parsed and compiled on the console**. It is
the same JSON the editor loads, and it runs through the shared client session
in `platform/client`; the only code here is the presenter: CPU framebuffer
drawing and controller input.

Controls, through the same intent seam every client uses: d-pad moves the
cursor, A selects / moves / attacks with the weapon in hand, B backs out one
step, Start opens the battle menu, and Z opens the **unit action menu**.

Controller ports one and two are sampled on every played frame. Each player
keeps a separate cursor, selection, threat toggle, and aimed action; the second
cursor is cyan while the active cursor retains the normal amber/white state.
If both pads produce an edge together, one is handled and the other remains
pending for the next frame. Menus are shared pause surfaces, but outside them
neither player waits for ownership of the board cursor. Authored `sideBlocks`
encounters let both players combine their allied activations before the
computer-controlled side begins.

This path is automated, not just documented. `grandleon_n64_coop_check` boots
Emberbound under ares, sends input first from controller port two and then from
port one, and checks the board state from inside the running ROM. It currently
requires at least 40 assertions; the complete scripted run reports 41.

The campaign decides how local player count is selected. `one` launches solo,
`two` requires both controls, and `choice` shows a one-player/two-player screen
before the save slots. The two-player choice cannot be confirmed until port two
is present. A two-player launch keeps authored Player 2 ownership separate and
includes opposing placements with `minimumPlayers: 2`; solo assigns the allied
company to the first control and omits those opponents. No unit stats are
adjusted.

An authored fixed formation with no deployment or inventory decision
starts its battle directly. The company editor appears only when the player can
actually change the fielded company or must repair a roster refusal. Debug
Stages remain available from the in-battle menu.

The menu is derived from the character, not from what the cursor rests on, so
a `WAIT` row can sit in it. Rows, in order: `ATTACK` (or one row per carried
weapon, in the unit type's order, the first being the weapon in hand), then
one row per ability by name, one row per carried item with its count, then
`WAIT`, `INFO`, `CANCEL`. Everything down to `WAIT` spends the activation;
`INFO` and `CANCEL` spend nothing. A strike or cast closes the menu holding
the pick: the next A says where it lands, B puts the pick down. An item
commits on the press, since it reaches the hand that holds it. A row for an
item at zero is still offered and says `x0`; the engine is what refuses it.

`INFO` opens the character's full information sheet: every stat, every carried
weapon with band and accuracy, every ability with its band, every item with
count and effect. B puts it down with the menu still standing. Every line is
composed by `grandleon::sheet` from the snapshot and the registries, so they
are the lines every client draws. It is a whole screen because ten lines of
forty characters is most of a 320-pixel display.

What the player sees:

- **Cutscenes are one line per screen**: portrait, speaker name, the line
  word-wrapped in the safe area, advanced with A. The portrait is picked from
  the generated character art by the same archetype keyword convention the
  board uses, through the compiler's `archetype_index`, with campaign titles
  aliased onto it and faction colours honoured. Portraits are CPU-decoded
  from the embedded CI4 sprites. A speech longer than the screen's rows is
  paged, never truncated, with an `A: MORE` prompt.
- **The title screen does not flicker**: static content painted once per
  display buffer, only the blinking prompt redrawn.
- **Turn hand-offs are announced** with a full-width banner, and an enemy
  activation that resolves to a wait still flashes the acting unit.
- **Refusals are on screen** in gameplay words like `OUT OF RANGE` and
  `THAT TILE IS TAKEN`, mapped from the engine's `CommandError`; only the
  engine's word, never a re-measured distinction. The table lives in
  `src/refusal_words.h` and `tests/nintendo64/console_words_test.cpp`
  compares it row by row against the shared client's copy.
- **The cursor carries an information panel**: name
  (`grandleon::sheet::character_name`'s, the one function every client asks),
  class, health, action points, band, strength, defence. Nothing drawn on the
  board carries a number; list ordinals are the terminal client's
  addressing scheme. With one of yours selected and an enemy under the
  cursor, the panel adds the engine's own forecast
  (`sim::forecast_attack`): chance when not certain, damage, health after,
  and a KO marker when lethal. What the panel shows is what `apply` rolls and
  delivers.
- **Selection lights the board**: reachable tiles glow blue, every tile an
  enemy could reach and strike is tinted red, both from the engine's own
  queries (`sim::reachable_tiles`, `sim::danger_tiles`, the latter given the
  registries through `Presenter::battle_definitions`). The tint is a
  one-pixel checker that leaves each cell's exact centre untouched, keeping
  the probes' centre-sampling classification honest.

All of that is presenter-only and compiled out of the probe builds.

The board is drawn with the RDP: the generated CI4 terrain sheets as scaled
textured rectangles, and character sprites as billboards with alpha-compare
transparency, with cursor, markers, health bars and text as CPU overlays on the
settled RDP output. Each sheet costs one TMEM upload per frame, and a 128x32
base sheet is exactly the usable CI4 TMEM. Sheets are converted from the
checked-in `n64_ci4` PNGs by the pinned toolchain's own `mksprite` at
configure time, driven by the art generator's manifest, and embedded as
bytes. Every theme's sheets are embedded, and only the chosen theme's sheets
are loaded into RDRAM. A theme is the same art recoloured, so a whole set is
about 20 KiB.

The renderer holds no table of any game's terrain names. A map cell carries a
stable content identity; the season, the terrain kind behind each identity,
the archetype each unit type wears and its colour all come out of the
package's **presentation section**, and each kind's elevation comes from the
art generator's registry (`tools/placeholder_art/assets/themes.h`). This ROM
could resolve those joins itself, since it compiles its project on the console,
but reads them from the package: a client that re-derives a rule can disagree
with the others.

The probe, `grandleon_n64_probe.z64`, samples the framebuffer and asserts
against the exact palette RGBA16 values the embedded sprites carry.
Point-sampled CI textures with dithering off write palette values verbatim.

### The character style is chosen when the ROM is built

The character art is one style's: the one the embedded project names in
`characterStyleId`, or `medieval` when it names none. `CMakeLists.txt` reads
the field from the project it is embedding and skips every other style's
assets. Sprite symbols are named `sprite_<archetype>_<colour>` from the
manifest's metadata, so `character_art[archetype][faction]` in `play_rom.cpp`
keeps its shape whichever style a game chose; adding a style to the library
costs this ROM zero bytes. Terrain themes, by contrast, are embedded whole
and chosen at run time. A theme is a recolour; a style is a second set of
drawings an order of magnitude larger. The style *resolution* is shared with
the PlayStation through `cmake/GrandleonCharacterStyle.cmake`, so two
consoles cannot disagree about which style a project names.

The style's roster is embedded twice over: the 48 standing sprites, and
beside each a **sequence sheet**: one 128x32 CI4 strip of four animation
cells (walk contact, walk pass, lunge, cast), exactly the 2,048 bytes a CI4
texture may hold in TMEM. The build refuses a ROM missing either, so every
unit it can draw standing it can draw moving. The blit uploads one cell's
sub-rectangle rather than the strip, so a drawn cell costs 512 bytes.

### Where a row of text may land

**libdragon does not clip.** `graphics_draw_character`'s only guard is a null
surface: it computes `buffer + y * stride + x` and stores, comparing nothing
against the surface's dimensions. A row drawn below scanline 239 is a write
into whatever follows the framebuffer: the second buffer, then the heap.

So every screen that walks authored strings declares a **band** in
`src/screen_text.h` (first row, row pitch, last legal scanline) and draws only
from it. The bands are `static_assert`ed against the frame at build time, and
`tests/nintendo64/screen_text_test.cpp` holds them on the host against the
ceilings the source schemas put on authored text, not against the lengths the
shipped game happens to use. A cutscene line longer than its band is paged; an
intake larger than its band names who it can and counts the rest; every
authored name in a single-row field is cut to that field's columns.

### The autopilot

The probe builds bypass every screen a person touches; the autopilot builds
(`grandleon_n64_autopilot.z64`) keep the interactive build's code paths (title
loop, cutscenes, controller cursor, action menu, banners, refusals, animations,
audio) and replace only the joypad read. A deterministic script of synthetic
input events (`src/autopilot.h` defines the vocabulary; the sequence lives
beside the campaign it drives, in `games/tarnholt/autopilot/`) plays the
opening of the Fordlight through the same `next_intent` a thumb would.

Named checkpoints print `CHECKPOINT <name>` on the ISViewer channel, assert
directly against the framebuffer (including a tile-for-tile comparison of the
highlights against `sim::reachable_tiles` and `sim::danger_tiles`), and hold
the frame for a beat; the ares harness photographs each hold into
`build-n64/ares/trail-<rom>/NNN-<name>.png`, so a run leaves a reviewable
picture story. The battle stretch can assert exact numbers because red's side
is deterministic: the session animates the first actionable red unit in
identifier order and the policy decides for it. How much of red there is
between two blue gestures is the board's turn order. The Tarnholt project
states `sideBlocks`, so a blue block is every living blue character and red
answers with a whole block of its own. A script recorded under one order is not
a script under another. If the campaign content, the turn order or red's
tactics change, re-record the script and re-derive it against the host build
before trusting the checkpoints.

### Sound and motion

Audio is a small square-wave synthesiser (`src/synth.h`) generating every
sample on the console: no audio assets, no conversion pipeline, no file
system in the ROM. Every waiting loop pumps it; a starved audio DMA loops its
last buffer. Sampled music is the upgrade path and changes no call site.

Animation is frame-counted, never timed, because checkpoints assert
framebuffer pixels. A moved token walks its route one tile at a time; a hit
flashes the target cell and knocks the struck token away from whoever swung;
a miss keeps a shorter amber flash; a defeat blinks the token out. All are
drawn from the presenter's copy of the pre-event snapshot. The route is a
breadth-first walk over the tiles `sim::reachable_tiles` returned, so every
tile the token stands on is one the engine said it could stand on. The frame
counts and the walk live in
`platform/view/include/grandleon/view/motion.hpp`, pinned on the host, so
every client animates identically. The cursor pulses on a thirty-two-frame
period; phase zero is the unpulsed cursor, and the autopilot's `checkpoint`
op puts it back to phase zero before asserting a pixel. The probe build skips
animation and audio entirely.

Boards larger than the screen scroll: a cell-grid camera follows the cursor
with a two-cell margin, clamping at the map edge. A board that fits renders
whole and never engages the camera. The camera arithmetic lives in
`src/camera.h` with no libdragon dependency, so the host suite pins its edge
behaviour (`grandleon.n64_camera`).

**The first board of the sample campaign scrolls**, which is why: a path
nothing shipped took was a path no console check exercised, and the earliest it
would have been found wanting is the day somebody's own game needed a bigger
board. The Fordlight is thirty-two columns against a sixteen-column window, so
the probe and the autopilot classify what the window holds rather than the
whole authored board. Both still count every cell they can see and both still
find both sides' units, because the crossing and the two lines sit inside the
window the board opens at; the whole-board claim is made by the desktop
renderer instead, which has a window of its own and draws all thirty-two
columns (`grandleon.sdl_presenter`).

**A board that scrolls is shown across before it is played on.** It opens at
the column showing its right edge and travels to the left, so how much board
there is arrives as a picture rather than as a surprise when the cursor walks
off the screen. Where play begins is the left edge on this machine, since the
cursor opens at a corner and the camera has not been asked to follow it yet; a
board whose play began further in would reach the left edge first and come
back, which is what `view::sweep_at` decides for both consoles. Three frames a
column, capped at ninety, and the ground only — no cursor, no highlights, no
panel. It happens on the first drawn frame of a board and no later one, so a
Stage reopened by the picker is shown again and a board redrawn mid-turn is
not. It is not interruptible: a press read inside it and then not acted on
would desync every recorded pad script. The probe build skips it with the rest
of the animation, and since no shipped board scrolls, no check draws one.

One lesson recorded so it is not relearned: libdragon routes **stderr** to
the debug channels and stdout to the video console. A ROM that prints with
`printf` and never calls `console_init` reports nothing anywhere. Newlib's
stdout buffering keeps even the hooked path silent under a buffer's worth of
output. The play ROM reports on stderr.

## The campaign ROM: a save that survives the power switch

`grandleon_n64_campaign.z64` is `play_rom.cpp` built under
`GRANDLEON_N64_CAMPAIGN`, differing from its play twin in exactly one call:
`client::run_persistent_campaign` instead of `client::run_campaign`. The six
steps a kept campaign walks (found or resume, story, the company, the board,
the battle, the commit) are `client::CampaignSession`; not one campaign number
is worked out on this machine. It is a separate ROM rather than a title-screen
mode so the probe and autopilot ROMs stay exact and keep their counts.

### The cartridge

| Device | Bytes | Holds a campaign save? |
| --- | ---: | --- |
| EEPROM 4 kbit | 512 | no, by a factor of five |
| EEPROM 16 kbit | 2,048 | no |
| **SRAM 256 kbit** | **32,768** | **yes, several times over** |
| FlashRAM 1 Mbit | 131,072 | yes, and needs a sector-erase protocol |

libdragon at the pinned commit ships EEPROM support and no SRAM API at all,
so the transfer had to be written either way. `src/sram_window.h` is that
transfer and nothing more: about forty lines. Every notion of what a slot
*is* lives in `grandleon::storage::ByteWindowSlotStorage`, which links
nothing, belongs to no console, and is proved on the host by
`tests/storage/storage_contract_test.cpp` at this cartridge's own budget.

Two decisions in `sram_window.h`:

- **The whole cartridge is shadowed in RDRAM.** `dma_read`/`dma_write` force
  the PI address into the ROM range, and SRAM is at `0x0800_0000`, outside
  it; the raw DMA pair is well-defined only for 8-byte-aligned RDRAM,
  2-byte-aligned PI addresses and even lengths. The window satisfies all of
  that once: 32 KiB of aligned `.bss`, one DMA in at boot, one DMA out per
  commit.
- **The PI's domain-2 timing registers are configured.** libdragon never sets
  them. An emulator does not need them and hardware does. They are the
  untested part of the save path.

The ROM declares `sram256k` in the Advanced Homebrew ROM Header. `n64tool`
has no flag for it; the toolchain's own `ed64romconfig` rewrites the header
afterwards, and `grandleon_package_rom` takes a save type as an optional
fourth argument so only the campaign ROMs carry one. **Without the
declaration an emulator allocates no save device, every write goes nowhere,
and the campaign is lost at the power switch with nothing on screen to say
so.**

### What a player does

`title → controls → the slot screen → the campaign`. The slot screen decides
which of the cartridge's four saves this run reads and writes, and whether it
resumes or founds. It must come first because the session takes
found-or-resume as an option before it begins.

| Slot | The row reads | A | Z |
|---|---|---|---|
| holds a campaign | `SLOT 1  A COMPANY STANDS` | continue it | arm "start over" |
| holds nothing | `SLOT 2  -- EMPTY --` | found here | found here |
| armed | `SLOT 1  START OVER?` | put it back | found over it |

CONTINUE is offered per row, not per cartridge. Writing over a held slot takes
two presses of Z. Any other press disarms it, moving the caret included. Slot
one is `tarnholt`, the unnumbered name; slots two to four are `tarnholt-2`
through `-4`. The caret opens on the first row. The screen is
`grandleon::view::SlotMenu` in `platform/view`, rendered by the shared client
as well. The model writes the button letter into the sentence, so no renderer
substitutes letters into finished text.

Between battles the **management stage** opens, in the unit-action-menu
vocabulary. A caret sits over the company, and A opens a member's menu: take
the field or sit out, one GIVE row per thing the store holds, one TAKE row per
thing carried, CANCEL. START opens the board. B leaves nothing behind: every
gesture commits and writes the cartridge as it is made.

**The Stage picker**, on a ROM built with `GRANDLEON_STAGE_PICKER`. It is
a testing aid, and reaching a late Stage on real hardware to look at one thing
in it should not cost playing every Stage before it. START over a battle opens
the board menu, which grows a fourth row, `GO TO ANOTHER STAGE`, under the way
out; Z on the management stage opens the same screen, named in that screen's
footer whenever it does anything. Both are built only when the session handed
this ROM a list of Stages, which it does only in such a build, so neither reads
the define and an ordinary ROM has neither.

The same debug build shows live FPS at the lower right. It continuously redraws
the board while the player is looking at it, making the number a worst-case
presented-frame cadence. Ordinary ROMs keep the cheaper settled-frame reuse
and carry neither the counter nor its extra redraw work.

The screen lists the Stages under the author's own names, marks the one the
campaign is standing on `HERE` and every one it has stood on `SEEN`, and says
under its heading that a Stage with neither mark may not open. That is the
honest half: a jump moves the campaign and changes nothing else, so a Stage
reached this way has not recorded the objectives, set the flags or gained the
characters the ordinary route would have. Tarnholt is the worked example — its
last board carries "keep Captain Mirea alive", and Mirea joins at a cutscene
after the first battle, so jumping straight there makes the roster refuse the
board. That refusal sends the player to the management stage, which is why the
picker is reachable from there too: nothing a player can do on that screen
recruits anybody, and the jump has already written the cartridge.

The company scrolls: a screen holds seven roster rows, a campaign may have
five hundred and twelve members, so the roster is a window:
`grandleon::view::ListWindow`, `view::Camera` in one dimension, one-row margin,
clamped. The store gets its own four-row window whose top never moves, and a
member's verb menu an eight-row window over up to twenty. A window that is not
scrolling draws the whole list plain; one that is says `4-10 OF 12` beside it.
`tests/client/company_scroll_test.cpp` walks a caret over a twelve-member
company through the real client, and `tests/view/list_view_test.cpp` pins the
window arithmetic with no emulator in the loop.

Where the next encounter authors a `deployment.capacity`, the stage says so
on one line, `FIELDED 2 OF 2`, and the member menu's row reads
`TAKE THE FIELD (FULL)` rather than disappearing. Pressing it commits
nothing; the refusal banner carries
`campaign_runtime::roster_error_name(RosterError::over_deployment_capacity)`,
the engine's sentence. The count is an early copy of
`join_campaign_roster`'s, never a substitute: the engine refuses an over-cap
company however this screen counted.

The aftermath sheet is the battle's effect on the company, read out of
`campaign_runtime::BattleProgression` and the committed roster, with nothing
added up here.

### The proof, which is a restart

`platform/nintendo64/ares/run-ares-persistence.sh`
(`grandleon_n64_campaign_check`) boots the campaign autopilot ROM **twice, in
two ares processes, over one save file**: the first founds the campaign,
plays the management gestures, and reports `CAMPAIGN FOUNDED`; the process is
killed, the 32,768-byte `.ram` file stays; a second process boots the same
ROM over the same file, resumes, checks the roster, kits, availability and
store against host-derived expectations, and reports `CAMPAIGN RESUMED`. The
ROM is the same binary both times and carries no flag saying which run it is:
what it does is decided by what the cartridge holds, which is the property
under test. A cartridge that did not persist makes the second run say
FOUNDED, and the check fails on the word.

Every number the ROM asserts lives in
`games/tarnholt/autopilot/campaign_expectations.h` and is derived by
`tests/nintendo64/campaign_expectations_test.cpp`, which links the real
engine, compiles the same project, and drives the same session through the
same gestures on the host (through the same `ByteWindowSlotStorage` over a
cartridge-sized window) before the ROM is built.

Two facts found by running it:

- **A PI DMA to a region no cartridge answers on is not an error; it is
  nothing at all**, and RDRAM keeps what it held. A read-back check would
  "pass" against the very shadow it was about to compare with.
  `SramWindow::reload` scribbles the shadow with `0x5A` first, which makes a
  silent no-op look like the empty device it is.
- **ares flushes cartridge memory on a timer, not on a signal.** A run killed
  one second after the verdict leaves nothing on disk.
  `General/AutoSaveMemory` is on and the harness lingers 75 seconds past the
  verdict.

### The stack is measured, not budgeted

libdragon puts the stack at the top of RDRAM growing down and newlib's heap
grows up from the end of `.bss`, so there is no fixed allowance to overrun:
four megabytes and no natural bound. `main` paints a window immediately below
its own frame before anything is allocated and counts back from the far end to
the first byte still holding the pattern: exact to a byte and monotonic. Its
one blind spot, a frame allocated and never written, is an underestimate rather
than a false pass. The checked build fails if a kilobyte of the window is ever
gone. The cartridge's 32 KiB shadow is `static` rather than a local, which
would have put it in `main`'s frame and left the watermark measuring a smaller
stack than the ROM stands on.
