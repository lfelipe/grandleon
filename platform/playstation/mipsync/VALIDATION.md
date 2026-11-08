# Mipsync playable-slice validation

Validated 2026-11-08 from the real two-player Steel and Cedar Bell Orchard
encounter. The target reconstructs the exported encounter and calls the shared
simulation; it is not a hand-authored Mipsync rules demo.

## Exact artifacts

- input JSON SHA-256:
  `5a3c3549cc53adfb0b8124c46fc2628385c037e030b5bc6030560a15db5c538a`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 2,942,352 bytes, SHA-256
  `bdab38e811a856684bcea69aba1a0c542197594f5e47282bcf1b62676940ed71`
- build manifest SHA-256:
  `dec5bc1a565d5b7016a36493480dbbb83aac1099fb6a70cda99458d6a34e870a`
- idle 320x240 frame SHA-256:
  `f48face29bd5cab5bc9e27aad57ca65b4c0076f087012677ffbb59fc37582258`
- idle emulator log SHA-256:
  `a630e449952f7096d9f14b751e600eb75fb6b40c78ed21bd56c063d260aa854b`
- scripted-turn emulator log SHA-256:
  `4db766ec12fbafd365690a220e11069cf72c3de4950dfcbe482c0f3d08c04fc6`

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
| Active-turn frame budget | render 245 average/252 maximum; ready 246 average/361 maximum |
| Active-turn stability | 9 transient misses during 9 rule mutations; maximum elapsed 2 VBlanks |
| Main RAM | text 410,940; data 4,784; BSS 137,724; resident 553,448 bytes |
| Reserved total | 1,176,040/2,097,152 bytes including 64 KiB audio, 32 KiB stack and 512 KiB gameplay heap |
| Renderer cache | 280/1,024 vertices; 152/1,024 triangles |
| Texture VRAM | 122,880 bytes used; 73,728-byte bounded atlas region |
| Renderer identity | present in PS-EXE, BIN and boot log |

The idle measurement excludes BIOS/disc cold start. Serial telemetry is emitted
after the 600-frame window. The active probe changes only the update function's
input argument through a debugger breakpoint; the executable still owns input
interpretation, rules, scene synchronization and opposing-turn behavior. The
nine one-frame update spikes are bounded but remain an optimization target.

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
remaining rule actions, command-spike optimization and real-hardware validation.
Until then the current player is an explicitly named baseline, never a fallback.
