# Models, meshes and animation

What the third dimension is blocked on, collected in one place. This is the
branch that carries the work; `main` carries none of it.

Each item states the condition under which it stops being a blocker, so that
clearing one is a decision somebody took rather than a thing that drifted.

## What is blocked

### The Nintendo 64 mesh pipeline needs an unpinnable toolchain

The hard one, and it is not ours to clear. From [DESIGN.md](../DESIGN.md) §3.4:

> The stable libdragon branch and its `rdpq` 2D API is the target. The board's
> projection is axis-aligned rather than isometric. The generated tiles are
> top-down squares, and no runtime transform makes a top-down tree read as an
> isometric one. A 3D mesh pipeline would additionally require libdragon's
> preview branch, which publishes nothing to pin against.

[CODING.md](../CODING.md), "Reproducible toolchains", requires every console
toolchain to be pinned inside a container. A preview branch with no release to
pin against cannot satisfy that, and loosening the pin to accommodate it would
cost the reproducibility every other target's checks rest on.

**Clears when:** libdragon's preview branch publishes something pinnable, or
the Nintendo 64 stops being a target the mesh path has to serve.

### There is no animation story

Terrain animates from the board's own frame counter and every gesture settles
exactly at rest — that much is decided and is in [CODING.md](../CODING.md),
"What may be drawn, and what may not be invented". What does not exist is any
notion of an *authored* animation: a character has one drawing per archetype
per style per figure, and nothing sequences them.

A mesh without one is a statue. Until an authored animation has a shape, a
model is a different-looking token rather than a different thing.

**Clears when:** an authored animation has a shape the format can carry.

### Replacement art has no contract to be supplied against

[README.md](../README.md) describes presentation as a set of choices rather
than a pile of files: a project names a terrain theme, a character style and a
colour per side out of a generated library, and the choice travels inside the
compiled package.

Supplying your own drawing instead works as a mechanism. What is not settled is
the contract it should be supplied *against*, because a replacement that is
correct for a flat sprite is not obviously correct once a character is a mesh
with an animation on it. Publishing the sprite contract first would mean
publishing a promise that a mesh path would break.

**Clears when:** the two above do, in that order.

### A geometry choice that changes nothing an author can see

The rule the editor keeps, and the one to keep keeping: a control appears the
day a build honours the field it writes, and not before. A control for a setting
that changes nothing an author can see is worse than no control — it reads as a
feature and behaves as a no-op.

**Clears when:** a shipped build honours it.

## What is *not* blocked on this

Worth stating, because the absence of a third dimension is sometimes read as a
limitation it is not:

- **Elevation already works.** A terrain kind declares how many levels it reads
  as, and every renderer lifts it. It is drawn and never simulated: no rule
  reads it, no snapshot carries it, no canonical hash observes it. Relief is not
  waiting on meshes.
- **The architecture's non-goal is the *assumption* of 3D rendering, not the
  possibility of it.** DESIGN.md §10 is a constraint kept rather than a promise
  pending: the engine is headless and issues no drawing commands, so a renderer
  that drew meshes would need no rule changed.
