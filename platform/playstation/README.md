# PlayStation

Seven executables. One proves the engine runs on an R3000A, one proves the
memory card, two draw a board, one is a board a controller plays, and two are
whole games. The interactive half is not written here:
`platform/client/src/turn_client.cpp` is the same translation unit the host
derivation compiles, and this directory supplies a GPU, a controller, a memory
card, a frame, and the pixel claims that make a picture checkable.

## Contents

- [What is here](#what-is-here)
- [Building and running](#building-and-running)
- [The disc](#the-disc)
- [The toolchain](#the-toolchain)
- [The ISA trap](#the-isa-trap)
- [No exceptions, and what that costs the content path](#no-exceptions-and-what-that-costs-the-content-path)
- [The runtime seam](#the-runtime-seam)
- [The emulator](#the-emulator)
- [The board client](#the-board-client)
- [The campaign and the card](#the-campaign-and-the-card)
- [The renderer](#the-renderer)
- [How a run is checked](#how-a-run-is-checked)
- [What is verified, and what is not](#what-is-verified-and-what-is-not)
- [How a board is fitted to the screen](#how-a-board-is-fitted-to-the-screen)
- [Still owed](#still-owed)
- [Alignment](#alignment)
- [Deliberately not here](#deliberately-not-here)

## What is here

| executable | what it proves |
|---|---|
| `grandleon_psx.ps-exe` | conformance: the engine's hashes reproduce on the R3000A, headless, no renderer |
| `grandleon_psx_play.ps-exe` | the renderer, on the Fordlight Crossing (level ground) |
| `grandleon_psx_play_raised.ps-exe` | the renderer, on the Ashen Watch (raised ground) |
| `grandleon_psx_turn.ps-exe` | a battle played from a controller: cursor, selection, reach and threat tiles, moves, priced strikes with forecasts, refusals in the engine's words, the action menu, the information sheet, `tactics::decide` on the other side |
| `grandleon_psx_card.ps-exe` | the memory card protocol, including refusing a card it cannot read |
| `grandleon_psx_campaign.ps-exe` | the Tarnholt Line campaign, founded and resumed across processes |
| `grandleon_psx_campaign_demo.ps-exe` | The Bridge at Dawn campaign, likewise |
| `grandleon_psx_turn_autopilot.ps-exe` | the turn executable, playing a recorded script instead of waiting on a pad |
| `grandleon_psx_campaign_autopilot.ps-exe` | the Tarnholt Line campaign, likewise |
| `grandleon_psx_campaign_demo_autopilot.ps-exe` | The Bridge at Dawn campaign, likewise |

Each check prints `RESULT PASS n/n` counts; the gate is the authority on the
numbers. The Tarnholt Line campaign is also written onto a disc image somebody
can burn; see [The disc](#the-disc).

### Played and autopilot are the same code

The last three are `src/turn_exe.cpp` again with `GRANDLEON_PSX_AUTOPILOT`
defined, and that macro decides one thing: whether a recorded script is linked
in. Without it there is no script in the image at all and every screen waits on
the controller, which is what makes the first set a game. With it the executable
paces itself by counting frames and reports what it saw, which is what makes the
second set checkable on an emulator that offers no pad ports.

**The checks run the autopilot builds and the disc carries a played one.** A
check pointed at a played build would not fail, it would hang, which is the
worst way for a check to be wrong; `run-playstation-turn.sh` says so where it
picks the name.

Deciding this at build time rather than by asking the machine at run time is
deliberate. A run-time test — is a pad plugged in, is this an emulator — would
have to be right on hardware nobody here can try, and would leave the shipped
executable carrying a script it must never reach.

## Building and running

```sh
cmake --build build --target grandleon_playstation                # build the executables
cmake --build build --target grandleon_playstation_check          # conformance
cmake --build build --target grandleon_playstation_render_check   # the pixels
cmake --build build --target grandleon_playstation_turn_check     # the played battle
cmake --build build --target grandleon_playstation_card_check     # the card
cmake --build build --target grandleon_playstation_campaign_check # the campaigns
cmake --build build --target grandleon_playstation_disc           # a burnable disc
cmake --build build --target grandleon_playstation_disc_check     # the disc, booted
cmake --build build --target grandleon_playstation_showcase       # the turn check, filmed
```

Run these from the repository root. Artifacts land in the gitignored
`build-playstation/`, including a portable pixmap of each frame the render
check sampled. `-DGRANDLEON_PLAYSTATION_TESTS=ON` adds the runs to `ctest` as
`grandleon.playstation`, `grandleon.playstation_render`,
`grandleon.playstation_turn`, `grandleon.playstation_card` and
`grandleon.playstation_campaign`; they are off by default because they pull a
cross toolchain image and build an emulator from source on first use. Every
target is excluded from `ALL`. The scripts under `scripts/` can also be run
directly and take `--rebuild-image` to force a container rebuild.

`grandleon_playstation_showcase` is the turn check's own run with a second
camera on: it still compares every transcript line and every claimed pixel and
still must reach `RESULT PASS`. It adds `build-playstation/showcase/`: one file
per vertical retrace between two checkpoints. Those become the repository
README's film through `scripts/readme-screenshots.sh psx-showcase`.
`GRANDLEON_PLAYSTATION_FILM_FROM` and `GRANDLEON_PLAYSTATION_FILM_TO` on
`scripts/run-playstation-turn.sh` are the whole interface; with neither set the
camera never runs. The camera writes from a `GPU::Vsync` listener and touches
none of the counters the verdict is computed from.

## The disc

A `.ps-exe` is not something a person can use. An emulator will take one only if
told two flags nobody guesses; PCSX-Redux needs `-bios` and `-loadexe`. ares'
PlayStation core boots CD images only. So the build also writes a disc.

`grandleon_playstation_disc` produces `build-playstation/disc/grandleon.bin`
and `grandleon.cue`. Both, and not just the bin: the bin is 2352-byte Mode 2
Form 1 sectors with no table of contents in them, and the cue is the table of
contents, so burning software handed the bin alone has nothing to tell it where
the track starts or what kind of track it is.

**The image carries no licence sector.** A retail disc holds Sony's licence
data in the sixteen sectors ahead of the ISO 9660 volume descriptor; that data
is Sony's, none of it is fetched or vendored here, and those sectors are
written as zeroes. `scripts/build-disc.sh` reads them back and fails if any
byte of them is not zero, so it is a checked property rather than a promise. It
first points the same two scans at a copy of the licence area with a licence
written into it and requires them to catch it, because a scan that has never
been seen to fail says nothing.

The consequences, and nothing beyond them:

- **PCSX-Redux boots it.** `grandleon_playstation_disc_check` puts the cue in
  the drive of the pinned emulator, with no `-loadexe` anywhere, and OpenBIOS
  reads `SYSTEM.CNF`, loads `MAIN.EXE` and runs it. It boots the disc twice over
  one card: founding the campaign, then picking it up in a second emulator
  process. Each boot is compared against the same host-derived transcript
  `grandleon_playstation_campaign_check` compares the injected run against, line
  for line and pixel for pixel. A disc and a memory card meet nowhere else here,
  and they do not interfere: the resuming boot reaches screens the founding
  boot's expectations do not contain, and the card comes out byte for byte what
  the first boot wrote.
- **A stock PlayStation will refuse it.** The console's boot ROM reads the
  licence area, finds nothing there, and stops.
- **Nothing here has ever run on real hardware**, on either console, so no
  claim is made about any console that does accept the disc.

One disc boots one game: `SYSTEM.CNF` names one executable and there is no menu
here to choose between two, so the disc carries the campaign, the thing a person
plays. `GRANDLEON_PLAYSTATION_DISC_EXECUTABLE` names a different one for
anybody who wants a disc of it. Whichever it is, it is called `MAIN.EXE` on the
image, which is what lets `disc/SYSTEM.CNF` be a fixed file that goes onto the
disc byte for byte rather than a template.

The game reads nothing off the disc after boot. Every package byte, every
texel and the whole controller script are inside the executable, so the only
transfer from the drive is the BIOS shell's own load of `MAIN.EXE`, about
700 KB, once, before `main`. No code path in this directory has ever waited on
a drive because none of them touches one.

## The toolchain

| | |
|---|---|
| SDK | Nugget, MIT, pinned by commit in `Containerfile` |
| Compiler | Debian's `mipsel-linux-gnu-g++` 12.3.0, binutils 2.42 |
| Disc writer | mkpsxiso, GPL-2.0, pinned by commit in `Containerfile` |
| Image | `ghcr.io/grumpycoders/pcsx-redux-build`, pinned by digest in `Containerfile` |

The image is PCSX-Redux's own build image and carries Debian's
`g++-mipsel-linux-gnu` with a hosted libstdc++ for this ABI. `Containerfile`
adds CMake and Nugget. PSn00bSDK is not usable: it is configured
`--disable-hosted-libstdcxx --without-headers`, ships no `libstdc++.a`, and
`#include <set>` fails outright.

Two pins to know about:

- **`-mfp32`, not `-msoft-float`.** `-msoft-float` selects the o32-soft ABI
  variant; Debian ships only the o32-hard variant's headers, so `#include
  <set>` reaches `gnu/stubs.h`, which asks for a `gnu/stubs-o32_soft.h` that is
  not in the package, and every C++ header fails. `-mfp32` gives
  `-march=mips1` the register width it demands without moving the ABI. No
  floating point is generated either way.
- **libstdc++'s `tree.cc` is compiled from source**, fetched by the toolchain
  image at the GCC 12.3.0 release tag and pinned by the SHA-256 of the file
  itself (a tag can move). It is not checked in. The prebuilt `tree.o` links but
  is compiled for the wrong ISA; see below.

## The ISA trap

The toolchain image's `libgcc.a` and `libstdc++.a` each have exactly one
multilib, built for `mips32r2`, not MIPS-I. `libgcc`'s `__clzsi2` is a single
`clz` and `__umoddi3` uses `clz` and `mul`; libstdc++'s prebuilt `tree.o` has a
`movn` inside `_Rb_tree_increment`. The R3000A implements none of `clz`, `mul`
or `movn`, and the failure is not a crash. The CPU takes a Reserved
Instruction exception, the BIOS handler returns, the destination register still
holds whatever it held before, and the caller proceeds with a wrong answer.
Under PCSX-Redux the only sign is a single `Encountered reserved opcode` line
in the log.

So the link passes no `-lgcc`; `src/psx_arith.c` implements `__clzsi2`,
`__ashldi3`, `__lshrdi3`, `__ashrdi3`, `__udivdi3` and `__umoddi3` for MIPS-I;
`tree.cc` is compiled with the same `-march=mips1` as everything else; and
`scripts/check-mips1.sh` runs as a post-build step and fails unless the linked
executable's ELF header says `mips1`. The header check works because GNU ld
merges the ISA level of every input object into `e_flags` by taking the highest,
so one bad object anywhere raises the whole image. The header cannot be fooled
the way a denylist of mnemonics can.

## No exceptions, and what that costs the content path

`tools/game_content`'s JSON parser reports a malformed document by throwing, the
only `throw` and `catch` in the portable code this repository runs on a console.
The content path therefore needs a working unwinder, and the pinned toolchain
cannot link one. Debian builds this cross libstdc++ for a hosted, dynamically
linked userland: its exception-handling objects carry
`R_MIPS_GOT16`/`R_MIPS_CALL16` relocations against `_gp_disp`, which a
`-mno-abicalls` link (a PS-EXE has no dynamic loader and the BIOS does not set
up `$gp`) reports as "relocation truncated to fit". Underneath the relocations
they need glibc (`pthread_mutex_lock`, `__tls_get_addr`, `gettext`,
`__stack_chk_fail`).

So `-fno-exceptions` and `-fno-rtti` are toolchain-wide, and
`grandleon::game_content` is not linked. Packages are compiled on the host by
`grandleon_content_compile` during the build and the bytes are embedded; the
console loads them through `package_format`, `encounter_loader` and
`campaign`. Only the parser stays on the host. The repository's only
target-side test of the JSON path is the Nintendo 64's.

## The runtime seam

`src/psx_runtime.cpp` and `src/psx_arith.c` are everything the R3000A needed
that the engine does not carry. Nothing in `engine/` was changed.

- **No SDK headers are on the include path.** Nugget's headers are C with
  inline assembly and cannot be held to `-Wpedantic -Wconversion -Werror`. The
  three entry points used (the BIOS `putchar` B0 call, the control port, and
  root counter 2) are declared by hand, and no engine translation unit ever
  sees a Nugget header.
- **`abort()` is replaced.** Nugget's calls `pcsx_debugbreak()` before
  `pcsx_exit(-1)`, and the break pauses the machine before the exit store
  executes. Under a script it hangs rather than fails. GCC routes every
  unhandled C++ ABI failure through `abort()`. Nugget declares it weak so it
  can be replaced, and it is.
- **`operator new`/`delete` wire to a first-fit allocator** over the machine's
  free RAM, `__bss_end` to the stack, with eight-byte blocks, boundary tags for
  O(1) coalescing, and an explicit free list. The nothrow forms are supplied
  too, because `std::get_temporary_buffer` reaches for them, and therefore so
  does every `stable_sort` in `engine/package_runtime`.
- **`std::nothrow` is defined here.** It is an object, and libstdc++ has it in
  an `-mabicalls` object. This is the one name the target adds to namespace
  `std`.
- **GCC 12 emits `__throw_bad_alloc` and friends even under
  `-fno-exceptions`.** libstdc++ has them in `functexcept.o`, which calls
  `gettext`. Here they are panics that print and exit non-zero.
- **Three explicit instantiation definitions close the `std::string` chain.**
  libstdc++ declares `extern template class basic_string<char>` and keeps
  `_M_replace`/`_M_mutate`/`_M_create` out of line in an object this link
  cannot use. If a second such chain turns up, fetch and pin `string-inst.cc`
  in `Containerfile` the way `tree.cc` is, rather than adding definitions one
  by one.
- **`memcpy`, `memmove`, `memset`, `memcmp` come from Nugget**; `isdigit` and
  `isspace` are supplied. The engine calls none of them directly; libstdc++'s
  vector and deque relocation and `string_view` comparison do.
- **Nugget's crt0 walks the constructor list** and finishes with
  `pcsx_exit(main(argc, argv))`, so returning from `main` sets the emulator's
  exit code.

## The emulator

PCSX-Redux (GPL-2.0) with OpenBIOS, a reverse-engineered MIT BIOS living in the
same repository and built from the same pinned commit, so no copyrighted
firmware is involved. ares, this repository's authoritative Nintendo 64
emulator, marks its own PlayStation core Experimental, which is why its
authority stops at that console.

Invocation facts that are not discoverable and mostly fail silently:

- **`-run` is mandatory.** The executable is injected by hijacking the BIOS
  shell when the CPU reaches `0x80030000`; without `-run` the CPU never starts
  and the emulator just sits.
- **`-bios` is mandatory.** With no BIOS there is no shell to hijack.
- **`-testmode` makes the exit code real.** Without it, an exit write pauses
  the emulator instead, which in a script is a hang.
- **`-no-ui` implicitly enables stdout** (so `-stdout` is not also passed) and
  selects a no-op text UI with no GLFW and no OpenGL: no Xvfb, no software
  Vulkan, no window-focus deadlock.
- **The Lua flag is `-dofile`.** There is no `-lua`, `-luafile`, `-dumpvram`
  or `-screenshot`.
- **`pcsx-redux` has no `--help`.** It tries to initialise GLFW and exits 255
  printing nothing; the source is the flag list.
- **`-softgpu` is passed explicitly** although it is the default: the
  screenshot API exists only on the software rasteriser.
- **There is no cycle limit or timeout flag.** The run scripts impose a
  wall-clock bound from outside.

The gate reads the verdict from both the exit code and the text report and
fails if they disagree: an exit code of zero with no report is a broken
channel, not a pass.

## The board client

### Buttons

| client | PlayStation | what it does |
|---|---|---|
| d-pad | d-pad | the cursor, and a menu caret |
| `pad_a` | **cross** | confirm: pick up, put down, take a menu row, aim a strike |
| `pad_b` | **circle** | back out: drop a selection, close the menu, abandon an aim |
| `pad_c` | **triangle** | open the unit action menu |
| `pad_start` | **start** | open the battle from arranging, end an activation |

Cross confirms and circle cancels (the Western convention). Square, select and
the shoulders are read and deliberately dropped. The names are the client's
(`grandleon/client/turn_client.hpp`); each console maps its own buttons onto
the same three, so a controller script means the same thing on every console.

### Screen

320x240, a 32-pixel cell, a four-row message bar in an eight-pixel font. The
board window is 10 columns by 6 rows: 320/32 is ten; 240 less a 32-pixel bar is
208, which holds six rows with sixteen lines over, and those sixteen are the
headroom a raised cell lifts into. The pair is derived here from the hardware
and `static_assert`ed against the client's `viewport_cols`/`viewport_rows`,
because the camera scrolls at the window's edge and a derivation against a
different window would put the camera somewhere the console never puts it.

The information sheet takes the whole screen: `grandleon::sheet` composes
forty columns and this display is forty columns wide in an eight-pixel font.
GP1(0x08)'s 512-pixel horizontal mode is not taken: it would move every pixel
expectation and make this the one console whose sheet is not the others'.

### Font

The console has no font. The sixty-four glyphs are
`grandleon/view/glyphs.hpp`'s, expanded at boot into a 4bpp sheet (32 glyphs
across, 2 down, 8 texels square) at VRAM (320, 0). That corner is free: a
texture page with Y base zero overlaps the framebuffer for its first 320
halfwords, so pages there were never usable as pages, and the rectangle right
of the framebuffer above the CLUT band is claimed by nothing. The font's CLUT
maps index 0 to paper and 1 to ink; paper is opaque and deliberately not the
all-zero halfword, because the GPU skips a texel whose CLUT word is zero and a
message bar that let the board show through between letters would be
unreadable over a mountain.

### Controller

SIO0, bit-banged, port one, five bytes a poll. Nugget's `psyqo` is inline
assembly throughout and cannot be held to this repository's warnings, so there
was nothing to reuse. The acknowledgement budget is 128 spins
(`src/psx_pad.cpp`): PCSX-Redux delivers each byte without ever raising the
acknowledgement level, so a generous budget is not a safety margin, it is
timeouts burned every poll. The pad is not read from a vertical-retrace
handler: this executable installs no interrupt handler at all, the frame has
milliseconds spare, and the client blocks on a press rather than sampling one.

### Frame

A frame is waited for, not assumed: `psx_runtime.cpp` polls bit 0 of I_STAT,
which the hardware raises once a display frame whether or not the interrupt is
unmasked, bounded by a 150,000-tick budget on root counter 2 so a machine
whose I_STAT somebody else is acknowledging gets a timer-paced loop rather
than a hang. Motion is a whole repaint: no sprite table, no plane, so moving a
primitive means drawing the picture again. Repaints fit comfortably in the
16,667 µs frame budget. Slide, hit and settle timing is the shared client's,
counted in frames by `platform/view/include/grandleon/view/motion.hpp`.

Reachable and threatened tiles are drawn as a three-pixel opaque frame inside
the cell, not a translucent wash: the machine has a blender, and using it
would make every probe over a lit tile a claim about the blender's rounding
rather than about the picture. The frame is drawn inside the sorted draw-list
walk, immediately after its own cell, so the computed depth order is the
painted one.

### The script is compiled in

PCSX-Redux headless offers no pad ports, so the autopilot script is compiled
into the executable and paced by counting frames, as on the Nintendo 64. The
live pad is polled every frame and wins: left alone the executable plays the
script and reports; pressed, it hands over and never goes back. One artifact
demonstrates itself, is a game to anybody holding a controller, and checks
exactly the scripted run when headless.

## The campaign and the card

`grandleon_psx_campaign.ps-exe` is `src/turn_exe.cpp` compiled a second time
with `GRANDLEON_TURN_CLIENT_CAMPAIGN` defined. Behind that macro the shared
client keeps the title, the slot screen, the company, the authored story, the
aftermath and the end. Every screen is composed by the client as a page of 38
columns by 16 rows and placed here by arithmetic: a panel of paper,
rows inset by the caret's column, a footer at the foot. A scene's backdrop is
six `gpu::fill` rectangles: the art library publishes flat bands, not a
picture.

The campaign is kept on a memory card. `src/psx_card.h` speaks the card
protocol on SIO0 directly, the way `src/psx_pad.cpp` speaks the controller's;
above it the campaign sees `storage::ByteWindowSlotStorage`, the same device
the Nintendo 64 saves into, proved on the host by
`tests/storage/storage_contract_test.cpp`. The region is one real card file of
two blocks (15,968 bytes usable after the slot directory) with a directory
entry, a title and an icon, so a card manager can see, copy and delete it.
Which blocks it occupies is the card's answer: the file's own chain is
followed when present, free blocks taken when not, and a card whose directory
this build does not understand is refused rather than formatted.

`grandleon_playstation_campaign_check` plays each shipped campaign twice, in
two emulator processes over one card image: founding on an empty card, then
resuming. Which script the autopilot plays is decided by what the card holds
and nothing the executable carries, so the resuming pass reaches screens the
founding pass's expectations do not contain, and a card that forgot fails on
the first line. `grandleon_playstation_card_check` is stricter. Five processes
run over four images: a card this game cannot read, a blank one it writes and
one it resumes from, a card whose block chain runs into another game's file, and
a card whose only free block has a damaged directory entry. Each of the three
refusals must happen **without writing a frame**, checked by digesting the
image before and after.

## The renderer

`grandleon::view` is the arithmetic every client shares: camera,
elevation-offset projection, autotile convention, depth-ordered draw list.
This renderer consumes all of it and re-derives none of it. What is left is
small:

- **Placing a cell:** one textured rectangle, positioned per pixel.
- **The origin:** the projection's own, unrounded. Nowhere does this renderer
  choose rather than compute.
- **Palettes:** a CLUT named in every draw packet; no banks.
- **Occlusion:** the order of the draws, which is the order of the list.
- **Raised ground:** the same code path as level ground, at a different `y`.
  240 lines hold seven 32-pixel rows with sixteen to spare, and the deepest
  lift the projection offers is `max_lift_for(32) = 12`, so the full headroom
  is reserved and nothing is clamped; the executable checks the clamp rather
  than assuming it does not fire. The band below a raised cell is backdrop, as
  on every client: draw order is elevation-ascending and nobody paints the
  `lift` rows between.
- **Autotiling:** the four interior variants, chosen by `(x + y * 3) % 4`, as
  on the Nintendo 64. The 47-variant blob sheets are not embedded.

### VRAM

VRAM is a 1024x512 array of halfwords and the display is a window into it.
The framebuffer sits at the origin and stays there, single-buffered, because
nothing here animates between checks. So a screen coordinate is a VRAM
coordinate, which lets two independent questions be asked about the same
address: what the executable read back, and what the emulator cropped out of
the display.

```
Y 0..239    X 0..319     the framebuffer, and the display
Y 240..255  X 0..1023    CLUTs, sixteen halfwords each: 64 to a row, 1,024 in all
Y 256..511  X 320..1023  eleven 4bpp texture pages, 64 cells each: 704 cells
```

A texture page is fixed by the hardware at 64 halfwords by 256 lines with its
X base a multiple of 64, so pages 0 to 4 overlap the framebuffer and are not
used. `psx_gpu.cpp` asserts at compile time that the regions cannot overlap,
because getting it wrong would draw a correct picture into a texture page and
say nothing.

### The art: no re-quantisation, one repacking

Every colour in the art profile is a master palette entry, and the master
palette's entries map injectively to 15-bit triples: rounding to five bits
per channel merges nothing, which `verify.check_playstation` asserts. Because
the map is injective and master entry 0 is the transparent one, entry 0 is the
only colour that becomes the all-zero halfword the GPU skips: an opaque colour
can never silently become a hole in the board. No new art profile and no new
`palette_mode` exist for this console.

Two things about the `n64_ci4` assets are not what this hardware reads, and
both are mechanical repacks that lose nothing:

- **Nibble order is reversed.** A 4bpp PNG (and the Nintendo 64's CI4) puts
  the left pixel of a pair in the high nibble; the PlayStation puts it in the
  low one.
- **The palette is not a CLUT.** A PNG carries 8-bit `PLTE` triples plus
  `tRNS`; a PlayStation CLUT entry is a little-endian halfword
  `SBBBBBGG GGGRRRRR`: red lowest, top bit a semi-transparency flag. The
  Nintendo 64's TLUT is `RRRRRGGG GGBBBBBA`, which is neither.

`playstation_header.py` does both at generation time into
`assets/playstation.h`, from the same `Converted` objects the `n64_ci4` PNGs
are written from, so header and PNGs cannot disagree. A header rather than
PNGs because the toolchain image has no PNG decoder. Index 0 is **not**
reserved for transparency here: this renderer decides transparency by the
colour a texel resolves to, never by its index, which is what the hardware
does. Generation-time checks hold every asset to a sixteen-entry CLUT, require
transparency (where present) at slot 0, and round-trip the packing.

The character style is resolved at build time: the art library emits one
character header per style, `CMakeLists.txt` resolves the project's
`characterStyleId` and includes exactly one, and `art::character` has no style
parameter. Within the embedded style, the archetype and faction-colour
dimensions are still pointer tables and cells a board never stands on are
still linked. A board that could scroll would want a different subset, so no
further trimming is chosen.

## How a run is checked

The order is the argument:

1. **`grandleon_playstation_expect`, on the host, before the executable is
   built.** It compiles the same `turn_client.cpp` the R3000A compiles, drives
   it through the same `client::run_campaign` over the same board with the
   same viewport, replays the same
   `platform/client/autopilot/fordlight_pad.h`, and writes the
   `CHECKPOINT`/`FACT` transcript out. It compiles
   `games/tarnholt/source/project.json` itself, so it depends on no console
   build and can be run, and disbelieved, on its own.
2. **The executable is built.**
3. **The run.** A byte written to `0x1f802081` makes PCSX-Redux call
   `PCSX.execSlots[n]` from inside the store instruction and resume the CPU
   when it returns. The executable names the instant, nothing waits a guessed
   number of frames, and there is no race. (`PCSX.nextTick` is a trap: the
   emulator clears the global after calling it, so a callback that
   re-registers itself fires exactly once.) The Lua harness photographs the
   composited display at that instant (execSlot 14 a checkpoint, execSlot 15
   the verdict) and reads the GPU's display registers out of a save state at
   the first capture, because GP1 is write-only and an executable that draws
   correctly but leaves the display disabled produces a byte-identical
   screenshot: `takeScreenShot` crops VRAM without consulting the
   display-enable flag, so a pixel check alone is not evidence anything is on
   screen. The save-state shadow is the one copy the guest cannot forge.
4. **The joiner** (`harness/playstation_probe.c`,
   `harness/playstation_turn_probe.c`) links nothing from this repository,
   never reads the package, never sees the art, and knows nothing about the
   board, so it cannot agree with the executable by sharing a mistake. It
   requires the console's transcript to match the host's derivation line for
   line, and three quantities to meet at every claimed pixel:

| | computed by |
|---|---|
| `claim` | the executable's arithmetic over the projection and the CLUT |
| `readback` | the GPU's rasteriser and CLUT unit, fetched through `GP0(0xC0)` |
| `frame` | the emulator's display window over that same VRAM |

`claim == readback` catches a texture on the wrong page, a wrongly addressed
CLUT, a rectangle at wrong coordinates, a reversed nibble order, and a
missorted draw list. `readback == frame` catches a picture drawn somewhere the
display does not look. The display-register checks catch a display switched
off. There is no tolerance anywhere: the machine stores five bits per channel
and the frame is those bits verbatim.

The harness also makes its own checks before believing a word the executable
says: display enabled, display window as asked, frame exactly the implied size,
every pixel a colour this art can produce (the semi-transparency bit is clear in
every generated CLUT entry), and a minimum colour spread. The joiner checks the
report before the pixels: distinct probe labels, every readback naming a probe,
printed counts matching claimed counts. A log that lost lines is a broken
channel, not a picture.

Each run writes the sampled frame beside its log as a portable pixmap and
prints an FNV-1a digest. It is a fingerprint for comparing two runs,
deliberately not a pinned golden: the per-cell claims already say *where* a
picture went wrong, which a digest cannot.

A harness whose checks a blank screen would also pass is not evidence, so the
render check points the same harness at the **conformance executable**, which
never touches the GPU at all, and requires it to fail. It does, on **10 of the
18** checks: the display is disabled, the display window was never set, the
frame is 0x0, and both colour properties fail. That runs on every
invocation rather than sitting in this file as a claim, and a run that cannot
find the control executable fails: a harness nobody has watched fail is a
harness nobody knows the meaning of.

The play executables are held to the count in the harness's verdict as well as
to the word in it, because `HARNESS RESULT PASS 0/0` carries the word and
measures nothing. The count is **18**, the size of the harness's table, which is
the same 18 the control is measured against.
[scripts/assert-harness-verdict.sh](../../scripts/assert-harness-verdict.sh)
makes that decision for every check on both consoles, and each of this console's
six run scripts proves it can still fail before it starts an emulator.

## What is verified, and what is not

Verified, by running it: the engine libraries compile for the R3000A under full
warnings with no `engine/` source changes; they link into executables that boot;
the engine reproduces its hashes on MIPS-I; the board the renderer drew
matches per-cell claims derived on the host; a controller's turn matches a
transcript derived on the host before the executable existed; the campaign
survives being written to a memory card and read back in another process; both
shipped campaigns play end to end; the disc image boots through the BIOS shell
and plays the campaign off itself.

Not verified, and not claimed:

- **Nothing has run on real hardware.** Every result comes from PCSX-Redux.
- **No performance number is claimed** for this target.
- **The card is verified under one emulator, and only under emulation.** The
  checks restart the emulator process to prove the bytes outlive it, but nothing
  has written to a real card.
- **No console is claimed to accept the disc.** A stock PlayStation will refuse
  it, for the reason above. What any other console does with it is not
  something this repository has tried, so it is not something this repository
  says.
- **The load is an emulated load.** A drive here delivers `MAIN.EXE` as fast as
  the host can read a file; a real one is a 2x CD reader, and how long the
  black screen before the title lasts on one is a number nobody here has.

## How a board is fitted to the screen

A cell is drawn as large as the board allows, up to the thirty-two pixels the
art is drawn at, and never smaller than sixteen. Below that the board scrolls
instead. `turn::board_fit` states those four numbers, `view::fit_board` applies
them, and the Nintendo 64 asks the same function with its own four — so the two
consoles share the rule and not a copy of it.

The consequence worth stating: every map the Tarnholt Line ships is larger than
ten by six, so before this every one of them was played through a window. All
of them now render whole, at cells between twenty-three and twenty-six pixels.
Twenty by thirteen is the largest board that fits; past that a cell would be
smaller than half the art and the camera scrolls.

**Sixteen is half of thirty-two on purpose.** This GPU has no filter, so a cell
drawn smaller drops texels rather than blending them, and half is the one
reduction that drops every other texel evenly. A board drawn at the native size
is unchanged, down to the primitive: `draw_cell_scaled` hands that case to
`draw_cell`.

Shrinking needs a different primitive, and that is why it was owed for so long.
`gpu::draw_cell` issues GP0(0x65), a textured *rectangle*, and this GPU samples
a rectangle's texture one texel to one pixel — so a smaller rectangle crops the
sprite rather than shrinking it. `gpu::draw_cell_scaled` issues GP0(0x2D)
instead, a textured quad whose four vertices carry their own texture
coordinates, which the hardware interpolates between.

### What a pixel claim has to know

A probe claims that one pixel holds one colour, and on a shrunk cell it can no
longer assume that pixel is one texel. It asks which texel a *pixel* shows,
never where a texel lands: thirty-two texels over twenty-six pixels means six
texels are drawn nowhere at all, so the inverse question has no answer for
them and inventing one claims a neighbour's colour.

The forward answer is `((2 * screen + 1) * 31) / (2 * drawn)`, and both halves
of it were got wrong once here before the hardware was asked: the quad carries
thirty-one texels of span rather than thirty-two, and the GPU samples at a
pixel's centre rather than its near edge. Dropping either reads a texel early
over most of the cell.

## Still owed

Nothing on the list this section used to carry.

## Alignment

MIPS-I has no 64-bit load or store: `LD`, `SD`, `DADDIU` and `DMULT` do not
exist. A `long long` is two `lw`/`sw` pairs and needs only four-byte alignment
in hardware; the conformance executable round-trips a
`std::uint64_t` through a four-byte-aligned address to prove it. So the
package format's four-byte alignment is sufficient here. GCC still reports
`__alignof__(long long)` as 8 on o32 and pads structs accordingly; the run
reports both facts. The corresponding hazard is inert for the same reason it
is inert everywhere: the package is read by explicit byte offset, with no
`memcpy`, `reinterpret_cast` or `union` anywhere in `engine/`.

## Deliberately not here

- **No second memory card slot.** Port one is the only place the campaign
  looks; selecting port two is one bit of `JOY_CTRL` and a preference screen
  nobody has needed.
- **No deleting a save from inside the game.** The card's own file manager can
  delete it, which is where a player looks first.
- **No autotiled coastlines.** Base variants only, as on every client; the
  blob sheets exist in `assets/n64_ci4/` for anything that can stream them.
- **No shadow layer.** `view::Layer::shadow` is offered and unused, as on the
  Nintendo 64; `playstation.h` carries the drop shadow, nothing draws it.
- **No `TargetProfile::playstation`.** The enumeration's values are serialized
  and append-only, and adding one is an `engine/` change. The play executables
  load with `TargetProfile::portable`, a real profile and not a stand-in.
- **No golden frame.** The digest is printed, not pinned: the per-cell claims
  localise a failure, a pinned digest would not.
- **No second emulator.** Mednafen would be the accuracy second opinion, and
  ares the day its PlayStation core leaves Experimental. A renderer gives a
  second emulator something to disagree about: `GP0(0xC0)` semantics and
  rasteriser edge rules. This one draws axis-aligned rectangles on integer
  coordinates, the part nothing disagrees about.
