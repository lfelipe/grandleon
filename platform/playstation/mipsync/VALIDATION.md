# Mipsync playable-slice validation

Validated 2026-11-08 from the real two-player Steel and Cedar Bell Orchard
encounter. The target reconstructs the exported encounter and calls the shared
simulation; it is not a hand-authored Mipsync rules demo.

## Exact artifacts

- input JSON SHA-256:
  `5a3c3549cc53adfb0b8124c46fc2628385c037e030b5bc6030560a15db5c538a`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 2,944,704 bytes, SHA-256
  `e4efdb911882a5501d3f7fc6ff02247ddd32afc467b68fe3b308eeea86ae1c3a`
- build manifest SHA-256:
  `c7919a8e578d7e048881bc403ed35ded7c41e1dd160b8a38edaf43b3ab174143`
- idle 320x240 frame SHA-256:
  `f48face29bd5cab5bc9e27aad57ca65b4c0076f087012677ffbb59fc37582258`
- idle emulator log SHA-256:
  `a8dc9ffeceb1683235b0e6a6a9853c4b960bbbac66513ae14cb4b66d4ef38aa3`
- scripted-turn emulator log SHA-256:
  `5fc72f23842a6ecf58dccb4b22d2a64d5e4cc073c025f17ec470c7d68cc2c9a7`

The disc was built by the checked-in wrapper from Mipsync
`866f405a0e34b5925a8d832272fe52c657688430` plus the checked-in three-patch
series. Both the PS-EXE and BIN contain `GRANDLEON_PS1_RENDERER=MIPSYNC`.

## Measured gates

| Gate | Result |
| --- | --- |
| BIOS-style boot | PCSX-Redux interpreter, software GPU, OpenBIOS; clean CUE boot |
| Real content | Bell Orchard 8x6, 8 units, 2 local players |
| Shared rules | canonical initial hash `cd555b4fda38d374` |
| Input/turn proof | select 4, move 1, player wait 4, opposing wait 4; final hash `cec012b51530befc` |
| Idle frame budget | 245 HBlanks average, 246 maximum; zero misses over 600 measured frames |
| Active-turn frame budget | 591 redraws at 244 average/248 maximum; ready 245 average/254 maximum |
| Active-turn schedule | 9 mutations use 9 redraw-deferred slices; mutation max 109 HBlanks |
| Active-turn stability | zero missed VBlanks over 600 measured frames; maximum elapsed 1 VBlank |
| Main RAM | text 411,340; data 6,432; BSS 137,724; resident 555,496 bytes |
| Reserved total | 1,178,088/2,097,152 bytes including 64 KiB audio, 32 KiB stack and 512 KiB gameplay heap |
| Renderer cache | 280/1,024 vertices; 152/1,024 triangles |
| Texture VRAM | 122,880 bytes used; 73,728-byte bounded atlas region |
| Renderer identity | present in PS-EXE, BIN and boot log |

The idle measurement excludes BIOS/disc cold start. Serial telemetry is emitted
after the 600-frame window. The active probe changes only the update function's
input argument through a debugger breakpoint; the executable still owns input
interpretation, rules, scene synchronization and opposing-turn behavior. Each
accepted mutation keeps the already-complete front buffer for one retrace and
redraws the synchronized scene on the next. The slowest rules slice is 109
HBlanks, leaving headroom within one frame; no update is skipped or simplified.

The Grandleon compiler uses Debian's o32 hard-float ABI metadata because its
hosted C++ headers do not include the o32-soft stubs. The R3000A code contains
no floating-point operations, and every call crossing into the PSn00bSDK
renderer libraries is integer- or pointer-only. The linker therefore reports a
metadata warning, while exact disc boot and both runtime proofs exercise the
boundary. A dedicated bare-metal C++ toolchain remains the long-term clean fix.

## Visual and surroundings policy

The tactical rectangle remains finite. Bell Orchard selects `natural_blend`:
deterministic edge-influenced terrain continues into a noninteractive apron,
rendered as coarse visible chunks with no tactical grid or highlights. The
portable adapter also exposes `extend_edges` and `fixed_terrain`; camera motion
must remain clamped to authored playable bounds.

Forest cells use three deterministic volumetric clusters in this scene. Each
has low-poly trunk and crossed/layered canopy geometry, while unit cutouts come
from the Godot graphics profile and team masks. Horizontal same-terrain runs
are coalesced and cached through Mipsync's static flat-mesh path; unit and prop
cards remain independent to preserve their silhouettes.

## Gates still open

This proves the real simulation update/input boundary, opposing turn, asset
conversion, disc and memory shape. It is not a cutover build. Production still
needs viewport-aware camera paging and scrolling, the complete controls and UI,
saves and campaign flow, close combat, the Grandleon platform audio seam, the
remaining rule actions and real-hardware validation.
Until then the current player is an explicitly named baseline, never a fallback.
