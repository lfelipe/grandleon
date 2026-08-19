# What the Nintendo 64 scratch photographed

Produced by the measurement ROMs in `platform/nintendo64/scratch/`, run under
the pinned ares. Nothing here is a check and nothing is in a gate.

Films are **not committed**, the same rule the PlayStation scratch follows next
door: they are several megabytes each and they regenerate from one command. The
stills are, because they are what an argument gets made from.

## Regenerating

Build with the scratch targets enabled, which are off by default:

```sh
platform/nintendo64/scripts/build-n64.sh --scratch3d \
    --targets grandleon_n64_figures
GRANDLEON_N64_BUILD_DIR=$PWD/build-n64-scratch \
GRANDLEON_N64_ARES_FILM_AFTER=figures \
GRANDLEON_N64_ARES_FILM_SECONDS=15 \
GRANDLEON_N64_ARES_FILM=figures-film \
    platform/nintendo64/ares/run-ares.sh \
    build-n64-scratch/platform/nintendo64/grandleon_n64_figures.z64
```

The grabs land in `build-n64-scratch/ares/figures-film/`. Assembling them into a
GIF is the same de-duplicate-and-quantise pass `scripts/readme-screenshots.sh`
uses for the showcase film: the grabbers sample far faster than ares presents,
so most grabs repeat the one before, and what survives is frame for frame what
the emulator showed.

## What the two ROMs are for

`grandleon_n64_scratch3d` measures what a triangle costs: a sweep from 64 to
4,096 triangles through `rdpq_triangle` and again through Tiny3D, so the slope
separates per-triangle cost from per-frame overhead. Tiny3D came out three times
cheaper, which is the whole reason the libdragon pin is on `preview`.

`grandleon_n64_figures` draws the roster -- sixteen figures, the generated part
tables, colours resolved from each archetype's own CLUTs -- and proves the
authored far-to-near order carries the picture with **no depth test at all**.
It reports the three numbers that argument rests on: the authored order against
a painter's sort computed for the live camera, against a reversed order, and the
depth flag on against off.
