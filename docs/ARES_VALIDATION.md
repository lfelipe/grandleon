# ares, the Nintendo 64 emulator we verify against

ares decides. Where another emulator disagrees with it, or cannot follow where
it goes, ares is right. Every Nintendo 64 check runs there and nowhere else.

The reason is ISViewer, a ROM-writable text channel. A ROM can print its own
verdict, so the harness reads a result instead of guessing at pixels. That is a
Nintendo 64 affordance and does not travel to other consoles;
`platform/playstation/README.md` records how that one answers the same problem.

## What it executes

ares runs **the real pipeline**: CPU and RSP recompilers, and paraLLEl-RDP on a
software Vulkan driver. It executes the RSP rather than pattern-matching what
runs on it, which is why it is the only pinned harness that can run a ROM that
draws through the RDP at all — the board is drawn on libdragon's `rspq`
microcode, and an emulator that cannot execute that microcode sees nothing the
RDP draws. `platform/nintendo64/README.md` and
`platform/nintendo64/ares/run-ares.sh` say the same thing where they need it.

That is also why there is deliberately no second Nintendo 64 emulator.
mupen64plus was evaluated and rejected: its pinned `rsp-hle` is high-level and
cannot execute `rspq`, so it is not a weaker check on the same thing but a
harness that can observe nothing. The cross-check that carries weight here is
host against console, not emulator against emulator.

**This is load-bearing beyond the checks, so it is stated here rather than left
to be inferred.** The libdragon pin is on the `preview` branch, which rewrites
IPL3 to decompress the ROM at boot, and that decompression runs on the RSP: a
ROM built here produces no output at all under a high-level-RSP emulator. The
pin could only move because ares executes the RSP for real, and it can only stay
there while that holds. `cmake/GrandleonNintendo64.cmake` records what the move
was measured to cost, and README.md states the restriction where an author
downloading a ROM will meet it.

## Versions

| | |
|---|---|
| ares | v148 |
| Base image | `ubuntu:24.04` |
| Derived image | `grandleon/n64-ares:<ares commit>` |

The exact ares commit and the base image digest are pinned in
`platform/nintendo64/ares/run-ares.sh` and
`platform/nintendo64/ares/Containerfile`. The built image records its ares
commit at `/opt/ares/ARES_COMMIT`, and every run checks it before starting. The
apt layer is not version-pinned, in common with every other image here.

## What runs there

Four checks. `grandleon_n64_check_all` builds the ROMs once and runs all four
concurrently.

| Target | What it proves |
|---|---|
| `grandleon_n64_check` | the conformance ROM reproduces the canonical hashes the host and WebAssembly builds produce |
| `grandleon_n64_play_check` | the play ROM draws the board through the RDP |
| `grandleon_n64_autopilot_check` | a scripted game plays through to its ending |
| `grandleon_n64_campaign_check` | a campaign survives a power cycle |

The last is the one no host can make. It boots the same ROM twice, in two
processes, over a single cartridge save file: founding a campaign, then
resuming it. The ROM carries no flag saying which run it is, so a cartridge
that forgot would announce a founding twice and fail on the word.

## Running it

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_n64

platform/nintendo64/ares/run-ares.sh                # conformance ROM
platform/nintendo64/ares/run-ares.sh path/to/a.z64  # any other ROM
platform/nintendo64/ares/run-ares.sh --rebuild-image
```

The image builds once and caches, which takes about seven minutes. Exit status
comes from the ROM's own `RESULT PASS` line, so a ROM that never boots prints
nothing and fails rather than passing quietly. Logs, screenshots and the
resolved Vulkan device land in `build-n64/ares/`.

Running a GUI emulator with no screen and no GPU takes several settings whose
absence fails in confusing ways. They are set and explained in
`platform/nintendo64/ares/headless.sh`.
