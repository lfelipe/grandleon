# Game-content compiler SDK

This host-side module validates game definitions and compiles them into the
experimental package container. It is installed as the
`grandleon::game_content` CMake target and depends on two public targets:
`grandleon::package_format`, which is the wire format it writes, and
`grandleon::simulation`, which is the contract it is held to. It reads one
thing from the latter: `simulation::maximum_stat`. The next section gives the
reason.

## Canonical project CLI

`grandleon_content_compile` reads a canonical source project and writes a
`.gpk`:

```sh
npm run --prefix tools/source_schema validate -- path/to/project.json
./build/grandleon_content_compile path/to/project.json output.gpk
./build/grandleon_package_check output.gpk
```

It reads exactly one source version and migrates nothing. That version is
`supported_source_schema` in
`include/grandleon/game_content/source_project.hpp`. A project at any other
version is refused by name, with both versions and the way up in the message;
`npm run --prefix tools/source_schema upgrade` is that way up, and the editor
offers it too.

The schema validator remains the authority for complete structural and semantic
validation. The native parser repeats the checks needed to map safely into its
bounded C++ types and never emits a partial package.

Source string IDs are converted with `stable_content_id_v1`. The package UUID
comes from `packageId`; `gameId` remains source metadata. Semantic content
revision components are packed into ten bits each as
`(major << 20) | (minor << 10) | patch`, so a component above 1023 is rejected.

The executable subset is classes, unit types, weapon and item types, weapons,
items, abilities, maps with terrain passability and movement cost, factions,
objectives, campaign encounter placements, dialogue, story nodes, conditional
transitions, and the project's presentation choices. Encounter placements use
`{id, unitTypeId, side, x, y}`.

The compiler refuses anything the runtime cannot execute rather than dropping
it, so an apparently valid package never silently loses gameplay. What it
refuses today, all of it schema-valid and therefore unsupported rather than
malformed, with a diagnostic that says so:

- a campaign predicate over inventory, for which there is no runtime state to
  evaluate it against;
- any non-empty `scriptBindings` or `extensions`, for which there is no runtime
  at all;
- a campaign with no `flow`. The schema leaves it optional because a campaign
  may be authored as a portable membership registry, but a campaign record
  holding no node has no node to enter and `campaign::load_campaign` refuses
  one outright;
- a content revision with a pre-release suffix, which the schema's version
  pattern admits and thirty packed bits cannot hold. Dropping the suffix would
  compile two different revisions to one number.

The promise at the head of that list is why this module links
`grandleon::simulation`. Every number the damage arithmetic reads stops at
`simulation::maximum_stat`: a class's `strength`, `defense`, `resistance` and
`magic`, and a weapon's, a cast's or an item's `power`. The compiler asks the
engine for that number rather than writing it out. Both gates that build an
encounter refuse a stat past it: `simulation::create_encounter` and the package
loader. A compiler keeping a larger copy of its own would hand an author a
`.gpk` for a project the game cannot open, which is the confusing kind of late
failure the promise exists to prevent.
`engine/package_runtime/src/encounter_loader.cpp` asks the same constant when
it reads the bytes back, so the writer and the reader of a package agree by
both asking rather than by both writing. This is the one refusal here that is
not schema-valid: `schemas/source/v1/common.schema.json` holds the same
ceiling, so the validator and the editor catch it first and the compiler is the
backstop for a project built by some other tool.

The compiler still keeps its own copy of every *encoded enumeration*, and that
is a different thing: those values are the wire format, and the wire format is
the contract this module owns. A bound on what the rules can execute is the
rules' to state.

`tests/fixtures/source_projects/README.md` explains the difference between
valid source and a compilable project, and
`tests/game_content/source_fixtures_test.cpp` asserts every fixture falls on the
side of that line it is supposed to, naming each exception and its reason.

Two kinds of resolution happen here rather than downstream, because the compiler
is the last place that still has the authored names: what a terrain admits and
what it charges, both from the terrain's name, and the presentation joins from
names to art-library indices, for theme, character style, faction colour,
terrain kind and archetype. A client sees hashes, not names, and could not redo
either.
`docs/FROM_EDITOR_TO_CONSOLE.md` describes where those indices go.

## Semantic model

The model separates:

- package manifest identity and display title
- weapon types from individual weapons
- item types from individual items
- unit classes from unit types/templates
- class base statistics from unit templates
- starting equipment from the unit definition that references it

References are `(category, stable ID)` relationships. IDs must be non-zero and
unique within their category. Typed namespaces allow class `42` and item `42` to
coexist without ambiguity while retaining compact 64-bit references.

Definitions use flat composition. Class inheritance and arbitrary property bags
are not part of this experiment: both make validation, editor behavior, target
budgets, and deterministic override order harder. They should be added only with
requirements that demonstrate why composition is insufficient.

Compilation sorts each category by stable ID, so authoring-file order does not
change package bytes. There is one exception, and it is deliberate: the
presentation section's faction table is written in authoring order, because a
faction that chooses no colour takes the menu colour at its own position in the
project's list. The position is content, so a table sorted by identity would
change what the game looks like. Nothing downstream depends on the order, since
`load_presentation` sorts and de-duplicates the table on read. But reversing
the `factions` array does move package bytes, and no other category does.

All identities, names, numeric constraints, duplicate
relationships, and cross-references are validated before any package is emitted.
Diagnostics use stable codes and semantic paths such as
`unit_types[60].class_id`. A later source parser will attach file/line/column
locations without changing these semantic diagnostics.

## Installed SDK interface

After installing Grandleon, an external package project can use:

```cmake
find_package(GrandleonPackageSdk 0.1 CONFIG REQUIRED)
target_link_libraries(my_game_compiler PRIVATE grandleon::game_content)
```

The build-tree alias has the same target name. Installed consumers receive only
public headers and exported libraries; engine source and private include
directories are not part of the interface.

## Tests

`grandleon.game_content` covers:

- every definition category, dialogue included, on the same identity rules
- canonical output independent of authoring order
- missing and duplicate references, including a story node's scenes
- duplicate typed IDs
- invalid names, stats, and ranges
- every string and list width the package writes, so nothing is truncated into
  a record that declares a length nothing wrote
- invalid derived stats and class/weapon compatibility, including the
  difference between a class that states no weapon allowance and one that
  states an empty one
- a node whose outgoing edges leave the taken route to authoring order
- an objective whose target no placement on its board answers to
- atomic failure with no partial package
- 2,000 independently addressable item definitions

`grandleon.source_fixtures` puts the whole source-fixture corpus through the
compiler, and `grandleon.source_project` covers the reader: what the schemas
bound, what each enumeration's vocabulary is, and what a scene may say about
its cast.

Canonical JSON remains the editable source; `.gpk` is generated output.
