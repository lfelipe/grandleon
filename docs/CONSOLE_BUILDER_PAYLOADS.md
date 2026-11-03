# Console builder payload contract

Grand Leon 0.3 keeps console toolchains out of the base PC installation. An
optional, versioned payload—suitable for a Steam DLC depot—may enable one
console target in **Finish & Export** without a source checkout or access to a
container socket.

This document describes the host contract implemented by the Godot creator.
It does not make the current repository build scripts redistributable payloads;
packaging and emulator-signing the first real payloads remains release work.

## Installed layout

The PC bundle discovers each target independently beneath its read-only
installation root:

```text
builders/
  nintendo64/
    builder.json
    build-rom
    dependencies.json
    LICENSE.txt
  playstation/
    builder.json
    build-disc
    dependencies.json
    LICENSE.txt
```

`builder.json` uses contract version 1:

```json
{
  "format": "grandleon-console-builder",
  "contractVersion": 1,
  "target": "nintendo64",
  "builderVersion": "0.3.0-1",
  "executable": "build-rom",
  "executableSha256": "64 lowercase hexadecimal characters",
  "dependencies": "dependencies.json",
  "licenseFiles": ["LICENSE.txt"]
}
```

Manifest paths are portable single-file names. The host refuses links, missing
inventories, incompatible targets/contracts, and an executable whose SHA-256
does not match. An optional `arguments` array may contain portable flags but
not paths, control characters, or drive separators.

## One isolated request

The creator first compiles the selected project in memory. If it is playable,
it creates a new request directory in per-user export storage containing only:

```text
request.json
project.json
output/
```

It starts the payload asynchronously with these arguments:

```text
--request ABSOLUTE_REQUEST_JSON
--source ABSOLUTE_PROJECT_JSON
--output ABSOLUTE_EMPTY_OUTPUT_DIRECTORY
```

No save, control profile, graphics override, credential, Steam data, home
directory, source checkout, installation directory, or container socket is
passed. The executable is a separately installed trusted release component;
the host never invokes a shell command assembled from project text.

`request.json` names the target, contract, builder version, and the two relative
request entries. A payload must treat the canonical source as read-only and
write only to the supplied empty output directory.

## Result

The payload writes `result.json` beside its artifacts:

```json
{
  "format": "grandleon-console-build-result",
  "contractVersion": 1,
  "target": "nintendo64",
  "builderVersion": "0.3.0-1",
  "artifacts": [
    {
      "name": "my-game.z64",
      "bytes": 8388608,
      "sha256": "64 lowercase hexadecimal characters"
    }
  ],
  "warnings": []
}
```

Nintendo 64 returns exactly one `.z64` ROM with the big-endian cartridge
header. PlayStation returns exactly one sector-aligned `.bin` and one `.cue`.
The PlayStation payload must retain the project's existing no-license-sector
policy; its image is for development/emulator or suitable homebrew hardware,
not an assertion that an unmodified retail console will boot it.

Before making output visible, the host checks:

- exact target, contract, and builder version;
- the exact artifact set, declared byte lengths, SHA-256 hashes, target size
  limits, and basic ROM/disc structure;
- no links, subdirectories, or undeclared files;
- no artifact contains the project-library, payload, request, or user-data
  path;
- warning text is bounded and any local path is removed.

The host rewrites a sanitized result report, atomically moves the verified
directory into per-user `exports/consoles`, then removes the request. A failed
or interrupted build never replaces a completed export.

## Release acceptance

Each real payload still needs a complete license/dependency inventory and a
golden comparison with the repository builder at the package/runtime boundary.
Its output must pass the existing ares or PCSX-Redux boot, controller, save,
and resume gates. The host contract has a synthetic smoke covering asynchronous
success, checksum enforcement, undeclared-file refusal, local-path leakage,
and atomic output; that smoke does not replace emulator acceptance of the real
payload.
