# Native creator and export design

Version 0.3 makes Grand Leon's Godot PC application both the player and the
supported campaign creator. A person can begin with a working campaign, change
it, test it in the same process, and export it without opening a browser or
installing a development toolchain.

The release checklist and current test evidence live in
[`docs/RELEASE_0_3.md`](../../docs/RELEASE_0_3.md). This document explains the
creator's product flow, ownership boundaries, and remaining implementation
work. The earlier browser editor is retained only as a migration and
compatibility test surface while 0.3 is prepared.

## Product rules

The native creator follows the same rules as the player:

1. Start from something playable, not an empty data model.
2. Keep the common path visible and put uncommon controls behind one focused
   action.
3. Make every essential action reachable with directions, Confirm, and Cancel.
   Text entry may use a keyboard.
4. Test the current draft quickly and without touching a player's save.
5. Keep source portable across PC, Nintendo 64, and PlayStation. Richer PC
   presentation cannot change shared rules.
6. Preserve stable identities when a player-facing name changes.
7. Validate a complete candidate before saving it, and make one author action
   one undoable command.
8. Never place a username, hostname, absolute path, controller identity,
   credential, private endpoint, or local setting in a project or export.

Enemy behavior in 0.3 is the deterministic built-in policy on every target.
Local-model behavior is outside this release.

## Supported creation loop

The ordinary flow is deliberately short:

```text
Projects
  -> New Campaign
  -> shape the first map and battle
  -> write the opening conversation
  -> Test Campaign
  -> Continue Creating
  -> Add Next Chapter
  -> reshape its copied battle
  -> grow the army
  -> test again
  -> Finish & Export
```

**New Campaign** asks for one player-facing name and creates a complete starter:
two heroes, opponents, co-op ownership and scaling, a goal, a conversation, an
automatically surrounded map, and an ending. It opens the first map immediately.

**Add Next Chapter** is the fastest linear path. It creates a story moment, a
fresh map, and a battle copied from a known-working encounter before the old
destination. Units, objectives, deployment, player ownership, two-player
scaling, and turn rules remain playable and can be reshaped. The whole addition
is one validated undo command.

**Continue Creating** opens a campaign outline in play order. Story, battle,
and ending rows open the exact focused editor for that chapter. **Outcomes**
edits named destinations and first-match order. **Add Route** can connect an
existing chapter, make a named ending, or create a complete alternate playable
story/map/battle that rejoins a selected destination.

Draft play compiles source in memory and starts the ordinary PC runtime. It
does not write the installed campaign's checkpoint or require a temporary build
directory.

## Implemented surfaces

### Projects and persistence

- Create a complete starter, rename it, copy it with fresh identities, and
  continue or test it from controller-focused project cards.
- Save through a validated temporary file and atomic replacement, with bounded
  recovery snapshots and the last known-good source retained after failure.
- Import and export deterministic `.grandleon.zip` archives containing only
  `grandleon.json`, canonical `project.json`, and optional project-owned assets.
- Refuse compressed or unsafe entries, traversal, case-colliding names,
  machine-local configuration, oversized expansion, checksum mismatches, and
  project-owned symlinks.
- Turn an imported identity collision into an explicit copy instead of
  replacing another library project.

### Map and battle

- Create maps; paint with brush, fill, and rectangle tools; preview the actual
  portable terrain art; and undo or redo complete edits.
- Add, move, and remove allied or opposing placements. Assign allied units to
  player one or player two and mark extra opposing units for two-player mode.
- Edit deployment regions, capacity, and defeat/protect/survive objectives.
- Resize with a clipping preview and refuse a shrink that would strand a
  placement or deployment tile.
- Choose solid-colour, automatically derived, or manually painted non-playable
  surroundings without changing the tactical board.
- Rename a map and battle together without changing either stable identity.
- Deliberately copy a battle setup from another chapter while retaining the
  target map, flow, name, and identity.
- Test the selected encounter or grow a linear campaign with **Add Next
  Chapter**.

### Story

- Create and attach a scene, edit its name and backdrop, and preview the same
  portable backdrop wash used in play.
- Add, edit, reorder, and remove dialogue lines while always retaining one
  line.
- Choose a named speaker/character and maintain the portable cast and portrait
  together.
- Commit typed text on navigation, save, test, or exit so there is no separate
  Apply trap.

### Army, roles, gear, and progression

- List founding heroes and later recruits in story order, rename without
  changing identity, and choose when a recruit joins.
- Add a character from an existing role or create an independent role by
  copying a working baseline.
- Tune the complete portable role baseline, side, traversal, post-attack rule,
  allowed weapon families, and focused personal modifiers.
- Create named sides and weapon/item families without exposing source IDs.
- Create reusable weapons, items, and abilities from safe defaults or a working
  copy, showing only fields relevant to the selected kind.
- Assign compatible role gear and skills, opening supplies, and chapter
  rewards.
- Keep experience, level thresholds, growth chances, item drops, and per-role
  appearance in the separate **Growth & Rewards** screen.

### Campaign flow

- Read the main route and alternate outcomes as named story, battle, and ending
  rows rather than a source graph.
- Edit **Always**, **Goal Won**, **Goal Lost**, and **Have Item** routes,
  destinations, first-match order, and removal.
- Create and rename terminal endings.
- Create a complete alternate playable branch from a working encounter and
  make a lone unconditional path into reachable complementary win/loss routes.
- Preserve imported advanced conditions that the focused editor does not yet
  create.

### Presentation and export

- Choose the shared terrain theme and character style while previewing actual
  assets used by the ports.
- Export a portable editable project on every installation.
- Export a player-only Linux game from a verified installed PC shell. The
  export is written atomically, contains compiled content rather than creator
  source by default, strips creator code/templates/saves/local settings, and
  reports portable names and hashes.
- Show Nintendo 64 and PlayStation as independent targets which remain disabled
  until compatible builder payloads are installed.

## Architecture and ownership

```text
canonical project source
          |
          | GrandleonProjectCompiler: parse + validate + compile
          v
      package bytes
          |
          +-> GrandleonSession: test the draft in the ordinary runtime
          +-> player-only PC shell
          +-> installed console builder payload
```

The bridge accepts source text and returns bytes or structured diagnostics. It
does not accept a build-directory path or launch a compiler process. That
boundary is portable across Linux and Windows and cannot record the compiling
machine in authored content.

Authoring commands live in the shared native source layer where an operation
can preserve integer tokens, stable identities, unknown supported fields, and
complete-project validation. Godot owns the focused interaction and preview;
the canonical schema and compiler own meaning.

Projects live below Godot's platform user-data directory, never beside the
installed executable:

```text
user://projects/<project-id>/
    project.json          canonical portable source
    assets/               project-owned source assets
    recovery/             bounded local recovery snapshots
    project.local.json    optional local-only preferences
```

Only `project.json` and `assets/` may enter a portable archive. Window layout,
recent selections, controller bindings, exports, test state, and saves remain
local.

## Schema and compatibility

The canonical JSON schema is the contract between the creator, compiler, PC
player, and console ports. The creator must never silently discard a field it
does not understand.

Current focused screens use named operations layered over full-project parsing
and compilation. Source and compiler diagnostics already include semantic paths;
the remaining UI work is to route a failure to the relevant field, character,
placement, or chapter instead of showing only a footer message.

Older schema migrations remain covered by the existing migration fixtures. A
newer unsupported schema must open read-only with an explicit explanation; an
older schema must migrate into a copy before changing the original. A generated
Godot-readable field/default manifest would reduce duplicated field descriptions,
but it must not become a second schema authority.

## Remaining 0.3 editor work

These are release blockers in priority order, not invitations to add a large
permanent form.

1. **Measured target budgets.** Define checked PC, Nintendo 64, and PlayStation
   authoring budgets, then show the impact before an addition that can exceed a
   target. PC success must remain independent of a console refusal. The existing
   Nintendo 64 runtime limit of 48 fielded units is real evidence; PlayStation
   limits still require an equally explicit measurement and gate.
2. **Actionable diagnostics.** Translate compiler semantic paths into the exact
   focused screen and selected control/map object while preserving the full
   diagnostic for technical inspection.
3. **Advanced flow operations.** Add safe whole-chapter reordering and focused
   world-state, negated, and compound conditions. Keep these under **Outcomes**;
   the common linear path must not become a graph editor.
4. **Replacement art packs.** Select an installed pack or import project-owned
   terrain, character, portrait, effect, and UI assets. Validate dimensions,
   pivots, animation names, target budgets, and licenses, and preview the same
   animation state machine used in play.
5. **Console builder payloads.** Integrate separately installed, versioned
   Nintendo 64 and PlayStation builders into the Export Center with independent
   progress, warnings, capability checks, hashes, and failure reports.
6. **Release sign-off.** Freeze labels, regenerate the real creator screenshots,
   run clean-revision PC/Nintendo 64/PlayStation gates, and complete one human
   solo and co-op campaign playthrough per target.

Windows PC support is post-0.3. It requires a Windows GDExtension, export shell,
signing and CI, controller validation, and a Windows-safe strategy for any
remaining console builder dependency.

## Standalone PC games

A project can become a player-only game that does not expose creator UI and
does not require Grand Leon to be installed. The installed creator copies a
verified player shell, replaces its compiled package and launch manifest,
includes selected runtime art and audio plus licenses, then makes the completed
directory visible only after validation succeeds.

Editable source is excluded by default and can be included only by an explicit
author choice. The exported game must not contain the creator scenes, source
templates, another player's saves, local preferences, checkout paths, or Steam
credentials.

Other operating systems use separately versioned player shells. Cross-platform
export is therefore a payload installation problem, not a reason to bundle a
compiler or Godot editor into every generated game.

## Console builder payloads

The current repository scripts are reproducible developer pipelines, not
retail application components. They assume a checkout, Bash, CMake, POSIX
utilities, and a Docker-compatible daemon. The installed application must not
mount a home directory, source checkout, installation directory, or container
socket into an opaque builder.

Two payload approaches remain viable:

| Direction | User experience | Tradeoff |
|---|---|---|
| Self-contained OCI builder | Optional large download plus an installed container runtime | Useful prototype/fallback, but carries runtime, licensing, and Windows setup burden |
| Patchable console template | Select a target and export natively | Best product flow, but requires strict versioned content/art slots and format-specific tooling |

The preferred product direction is a versioned patchable template:

- Nintendo 64 uses an aligned content/art slot with explicit capability and
  size budgets, deterministic padding, and cartridge checksum/header updates.
- PlayStation loads `GAME.GPK` and selected assets as disc files, then a small
  redistributable writer creates BIN/CUE output.

Golden tests must compare template output with the existing full builder at the
package/runtime boundary and boot both through the established emulator gates.
Payloads need complete licenses and machine-readable dependency inventories.
No BIOS, signing key, license sector, proprietary SDK, credential, or private
configuration may enter a payload or artifact.

## Acceptance gates

The native creator is ready for 0.3 only when:

- every field required by Emberbound can be created, edited, saved, reopened,
  and exported without hand-editing JSON;
- focused map, story, army, gear, and campaign commands have deterministic
  undo/redo, persistence, and compilation tests;
- draft play starts without a temporary source or package file;
- portable archives and player-only games pass private-path, local-setting,
  source-stripping, and secret scans;
- all essential creation actions work with the simple PC control profile;
- a console budget/export failure cannot damage source or block PC export;
- final PC, Nintendo 64, and PlayStation artifacts come from one clean revision
  and pass their complete automated and human release checks.
