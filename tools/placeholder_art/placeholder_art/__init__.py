# SPDX-License-Identifier: MIT
"""Reproducible placeholder-art generator for Grandleon.

The package renders a terrain tileset, autotiled terrain variants, terrain
transition sheets, and unit sprites from a single set of source definitions,
then emits them through a set of output *profiles* (modern/web, Nintendo 64
CI8, Nintendo 64 CI4).

Module map
----------
``palette``     the fixed master palette and its named ramps.
``rng``         deterministic, dependency-free random numbers.
``noise``       periodic value noise, the basis of every seamless texture.
``raster``      the palette-index canvas and lit drawing primitives.
``autotile``    the 47-tile blob mask convention and its coverage field.
``terrain``     one class per terrain type (the source of truth).
``characters``  one class per character archetype (the source of truth).
``profiles``    output targets and their quantisation rules.
``pngio``       a deterministic PNG encoder (no timestamps, fixed chunks).
``build``       orchestration: renders sources, writes every profile.
``gallery``     contact sheets and the generated ``GALLERY.md``.
``verify``      seam and adjacency assertions run during every build.

Every module is deterministic: all randomness is seeded from stable strings,
and no wall-clock, hash-order, or locale-dependent value ever reaches output.
"""

__all__ = ["build"]
