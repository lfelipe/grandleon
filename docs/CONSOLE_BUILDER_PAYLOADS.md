# Console builder payload contract

Grandleon keeps console toolchains out of the base PC installation. An
optional, versioned Linux payload—suitable for a separate download—enables one
console target in **Finish & Export** without a source checkout, network
access, or a container runtime.

This document describes the host contract, sealed payload packager, automated
licence audit, and exact-output comparison required before publishing a
payload.

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
  "builderVersion": "0.4.0-1",
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
      "version": "0.4.0-1",
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
  nintendo64 out/builders 0.4.0-1
platform/godot/scripts/package-console-builder-payload.sh \
  playstation out/builders 0.4.0-1
```

It refuses a dirty tree, takes the source from the exact `HEAD` revision, and
adds it to the same pinned toolchain image used by the repository gate. The
result is one compressed, authenticated root filesystem plus a small launcher,
static extraction utility, complete Debian package list, and license archive.
The packager verifies the cached toolchain image's embedded revision markers
against the repository pins and includes the release host's BusyBox copyright
alongside the toolchain's package documentation and common license texts. The
pinned images also preserve the libdragon, Nugget, mkpsxiso, and bundled-tool
license files before deleting their fetched source trees; an older cached
image without that license bundle is refused.
Before publishing a payload, the packager audits its manifest and every file
hash, requires each installed Debian package's copyright file, checks the
pinned toolchain components and source revisions, and requires the expected
upstream license texts. This is an archive-completeness check; it does not
replace final storefront review or third-party notice and corresponding-source
compliance review.
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

These creator payloads currently produce the established sprite presentation.
The 0.4 3D console path is a separately built Sevenfold release showcase, not a
renderer switch in **Finish & Export**. Its paired sprite and 3D commands are in
[Console 3D](CONSOLE_3D_EXPERIMENT.md).

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
  "builderVersion": "0.4.0-1",
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

## Validate a payload

Each payload must pass its manifest, file-hash, component,
Debian-copyright, and upstream-licence audits, then build Emberbound inside the
offline sandbox. Nintendo 64 output must be byte-for-byte identical to the
ordinary repository build. PlayStation comparison may normalize only ISO 9660
recording timestamps and the GNU build-id note. The comparator that enforces
those boundaries is:

```sh
platform/godot/scripts/compare-console-builder-output.py \
  --target nintendo64 --payload PAYLOAD_OUTPUT --reference REPOSITORY_OUTPUT
platform/godot/scripts/compare-console-builder-output.py \
  --target playstation --payload PAYLOAD_OUTPUT --reference REPOSITORY_OUTPUT
```

The ordinary outputs from the same pinned builders must then pass all five ares
gates and all six PCSX-Redux gates, including two-controller co-op, native save
media, both campaign resume paths, and a twice-booted BIN/CUE. Exact N64
identity and normalized PS identity bridge the sandbox output to those gates.

The host contract also has a synthetic smoke covering asynchronous success,
checksum enforcement for every installed file, undeclared-file refusal,
local-path leakage, and atomic output. The packager smoke enters a minimal root
through the real launcher, checks its mount layout, and proves an inherited
secret does not cross the cleared environment. Packaging, audits, comparisons,
and hashes must be repeated from the exact revision being published.
