# Web compatibility client

The supported Grandleon creator is the Godot application. This directory holds
the Vue/TypeScript web client used to verify portable project archives, source
validation, WebAssembly engine behavior, and browser deployment.

If you want to make a game, start with the
[Godot creation guide](../docs/CREATING_A_GAME.md).

## Run the web client

```sh
npm ci --prefix editor
npm run --prefix editor dev
```

Open the URL printed by Vite. To choose a port:

```sh
npm run --prefix editor dev -- --port 4891
```

The web client can load the included projects, validate and edit portable
source, run the shared C++ engine through WebAssembly, and import or export
`.grandleon.zip` archives. Browser storage is recovery storage; keep important
projects as exported archives or canonical source under version control.

Projects exported here can be imported by the Godot creator. Browser-local
recovery data and playtest progress are not part of the portable archive.
Older source is migrated before it is opened, and the current one/two/choice
campaign launch policy survives import and export. Choosing that policy through
an ordinary creator control is part of the planned editor pass; today the
starter project already uses the launch-time choice.

## Checks

```sh
npm run --prefix editor typecheck
npm test --prefix editor
npm run --prefix editor build
npm run --prefix editor test:startup
npm run --prefix editor test:deployment
npm run --prefix editor test:offline
npm run --prefix editor test:browser
npm run --prefix editor test:dev
```

`test:browser` and `test:dev` need the Chromium installation described in
[`docs/SETUP.md`](../docs/SETUP.md). The production build is written to
`editor/dist/` and contains static assets only.

Set `GRANDLEON_EDITOR_BASE` when checking a sub-path deployment:

```sh
GRANDLEON_EDITOR_BASE=/tools/grandleon/ npm run --prefix editor build
```

## Boundaries

- Portable rules come from the WebAssembly build of the shared engine, not a
  JavaScript reimplementation.
- Source validation uses the canonical schemas and migration registry under
  `tools/source_schema`.
- Project import validates the complete archive before replacing an open
  document.
- The service worker is scoped to the configured base path and handles only
  same-origin `GET` requests.
- Production pages use a strict Content Security Policy and no runtime code
  generation.

[src/domain/README.md](src/domain/README.md) describes the source-document,
command, validation, and storage boundaries for contributors working on this
client.
