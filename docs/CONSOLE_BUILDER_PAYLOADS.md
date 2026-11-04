# Console builder payload contract

Grand Leon 0.3 keeps console toolchains out of the base PC installation. An
optional, versioned Linux payload—suitable for a Steam DLC depot—enables one
console target in **Finish & Export** without a source checkout, network
access, or a container runtime.

This document describes the host contract and the sealed payload packager.
Producing release payload archives, completing their license review, and
emulator-signing their outputs remain release work.

## Installed layout

The PC bundle discovers each target independently beneath its read-only
installation root:

```text
builders/
  nintendo64/
    builder.json
    build-rom
    rootfs.tar.xz
    busybox
    dependencies.json
    LICENSE.txt
    NOTICE.txt
    THIRD_PARTY_LICENSES.tar.xz
  playstation/
    builder.json
    build-disc
    rootfs.tar.xz
    busybox
    dependencies.json
    LICENSE.txt
    NOTICE.txt
    THIRD_PARTY_LICENSES.tar.xz
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
  "dependenciesSha256": "64 lowercase hexadecimal characters",
  "licenseFiles": ["LICENSE.txt"],
  "systemRequirements": ["bubblewrap"]
}
```

Manifest paths are portable single-file names. The host refuses links, missing
inventories, incompatible targets/contracts, and an executable or inventory
whose SHA-256 does not match. An optional `arguments` array may contain
portable flags but not paths, control characters, or drive separators.

`dependencies.json` is both the machine-readable component inventory and the
complete authenticated file list for the payload:

```json
{
  "format": "grandleon-console-builder-inventory",
  "inventoryVersion": 1,
  "components": [
    {
      "name": "Grand Leon Nintendo 64 builder",
      "version": "0.3.0-1",
      "license": "MIT"
    }
  ],
  "files": [
    {
      "name": "rootfs.tar.xz",
      "role": "tool",
      "bytes": 268435456,
      "sha256": "64 lowercase hexadecimal characters"
    },
    {
      "name": "LICENSE.txt",
      "role": "license",
      "bytes": 1091,
      "sha256": "64 lowercase hexadecimal characters"
    }
  ]
}
```

Every component needs a non-empty name, version, and license identifier. Every
file other than `builder.json`, the executable, and the inventory itself must
appear exactly once with a positive byte length, SHA-256, and one of the roles
`template`, `tool`, `data`, or `license`. The manifest's license list must
exactly match the files with the `license` role. Extra files, directories, and
symbolic links make that target unavailable. This ensures a launcher cannot be
paired accidentally with a toolchain or helper from another payload version.
Contract version 1 requires exactly the `bubblewrap` system requirement; an
omitted, duplicated, or unknown requirement makes the payload unavailable.

## Sealed Linux builder

The release-maintainer command is:

```sh
platform/godot/scripts/package-console-builder-payload.sh \
  nintendo64 out/builders 0.3.0-1
platform/godot/scripts/package-console-builder-payload.sh \
  playstation out/builders 0.3.0-1
```

It refuses a dirty tree, takes the source from the exact `HEAD` revision, and
adds it to the same pinned toolchain image used by the repository gate. The
result is one compressed, authenticated root filesystem plus a small launcher,
static extraction utility, complete Debian package list, and license archive.
The packager verifies the cached toolchain image's embedded revision markers
against the repository pins and includes the release host's BusyBox copyright
alongside the toolchain's package documentation and common license texts.
Docker is a release-maintainer input only; it is not installed with the game
and is not used by an author. The Linux host must provide Bubblewrap. Payload
discovery checks `/usr/bin/bwrap` and `/bin/bwrap` before enabling either
console target, so a system without it gets an installation requirement rather
than a failed build. Using the operating-system path also lets AppArmor-based
distributions apply their path-specific user-namespace policy, which a copied
binary would not inherit.

At export time the launcher expands the root beneath the ephemeral request and
enters it through a fresh user/process/network namespace. The root is mounted
read-only. `/tmp` and `/work` are fresh memory filesystems. The only writable
host bind is the empty output directory; the only other host files visible are
`request.json` and `project.json`, both read-only and renamed to fixed sandbox
paths. The installed game, payload directory, source checkout, home directory,
container socket, network, and Steam data are not mounted. The host environment
is cleared before the fixed toolchain environment is established, so a token
or credential in an environment variable cannot cross the boundary either.
The expanded root is deleted after success or failure.

Inside that boundary, Nintendo 64 builds `grandleon_n64_campaign` and
PlayStation builds `grandleon_playstation_campaign` plus its BIN/CUE disc. The
commands and pins match the repository builders; project-specific character
art is still selected at compile time. This matters because the Nintendo 64
image is compressed and both console builds embed only the drawings a campaign
actually uses, so a generic ROM template would not preserve the current art or
memory guarantees.

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

Each release payload still needs a license review and a golden comparison with
the repository builder at the package/runtime boundary. Its output must pass
the existing ares or PCSX-Redux boot, controller, save, and resume gates. The
host contract has a synthetic smoke covering asynchronous success, checksum
enforcement for every installed file, undeclared-file refusal, local-path
leakage, and atomic output. The packager smoke enters a minimal root through
the real launcher, checks its mount layout, and proves an inherited secret does
not cross the cleared environment. Clean-revision validation payloads from
both pinned images also build the demo project into a structurally valid ROM
and unlicensed-sector BIN/CUE. None of those checks replaces the golden
comparison or emulator acceptance of the final release payloads.
