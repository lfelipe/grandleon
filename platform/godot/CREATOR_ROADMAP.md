# Native creator and export roadmap

The 0.3 release status and acceptance gates live in
[`docs/RELEASE_0_3.md`](../../docs/RELEASE_0_3.md). This document describes the
native creator architecture and the increments beyond the current vertical
slice.

Grand Leon's PC application is both the game player and the game creator. A
person can create a project, test it immediately in the same process, return to
editing, and export it without opening a browser or installing a development
toolchain.

This is the supported product direction. The repository's earlier browser
editor is a migration reference, not a second product surface. Its useful
behavior and test cases are moving into the Godot application and native
libraries; it is not a supported 0.3 authoring path and can be removed after
the remaining data editors reach parity.

## What already works

The first native seam is implemented:

- `GrandleonProjectCompiler.compile_source_json()` parses and compiles the
  canonical source schema inside the Godot process. It returns package bytes
  or structured source/compile diagnostics. It launches no process and accepts
  no filesystem path.
- `GrandleonSession.start_package()` starts a campaign directly from those
  bytes. An edited project therefore has a complete in-memory
  `source -> compile -> play` path.
- `ProjectLibrary` creates a canonical project from the native blank template,
  assigns a portable UUID, validates every save through the native parser, and
  keeps the prior source recoverable if replacement is interrupted.
- The controller-navigable Projects screen has one guided **New Campaign**
  path: name the adventure, create a complete playable campaign, and open its
  first map immediately. Existing cards emphasize **Continue Creating** and
  **Test Campaign**; story, rename, copy, and portable export sit behind
  **More**. Drafts enter the ordinary game scene without a temporary package.
  Copies receive new portable identities; renames preserve stable identifiers
  and content.
- Map-editor drafts proceed through the native company gate automatically and
  disable campaign persistence, so iteration reaches the edited board directly
  without reading or replacing an installed game's checkpoints.
- Portable `.grandleon.zip` exchange is implemented without an external
  archiver. Exports contain a version marker, canonical source, and
  project-owned assets only. Imports preflight stored ZIP entries, paths,
  expansion limits, and checksums before accepting them, retain compatibility
  with the earlier unmarked editor form, and turn identity collisions into new
  copies instead of overwriting a library project.
- The project import action opens a controller-native inbox which reveals only
  archive names and sizes, focuses the first candidate, refreshes without a
  restart, and imports or returns with the compact control profile. The ordinary
  desktop file browser remains an optional mouse-and-keyboard route rather than
  a requirement for the simple-control workflow.
- **Finish & Export** opens one controller-native destination for output. It
  writes an editable portable archive immediately and, from an installed clean
  Linux release, can build a separate player-only game. The exporter copies the
  tested runtime shell atomically, replaces its compiled campaign, strips the
  creator, templates, runtime saves, and local configuration, preserves
  licenses, and reports only portable names and hashes. Console targets remain
  visibly unavailable until their versioned builder payloads are installed.
- The first native map creator paints canonical terrain with the same art atlas
  used by play mode. Edits pass through the native integer-preserving source
  layer, support bounded undo/redo and recoverable save, and can be compiled,
  played, and reopened in the same project session. Blank projects can create
  maps without external tools. Map resize previews retained, clipped, and added
  cells and refuses to strand encounter data.
- **Look & Feel** chooses the project-wide world theme and character style from
  the shared portable art menus and shows the actual terrain and character
  assets. The map canvas now follows the authored theme instead of always
  previewing temperate ground. These presentation edits are undoable and leave
  gameplay source unchanged.
- **Add Next Chapter** inserts a story moment, a cloned playable encounter, and
  a fresh automatically surrounded map after the selected linear battle as one
  validated undo command. It preserves the old destination and refuses
  conditional/branched exits rather than guessing how to rewrite them.
- The focused Story creator lists the starter's real dialogue scenes and lines,
  cycles only portable cast speakers, and edits text through a native
  integer-preserving source command. It also creates a complete scene with an
  opening cast member, portable backdrop, and first line, and attaches it to an
  existing campaign story moment as one validated command. Lines can be added
  after the selection, reordered, or removed without allowing an empty scene;
  frequent creation stays on the main screen while ordering and deletion stay
  in **More**. Its **Speaker / Character** picker chooses from named project
  characters and atomically maintains the portable scene cast, including a
  deliberate no-character-art choice. The scene panel edits the portable
  backdrop and previews the same resolved portrait and backdrop wash used in
  play. It has bounded undo/redo, recoverable save, immediate draft play, and
  controller navigation around the keyboard writing fields. Visible line edits
  are committed when saving, navigating, testing, leaving, or changing the
  line list, so there is no separate Apply step that can silently lose typed
  text. A smoke reopens and compiles the saved dialogue and its campaign
  attachment.
- The focused Army creator shows the starting company and later recruits in
  story order, previews the same portable character art used in play, and
  renames a character without changing persistent identity. **Add a Character**
  chooses an existing named role and either the starting company or a chapter
  after which that recruit joins. **Make a New Role** asks only for a visible
  name and a working role to start from, then returns to the waiting character
  with the portable copy selected. Battle placement remains a deliberate Map
  → Battle Setup action, so growing the company never silently changes a board.
  **Campaign Supplies** chooses named shared items or weapons and their amount
  either at the opening or after a named chapter; these portable rewards use
  the same runtime path on every target. **Role Gear & Skills** assigns named
  weapons, items, and abilities shared by every character using the selected
  role; incompatible weapon types are hidden. Add, tune, rename, loadout,
  supply/reward, undo/redo, save/reopen, and compile are covered by a native
  smoke.
- The smoke test compiles the real demo project and starts its campaign. It
  also proves that malformed input returns a useful diagnostic instead of a
  partial package.

The bridge deliberately exposes bytes, not a build directory. That boundary
is portable across Linux and Windows and cannot record information about the
machine that compiled a project.

## Product shape

The application opens on one native project surface:

```text
Projects -> Creator -> Compile -> Play draft
                 |          |
                 |          +-> standalone PC game
                 +------------> portable source project
                 +------------> optional console target builder
```

`Play draft` is not an export. It passes the compiler's bytes directly to the
existing runtime, so it should take no more time than loading an ordinary
campaign.

### Project storage

Projects live under a platform-appropriate Godot user-data directory, never
beside the installed executable:

```text
user://projects/<project-id>/
    project.json          canonical, portable source
    assets/               project-owned source assets
    recovery/             rotating local recovery snapshots
    project.local.json    optional local-only preferences
```

Only `project.json` and `assets/` belong in a portable project archive.
`project.local.json` may hold window layout and recent selections, but never
travels with the project. Absolute paths, controller identities, Steam
credentials, and machine names are local configuration and must not be copied
into source, exports, logs, or build manifests.

Saving should use a new file plus an atomic rename, retain bounded recovery
snapshots, and preserve the last known-good source after a failed validation.
The portable interchange form is `.grandleon.zip`, with `grandleon.json` as a
version marker, `project.json` as canonical source, and optional relative paths
under `assets/`. The writer uses deterministic, uncompressed ZIP entries so the
earlier editor can still read new archives. The reader also accepts its
unmarked stored archives, but rejects compressed entries, traversal,
Windows-incompatible names, duplicate case variants, local configuration,
oversized expansion, and checksum mismatches before importing any file. Export
also refuses project-owned symlinks instead of following them into an archive.

### One schema authority

The canonical JSON schema remains the contract between the creator, PC player,
and console targets. The native compiler is the final semantic validator.

The creator still needs field descriptions, defaults, and migrations. These
should be generated from the source schema into a small Godot-readable schema
manifest rather than retyped in GDScript. Specialized editing behavior then
sits above that generated description:

- a typed project document and command-based undo/redo;
- a map tool with paint, fill, rectangle, placement, and safe resize commands;
- a campaign graph tool;
- focused editors for dialogue, objectives, units, items, weapons, and
  abilities;
- source-schema migrations covered by the existing migration fixtures;
- compile diagnostics mapped back to the relevant control or map element.

The creator must not silently discard a field it does not understand. A newer
source schema is opened read-only with an explicit explanation; an older one
is migrated into a copy before the original is changed.

## Native creator increments

Each increment ends in a useful application and a concise commit.

### 1. Project shell

- Add **Projects**, **Create**, and **Play** to the native start screen.
- Create from a few small templates rather than from a blank invalid document.
- Open, duplicate, rename, archive, import, and export projects.
- Add atomic save, recovery, dirty-state indication, and command undo/redo.
- Compile and play a draft through the in-memory path that already exists.

This slice proves the whole ownership model before building a large editor.

### 2. Map and encounter creator

- Map creation from blank projects is implemented with portable generated IDs,
  a useful initial size, and the selected terrain as its fill.
- Terrain palette plus controller/mouse brush, contiguous fill, and two-corner
  rectangle tools are implemented.
- Encounter selection, allied/enemy placement preview, and safe two-confirm
  placement movement are implemented. New allied roster members and enemy unit
  types can be placed, and placements can be removed with undo recovery.
- Deployment regions have a visible map overlay and an atomic controller-safe
  draft/apply flow. Canonical validation rejects duplicate, off-map,
  unoccupied, or impassable regions before source changes.
- Resize preview and top-left-preserving resize are implemented. Shrinks are
  blocked when a placement or deployment tile would be clipped.
- Map surroundings are implemented as a solid colour, automatically derived
  non-playable terrain, or a manually painted perimeter with exact coverage
  guidance. They remain presentation-only in portable content.
- Encounter objectives and two-player opponent scaling are implemented.
- Immediate visual and console-budget feedback.
- A single action to play the currently selected encounter.
- A single **Add Next Chapter** action grows a linear campaign with a story
  moment, ready battle, and editable map. General branch editing remains in the
  campaign-flow increment.

This comes first because it produces the shortest useful create/play loop and
ports the most interaction-heavy part of the earlier editor early.

### 3. Game data and campaign flow

- Starting characters and chapter recruits can be added and renamed through
  the Army screen. A focused hero-tuning overlay authors bounded portable
  health, strength, defense, resistance, movement, magic, and weapon-range
  adjustments over the selected role. Shared starting supplies and named
  item/weapon chapter rewards are implemented. Compatible named weapons,
  items, and abilities can be assigned to a role, and a new role can copy an
  existing valid baseline. Classes, factions, detailed role baselines, and
  asset definition editors remain.
- Stages, transitions, conditions, arrivals, and general route editing remain;
  starting roster and linear chapter rewards are implemented.
- Complete dialogue-line authoring with portable speaker selection and scene
  creation/attachment to existing story moments is implemented. Linear story
  and battle insertion is implemented through **Add Next Chapter**, and the
  focused screen edits and previews cast/backdrop presentation. General
  transition routing remains.
- Searchable reference pickers; raw stable IDs should not be normal UI.

### 4. Art and presentation

- Select the shared world theme and character style with an actual-asset
  preview. **Implemented.**
- Select an installed replacement pack or project-owned art pack.
- Import replacement terrain, character, portrait, effect, and UI assets.
- Validate dimensions, animation names, pivots, and platform budgets on import.
- Preview the same animation state machine used by play mode.
- Keep art manifests versioned and data-driven so a better pack can replace
  the initial pack without changing project gameplay data.

### 5. Export center

One screen reports the result for each target independently:

- portable editable project — implemented;
- standalone Linux PC game from the installed verified shell — implemented;
- standalone Windows PC game;
- Nintendo 64 image;
- PlayStation disc image.

A failed console budget must not prevent saving or exporting the PC game. An
export report records source revision, content identity, target, compiler
version, included art pack, warnings, and artifact hashes, but no absolute
paths or local configuration.

## Standalone PC games

A project can become a player-only game which does not expose creator UI and
does not require Grand Leon to be installed.

The Linux bundle builder already proves the first form of this boundary with
`--player-only`: it compiles a selected canonical project, removes creator
scenes, implementation, and source templates, and suppresses the creator entry
in the session menu. The native Export Center now performs the corresponding
runtime operation from a clean installed shell and writes through a temporary
directory before making the completed game visible.

The practical design is a signed, prebuilt player shell per operating system.
The creator copies that shell into a new output directory and adds:

- the compiled `.gpk`;
- the selected runtime art and audio packs;
- a small launch manifest naming the campaign and graphics profile;
- licenses and build metadata.

Source is excluded by default, with an explicit **Include editable source**
option. The player shell is built and tested by the Grand Leon release process;
the installed application is not expected to carry CMake, a compiler, or the
Godot editor. Godot's [`PCKPacker`](https://docs.godotengine.org/en/stable/classes/class_pckpacker.html)
can assemble project data at runtime, but executable and native-extension
templates still have to be shipped for every supported operating system.

The first milestone should export the host operating system. Cross-operating-
system export can follow by installing the other player shell as optional
content.

An exported folder can also include a SteamPipe-ready content layout and VDF
template with placeholder AppID/depot values. It must never upload or store a
Steam account token. Publishing each generated title on Steam is a separate
publisher operation: it needs its own application, store/build review, depots,
and an account with build permissions. Valve documents those requirements in
[Applications](https://partner.steamgames.com/doc/store/application), the
[review process](https://partner.steamgames.com/doc/store/review_process), and
[Uploading to Steam](https://partner.steamgames.com/doc/sdk/uploading).

## Windows feasibility

The creator, player, and local co-op are all reasonable Windows targets. The
engine code is portable C++ and Godot supports Windows exports. The concrete
work still required is:

1. Build and test `grandleon_godot.dll` on Windows.
2. Add Windows debug/release entries to `grandleon.gdextension`; a
   GDExtension file selects a different native library by operating-system and
   architecture feature tags, as documented by
   [Godot](https://docs.godotengine.org/en/stable/engine_details/engine_api/gdextension/gdextension_file.html).
3. Add a Windows export preset, player shell, signing step, and Windows CI.
4. Exercise two-controller input, focus recovery, and filesystem behavior on
   Windows hardware.

Windows executable signing can be performed during release export; Godot's
[Windows export documentation](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_windows.html)
supports Windows `SignTool` or cross-platform `osslsigncode`.

| Capability | Linux/SteamOS | Windows | Main blocker |
| --- | --- | --- | --- |
| Native creator and player | Foundation works | Feasible | Windows DLL, preset, CI |
| Simultaneous local co-op | Same runtime | Same runtime | Hardware/input validation |
| Standalone game for host OS | Player shell work | Player shell work | Export-center implementation |
| Current console scripts | Developer-only with Docker/Podman | Do not run natively | Bash, `flock`, POSIX paths, mounted repository |
| Reworked OCI builders | Conditional on a runtime | Conditional on Docker Desktop/compatible runtime | External runtime installation |
| Patchable console templates | Feasible target | Feasible target | Console-specific template work |

The current container path cannot honestly be described as “everything
working” in a normal Windows Steam install. Docker Desktop's Linux-container
backend depends on WSL 2 or another supported virtualization backend; enabling
WSL for the first time can require elevation and a restart. See the current
[Docker Desktop Windows requirements](https://docs.docker.com/desktop/setup/install/windows-install/)
and [Microsoft WSL installation guide](https://learn.microsoft.com/windows/wsl/install).
Grand Leon should detect such a runtime and explain how to use it, but should
not silently install or own a privileged system service.

## Console target builders

The existing builders are excellent reproducible developer pipelines, but they
are not yet distributable application components. They assume a repository
checkout, Bash, `flock`, POSIX identity commands, a mounted repository, CMake,
and a Docker-compatible daemon. The toolchain images themselves do not contain
the complete retail builder.

There are three viable delivery levels.

| Direction | User experience | Windows | SteamOS | Cost/risk | Recommendation |
| --- | --- | --- | --- | --- | --- |
| Current repository service | Development setup | WSL/manual only | Manual runtime | Low code cost, poor product fit | Keep for development |
| Self-contained OCI builder images | One optional download plus installed container runtime | Docker Desktop/compatible runtime | Docker/Podman-compatible runtime | Large download, runtime and licensing burden | Useful fallback/prototype |
| Patchable console templates | Select target and export; no container | Native | Native | Most console-format engineering | Preferred product route |

### OCI fallback

If retained, a retail builder image must contain the pinned engine source,
cross compiler, SDK, disc/ROM tools, and one narrow entry point. It mounts only
an input directory containing `project.json` and assets plus an empty output
directory. It must not mount the installation directory, repository, home
directory, or container socket.

Steam can deliver an OCI image archive as ordinary files in an optional depot
or free DLC. Steam depots are logical file groups selected by operating system
and ownership, and DLC can be held in its own depot; see Valve's
[Depots](https://partner.steamgames.com/doc/store/application/depots) and
[DLC](https://partner.steamgames.com/doc/store/application/dlc) documentation.
That only delivers bytes. It does not install or authorize a container runtime,
so “containers as DLC” is a distribution mechanism, not a complete solution.

Before any image is redistributed, its complete transitive license and notice
inventory must be generated and reviewed. The present PlayStation container
already calls out Nugget, GCC runtime-exception source, and GPL-2.0 mkpsxiso;
the base images and all installed packages still need a machine-readable
software bill of materials and corresponding source/notice plan. No vendor
BIOS, signing key, licence sector, SDK, or other proprietary console material
may enter a builder or output.

### Patchable template direction

This is the route to console export that works in an ordinary Linux or Windows
Steam installation without Docker:

- **Nintendo 64:** build a fixed engine ROM with a versioned, aligned content
  slot. The creator compiles the project, validates a strict slot/art budget,
  writes content into the slot, fills unused bytes deterministically, and
  updates the cartridge checksum/header data. A template art pack must either
  contain every selectable asset or use independently patchable asset slots.
- **PlayStation:** build a fixed executable which loads `GAME.GPK` and selected
  assets as disc files. A small redistributable native disc-image writer then
  creates BIN/CUE output. This is likely the earlier win because content can be
  an ordinary disc file instead of bytes hidden inside a compressed
  executable.

Both template formats need a versioned capability manifest so a newer creator
can refuse an older template that lacks required engine features. Golden tests
must compare a template export with the existing full container build at the
package/runtime boundary and boot both images in the existing emulator gates.

## Steam packaging recommendation

Use operating-system depots for the main application and keep all ordinary
creation and play in them. Valve's compatibility guidance also requires any
launcher to be controller-navigable and recommends avoiding a required
launcher, which supports keeping Projects/Create/Play inside the game itself:
[Steam Deck and Steam Machine compatibility](https://partner.steamgames.com/doc/steamhardware/compat).

Recommended layout:

1. **Base Linux depot:** Godot creator/player, Linux native extension, Linux
   player-only export shell, shared art and templates.
2. **Base Windows depot:** Godot creator/player, Windows native extension,
   Windows player-only export shell, shared art and templates.
3. **Optional console-support depot/DLC:** patchable templates, or OCI archives
   only while the container fallback remains necessary, plus licenses.
4. **Workshop later:** portable project archives and art packs after validation,
   dependency, moderation, and compatibility rules exist. Workshop is a good
   distribution channel for user-created projects, not a console build system;
   Valve describes the required upload and load integration in
   [Steam Workshop](https://partner.steamgames.com/doc/features/workshop).

During development, a password-protected Steam beta branch is sufficient for
testing Windows and Linux packages. Valve documents branches as distinct
application builds intended for public or private testing:
[Branches](https://partner.steamgames.com/doc/store/application/branches).

## Acceptance gates

The native creator is ready to replace the earlier editor only when all of
these pass:

- every supported canonical field can be created, edited, saved, reopened,
  migrated, and exported without loss;
- map and campaign edits have deterministic undo/redo and recovery tests;
- creator output compiles byte-identically to the command-line compiler;
- a draft starts without a temporary file;
- portable archives contain no absolute paths or local configuration;
- player-only exports boot without creator UI on Linux and Windows;
- all essential creator actions are reachable with the simple PC control
  profile, while mouse and keyboard remain efficient alternatives;
- console target failures are isolated and never damage the source project;
- release bundles and reports pass private-path and secret scans.

## Next implementation commits

1. Extend the native creator into campaign routing, roster, item, weapon,
   ability, and art-pack surfaces.
2. Add a Windows GDExtension build and export smoke in CI.
3. Define and prototype the PlayStation external `GAME.GPK` disc template.
4. Define the Nintendo 64 content-slot manifest and measure realistic content
   and art budgets.
5. Integrate separately installed, versioned Nintendo 64 and PlayStation
   builder payloads into the native Export Center.
