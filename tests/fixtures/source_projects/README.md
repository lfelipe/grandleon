# Source-project fixtures

Canonical source-schema `1.0.0` projects, used by every analyzer in the
toolchain: `tools/source_schema/test.mjs`, the editor's
`source-conformance.test.ts`, and the native `grandleon.source_fixtures` test.

## What the two directories mean

`valid/` means **valid source**: a project `tools/source_schema/validate.mjs`
accepts with no diagnostics. `invalid/` means a project it refuses, and each one
is there to pin a particular diagnostic at a particular instance path.

`valid/` does **not** mean "compiles to a `.gpk`". The native compiler is
deliberately narrower than schema validation: it refuses anything the runtime
could not execute rather than emitting a package that silently loses gameplay,
and `tools/game_content/README.md` lists what falls under that. Three fixtures
here are valid source and refused on exactly those grounds: a project with
script bindings, one with a campaign predicate over inventory, and one whose
campaign is a membership registry with no flow to walk.

Both directions are asserted, fixture by fixture, in
`tests/game_content/source_fixtures_test.cpp`. Every difference between what the
schema validator accepts and what the compiler compiles is named there with its
reason, and the test fails if a fixture changes category without the list
changing with it. That is the whole point of the two tables: a rule that only
one analyzer holds is a rule that will drift.

## Adding a fixture

Put it in the directory that matches what `validate.mjs` says about it, and
expect it to be compiled by the native test as well. If it is valid source that
the compiler cannot emit a package for, add it to `refused_though_valid` with
the diagnostic that refuses it. If you cannot write a reason there pointing at
a documented refusal, the compiler needs changing, not the list.
