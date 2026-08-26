# Deferred: models, meshes and animation

Everything in this repository that waits on a third dimension, parked in one
place so that the working list of what is actually being done does not carry
it. That list is kept outside this repository and is not part of the public
export; this document is the part of it that belongs with the code, because it
is the reasons certain things are shaped the way they are.

**Nothing here is being worked on.** These are not open tasks; they are the
reasons certain things are shaped the way they are, and the conditions under
which they would become tasks. Moving an item out of this file is a decision to
start on 3D, not a bug fix.

## What is waiting

### Replacement art cannot yet be made future proof

[README.md](../README.md) describes presentation as a set of choices rather
than a pile of files: a project names a terrain theme, a character style and a
colour per side out of a generated library, and the choice travels inside the
compiled package.

Supplying your own drawing instead works as a mechanism today. What is not
settled is the contract it should be supplied *against*, because a replacement
that is correct for flat sprites is not obviously correct once a character is a
mesh with an animation on it. Fixing the sprite contract first would mean
publishing a promise we would have to break.

**Becomes a task when:** the two items below are settled, in that order.

### There is no animation story

Terrain animates from the board's own frame counter and every gesture settles
exactly at rest — that much is decided and is in
[CODING.md](../CODING.md), "What may be drawn, and what may not be invented".
What does not exist is any notion of an *authored* animation: a character has
one drawing per archetype per style per figure, and nothing sequences them.

Until an authored animation has a shape, "what a replacement drawing must
supply" has no answer.

### `characterGeometry` is authored, read, and drawn by nothing

The source schema carries `characterGeometry` and the compiler reads it, so a
project may already ask to be drawn with models. No shipped ROM has a mesh
path.

The editor deliberately offers no control for it. From
`editor/src/domain/source-form-model.ts`, `withheldProjectFields`:

> "nothing draws models yet; the control would do nothing"

> A control for a setting that changes nothing an author can see is worse than
> no control — it reads as a feature and behaves as a no-op. It goes on the
> settings page the day a build honours it.

That is the rule to keep: the control appears when a build honours the field,
not before.

**Becomes a task when:** a build honours it.

### The Nintendo 64 mesh pipeline needs an unpinnable toolchain

From [DESIGN.md](../DESIGN.md) §3.4:

> The stable libdragon branch and its `rdpq` 2D API is the target. The board's
> projection is axis-aligned rather than isometric. The generated tiles are
> top-down squares, and no runtime transform makes a top-down tree read as an
> isometric one. A 3D mesh pipeline would additionally require libdragon's
> preview branch, which publishes nothing to pin against.

This is the hard blocker and it is not ours to clear. [CODING.md](../CODING.md),
"Reproducible toolchains", requires every console toolchain to be pinned inside
a container; a preview branch with no release to pin against cannot satisfy
that.

**Becomes a task when:** libdragon's preview branch publishes something
pinnable, or the Nintendo 64 stops being a target the mesh path has to serve.

## What is *not* waiting on this

Worth stating, because the absence of 3D is sometimes read as a limitation it
is not:

- **Elevation already works.** A terrain kind declares how many levels it reads
  as, and every renderer lifts it. It is drawn and never simulated: no rule
  reads it, no snapshot carries it, no canonical hash observes it. Nothing here
  changes that, and nothing about relief is blocked on meshes.
- **DESIGN.md §10 already says 3D rendering is a non-goal**, in the specific
  sense that the architecture must not *assume* it. That is a constraint kept,
  not a promise pending: the engine is headless and issues no drawing commands,
  so a renderer that drew meshes would need no rule changed.
