# Grandleon 0.4 console review builds

These are ready-to-play builds of **Sevenfold Gate**, the 0.4 showcase
campaign. It covers all seven art themes, all eight unit roles, male and female
characters, every terrain kind, and one- or two-player local play.

Both builds use Grandleon's only console renderer. They include the review
Stage picker and live FPS display so the whole campaign can be inspected
quickly.

## Nintendo 64

Open `grandleon-0.4.0-n64.z64` in ares, on a compatible FPGA system, or from a
flash cartridge. The ROM uses SRAM saves.

## PlayStation

Keep `grandleon-0.4.0-ps1.bin` and `grandleon-0.4.0-ps1.cue` together and open
the `.cue`. The image works with compatible emulators and FPGA systems. It
intentionally has no Sony licence sector, so it will not boot on an unmodified
retail PlayStation.

See [Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md) for
controls and more detail. The hashes in `SHA256SUMS` identify the exact files
tested for this review.

Grandleon is distributed under the included MIT `LICENSE`. Third-party notices
are retained in `NOTICE`.
