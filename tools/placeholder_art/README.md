# Generated pixel map art

This folder contains the small pixel-art library used by Godot previews,
documentation, and shared interface elements. It provides terrain tiles,
map-scale unit sprites, icons, portraits, and the Grandleon logo.

It is not the Nintendo 64 or PlayStation character renderer. Console characters
are generated from the curated Godot sheets and team-colour masks described in
[Artwork generation](../../docs/ARTWORK_GENERATION.md). Console terrain and
combat backgrounds live with each console platform.

## View the library

- [Gallery](GALLERY.md) shows the generated terrain, characters, portraits, and
  interface art.
- [Roster](ROSTER.md) lists the available map-scale character styles.
- `gallery/` contains the images used by those two pages.

The output profile is `modern`: 32×32 RGBA terrain and character art. Console
character assets are deliberately outside this generator.

## Regenerate it

After running `scripts/setup.sh`, use:

```sh
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py
```

That command rewrites `assets/`, `gallery/`, `GALLERY.md`, and `ROSTER.md`.
Before committing a change, verify that the checked-in output is current:

```sh
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py --check
```

The generator is deterministic. The check rebuilds everything in a temporary
directory and fails if any committed file is missing, stale, or unexpected.

## Replacing an image

Grandleon can accept hand-drawn replacements under `art/provided/`. A
replacement uses the same relative filename as the generated image it replaces.
List valid names and validate a contribution with:

```sh
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py --list-keys
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py --validate-provided
```

The validator checks dimensions, transparency, animation cells, palette use,
safe PNG structure, and character readability. A rejected image is left out of
the build and the command reports the rule that failed. The complete refusal
and substitution test is:

```sh
tools/placeholder_art/.venv/bin/python tools/placeholder_art/check_provided.py
```

Third-party artwork is not part of the 0.4 release, but this path is retained
for a possible later workflow.

## Where to make changes

The Python modules under `placeholder_art/` are the source:

- `terrain.py` and `themes.py` define the map tiles and biome palettes;
- `characters.py`, `styles.py`, `figures.py`, and `frames.py` define map-scale
  character drawings and animation strips;
- `portraits.py`, `backdrops.py`, and `logo.py` define the other visible art;
- `build.py` assembles the library; and
- `verify.py` checks seams, animation cells, palettes, and readability.

Add or edit source definitions, regenerate the library, then review the gallery
and run the drift check. For high-resolution Godot and console characters, use
the separate workflow in [Artwork generation](../../docs/ARTWORK_GENERATION.md).
