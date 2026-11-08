# Mipsync renderer validation

Validated 2026-11-07 from the real two-player Steel and Cedar Bell Orchard
encounter, not a hand-authored Mipsync scene.

## Exact artifacts

- CUE: `grandleon-mipsync.cue`, 80 bytes,
  SHA-256 `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: `grandleon-mipsync.bin`, 2,763,600 bytes,
  SHA-256 `7f524c9640e398641d7753f6bf7b05a7e1299ff5325ee3775e8f04da5e817ed7`
- Build manifest SHA-256:
  `753107591b226f4eec4e067d0f83fe525c86649c7d240b8e28a5413e1b811cdd`
- 320x240 frame SHA-256:
  `8d8b1d0daccd42af526efa4ba46d26eddd1d76cb94bbd8e7ce2aeb581f424f38`
- Emulator log SHA-256:
  `cbf5cda88d2a05fbd3c216c996f83f54cd68ad3fc2d110a06ba3ad6b7181810c`

The disc was built by the checked-in wrapper from Mipsync
`866f405a0e34b5925a8d832272fe52c657688430` plus the checked-in two-patch
series. Both the PS-EXE and BIN contain `GRANDLEON_PS1_RENDERER=MIPSYNC`.

## Measured gates

| Gate | Result |
| --- | --- |
| BIOS-style boot | PCSX-Redux interpreter, software GPU, OpenBIOS; clean CUE boot |
| Real content | Bell Orchard 8x6, 8 units, 2 local players |
| Frame budget | 241 HBlanks average, 242 maximum, about 15.32/15.38 ms |
| Frame stability | zero missed VBlanks over 600 post-warmup frames |
| Main RAM | text 256,916; data 412; BSS 127,100; resident 384,428 bytes |
| RAM with audio reserve | 449,964/524,288 bytes |
| Renderer cache | 320/1,024 vertices; 176/1,024 triangles |
| Texture VRAM | 114,688 bytes used; 81,920 bytes reserved in the bounded atlas |
| Renderer identity | present in PS-EXE and BIN |

The 60-frame warmup excludes BIOS/disc cold start. Serial telemetry is emitted
after the measurement so logging cannot create a false missed-VBlank result.

## Visual policy

The tactical rectangle remains finite. Bell Orchard's automatic surroundings
select `natural_blend`: deterministic edge-influenced terrain continues into a
noninteractive apron, rendered as coarse visible chunks with no tactical grid
or highlights. The adapter also exposes `extend_edges` and `fixed_terrain`.
The static probe camera is centered on playable bounds and never centers the
apron.

Connected forest cells select one deterministic volumetric cluster at a local
hash minimum. A cluster uses crossed trunk and canopy planes; isolated forest
keeps the terrain texture as its far LOD. Unit cutouts come from the same Godot
graphics profile and team masks as the desktop player.

## Gates still open

This proves the renderer, asset conversion, disc and memory shape. It is not a
cutover build. Production still needs the live simulation adapter, viewport-
aware camera paging for larger scrolling maps, controls, game UI, save/campaign
flow, close combat, platform audio and real-hardware validation. Each must reach
parity before release/export points at Mipsync. Until then the current player is
an explicitly named baseline, never a fallback.

