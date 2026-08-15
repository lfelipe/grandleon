# Core

`grandleon_core` is the substrate every other module stands on: the engine
version, the identity scheme content is named by, the deterministic random
stream, and the bounds question every parser of untrusted bytes asks. It knows
nothing about games.

It depends on nothing: no other Grandleon target, no platform SDK, no
filesystem, no clock, and almost nothing from the standard library beyond
`<array>`, `<cstdint>` and `<string_view>`. Nearly all of it is header-only and
`constexpr`, which is why a conformance ROM on a console can call it with
nothing else built.

## What it owns

### The engine version

`Version` and `engine_version()`. Three sixteen-bit fields, compared for exact
equality. This is what `engine/package_format` checks a package's compatibility
range against.

### Content identity

`stable_content_id_v1(source_key)` maps an authored key such as
`"tarnholt_line"` or `"tarnholt_line/fordlight_battle"` to a 64-bit
`StableContentId` by FNV-1a-64. Around it sit `PackageId` (sixteen bytes),
`ContentCategory`, and `ContentRef`, which names one record as the triple of
package, category, and stable id.

Two properties are load bearing:

- **It is a persistence contract, not a security hash.** Compiled packages,
  saved games, and cross-package references all carry these numbers, so the
  function's arithmetic can never change. A different mapping would be
  `stable_content_id_v2` beside it, not a new body for this one.
- **A collision is the compiler's problem.** Nothing here can detect one. A
  compiler must reject two source keys that collide within a single package and
  category, and `tools/game_content` does.

`ContentCategory` is serialized, so its values are append-only. The same is true
of every other enumeration in this repository that reaches a package.

### The random stream

`random.hpp` is the whole deterministic random substrate, and it is mostly the
argument for its own choices. `random_draw(seed, stream, position)` is a pure
counter-based function: FNV-1a-64 over an eighteen-byte little-endian encoding
of its three arguments, finished with four shift-XOR-multiply rounds. There is
no generator state to carry around, because a draw is addressed rather than
produced. The Nth number of a stream is a function of `(seed, stream, N)` and
nothing else.

`RandomState` is what an encounter carries: the seed, and how far each stream
has advanced. `RandomStream` names the purposes `hit`, `drop` and `growth`, and
one stream's position never depends on another's.

Three consequences follow from that shape and each is why it was chosen:

- **A save resumed mid-battle rolls the number it was going to roll.** The
  position is the state; there is nothing else to serialize.
- **Adding a purpose rerolls nothing.** A drop roll cannot silently shift every
  hit in a battle, because the streams are independent.
- **A stream nothing has drawn from is not encoded at all**, so `RandomStream`
  can grow without moving a single golden hash. A seed, or an actual draw, does
  move them. That is exactly when a moved hash means something.

**Two rules draw from it, and only two.** A strike can miss (`roll_hit` in
`engine/simulation/src/encounter.cpp`, against the folded accuracy) and a
defeated character's drop can fail (`roll_drop`, in the same file). Both are
drawn inside the battle and hashed with it. Growth is the third named stream
and is drawn *outside* any battle, by `engine/campaign_runtime`, from a state
seeded off the completed battle's canonical hash. `engine/tactics` draws
nothing. `engine/simulation/README.md` states each stream's consumption order
and what the forecast promises about it.

### The bounds guard

`checked_region(total, offset, size)` in `bounds.hpp` answers whether
`[offset, offset + size)` lies inside a buffer of `total` bytes, and it is the
only place that question is answered. `engine/campaign` asks it of a save's
section directory and of the migration readers; `engine/package_format` asks it
of every scalar and every section it reads out of a package. Neither module
depends on the other, which is why the answer lives below both.

It exists as a function because the obvious inline spelling is wrong in a way
that no well-formed file reveals. `size <= total - offset` wraps when `offset`
is past the end, so the guard passes against a number near 2^64 and the read it
was guarding walks off the buffer. An offset past the end needs no hostile size
field, only a layout rule that computes one offset from another over a file
whose length the rule was never told. `tests/core/bounds_test.cpp` holds
both spellings side by side: they agree over every region a well-formed file
can describe, and disagree on every region that starts past the end.

## Boundaries

[CODING.md](../../CODING.md), "Architectural invariants", forbids this module a
dependency on platform SDKs, renderers, game packages, or host tools, and that
restriction is the whole point of it: `core` is the one target that compiles
unchanged for a host, a VR4300, and an R3000A.

## Tests

```sh
cmake --build build
ctest --test-dir build -R "grandleon.core|grandleon.content_identity|grandleon.random|grandleon.bounds" --output-on-failure
```
