# The source schema and its validator

The executable authoring-source contract the editor and the native package
compiler both hold to. It is deliberately separate from the compiled `.gpk`
format the engine consumes.

The canonical Draft 2020-12 schemas live in `schemas/source/v1/`. Source
identities are stable lowercase strings scoped by game, content category, and
schema contract. Numeric package IDs are derived later and are not authoring
identities.

Install the pinned test dependencies and run the fixtures with:

```sh
npm ci --prefix tools/source_schema
npm test --prefix tools/source_schema
```

Validate one project directly with:

```sh
npm run --prefix tools/source_schema validate -- \
  tests/fixtures/source_projects/valid/minimal.json
```

Bring an out-of-date project up to the version this checkout writes, in place:

```sh
npm run --prefix tools/source_schema upgrade -- games/demo/source/project.json
npm run --prefix tools/source_schema upgrade -- --check <project.json>
```

`migration.mjs` holds the version and every step between versions, and the
editor imports it rather than keeping a second copy. One registry, so a project
upgraded here and a project upgraded in a browser are the same project.
`--check` writes nothing and exits 1 when a file is behind.
[SOURCE_FORMAT.md](SOURCE_FORMAT.md) states the rules the registry keeps and
[CONTRIBUTING.md](../../CONTRIBUTING.md) what changing a schema costs.

Diagnostics contain a stable code, source filename, one-based line and column,
and JSON instance path. Structural validation is followed by semantic checks
that JSON Schema cannot express, including typed cross-references, duplicate
identities, and map cell counts.

The unchanged `valid/minimal.json` fixture proves legacy source remains valid.
`valid/typed-content.json` exercises weapon/item type definitions, class
allow-lists, typed records, and starting equipment. Browser conformance tests use
these same files and compare stable diagnostic code/path pairs.
`valid/authoring-registries.json` covers the explicitly authoring-only faction,
ability, objective, campaign, and dialogue registries without claiming runtime
support that does not yet exist.

The current limits are defensive authoring-tool budgets, not console or gameplay
limits. Target-specific package compilers may impose lower limits with their own
diagnostics.

## Canonical output

The serializer writes two-space indentation, a final newline, lexically sorted
object keys and preserved array order, and serializing canonical output again
produces identical bytes. That is what makes a no-edit save stable and keeps an
ordinary field edit a local diff. Inspect it with:

```sh
npm run --prefix tools/source_schema roundtrip -- \
  tests/fixtures/source_projects/valid/minimal.json
```

[`SOURCE_FORMAT.md`](SOURCE_FORMAT.md) states the identity, versioning and
cross-field rules the schemas cannot; `schemas/source/v1/` is the field
reference.
