# Grand Leon 0.3 release plan

`RELEASE_VERSION` is the product release version. The CMake project version,
source-schema version, package-format version, campaign content revision, and
save-envelope version are compatibility contracts with independent lifetimes;
they do not change merely because the product becomes 0.3.0.

Version 0.3 is the first release in which the Godot application is both the PC
player and the supported campaign creator. The release artifacts are:

- one Linux x86-64 PC bundle containing the player and creator;
- one Nintendo 64 ROM containing Emberbound;
- one PlayStation BIN/CUE image containing Emberbound;
- SHA-256 checksums and the exact clean source revision for every artifact.

Windows is an intended PC target, but it is not a 0.3 artifact. The SDL client
remains a tested reference front end, not the primary PC product. The earlier
browser editor is not a supported 0.3 authoring path and must disappear from
the user-facing documentation before release.

## Product principles

Every release decision should preserve these constraints:

1. Gameplay is quick, intuitive, and enjoyable before it is feature-rich.
2. Ordinary play and creation work with one directional control and a small
   set of buttons. Text entry may use a keyboard.
3. Information appears when it is useful. A permanent label must justify the
   space it occupies.
4. A project is portable source. It carries no username, hostname, absolute
   path, controller identity, credential, network endpoint, or local setting.
5. PC, Nintendo 64, and PlayStation consume the same campaign, encounter,
   co-op ownership, scaling, objectives, dialogue, and deterministic rules.
6. PC-only presentation can be richer, but it cannot change portable gameplay.
7. Optional local-model behavior is outside 0.3. Enemies use the deterministic
   built-in policy on every target.

## Current readiness

| Area | State | Evidence / remaining work |
|---|---|---|
| Shared engine and content compiler | Ready | All 68 native tests pass in release assertions. Source edits preserve integer tokens and revalidate complete projects. |
| Linux PC player | Candidate ready | The portable bundle launches its home screen, loads controls, verifies frame timing, and accepts two simultaneous co-op movement routes. Final artifact must be rebuilt from the release commit. |
| Linux PC packaging | Ready | A pinned Godot 4.7.2 runtime, godot-cpp 10.0.0-stable binding, 512-pixel icon, clean-source metadata, project/upstream licenses, and Godot's third-party inventory are included. Local configuration and checkout paths are rejected. |
| Nintendo 64 | Builds | Emberbound builds as a current `.z64` with SRAM persistence and shared two-controller/co-op content. Final emulator acceptance and a clean-revision rebuild remain. |
| PlayStation | Builds | Emberbound builds as a MIPS-I executable and BIN/CUE image. Mixed style/figure character art now uses a project-specific sparse table. Final emulator/controller/card acceptance and a clean-revision rebuild remain. |
| Player/console feature parity | In validation | Campaign flow, story, objectives, abilities, items, save data, co-op ownership, and two-player enemy scaling are portable. A target-by-target playthrough checklist still needs to be signed off. |
| PC graphics | Playable, not final art | Pixel, hybrid voxel-sprite, diorama, and full-voxel profiles are replaceable. The recommended initial profile is `voxel-sprites`; final animation/art polish is not a release-format dependency. |
| Native project library | Ready | Create, rename, copy, import, export, atomic save/recovery, and in-memory compile/play are covered by smokes. |
| Native map/battle creator | Useful vertical slice | Paint/fill/rectangle, placements, player ownership, co-op scaling, deployment, objectives, resize, and surroundings work. Add Next Chapter creates a story moment, cloned playable battle, and fresh map as one undoable operation. |
| Native story creator | Useful vertical slice | Existing lines can be edited, and a one-line scene can be created and attached. Visible line edits follow navigation and Save without a separate Apply trap. Adding/removing/reordering lines and richer scene/cast editing remain. |
| Full native authoring parity | Blocking | Characters, classes, factions, equipment, abilities, items, recruits/rewards, campaign branches, and presentation selection still need focused native editors. |
| Native Export Center | Blocking | Portable project export works, and command-line target builders work. The in-app target report/build experience is not implemented. |
| Documentation and screenshots | Blocking | README and `docs/CREATING_A_GAME.md` still describe the browser editor. Rewrite them only after the native flow and labels are frozen, then capture every documented screen from that build. |
| Windows PC | Post-0.3 | Needs a Windows GDExtension, export shell, signing/CI, controller validation, and a Windows-safe console-container strategy. |

The provisional artifacts under `out/` prove that each current target can be
built. They are not release artifacts: their source revisions predate later
0.3 work and they must not be published as final.

## Supported creator flow

The intended first campaign journey is deliberately short:

```text
Projects
  -> New Campaign
  -> Shape the first map
  -> Write the opening conversation
  -> Test the campaign
  -> Add Next Chapter
  -> reshape its map and battle
  -> add its conversation
  -> test again
```

New Campaign starts from a complete, playable project with heroes, opponents,
a goal, co-op ownership, a story moment, and a terminal ending. It does not put
an empty schema document beside the recommended route.

Add Next Chapter is intentionally linear. It is offered only after a playable
encounter with one unconditional exit. It inserts a story moment and a cloned
battle before the old destination, so authors begin from working characters,
opponents, ownership, scaling, deployment, and goals. It refuses a branched
exit rather than guessing. Branch creation belongs in the campaign-flow editor.

## Remaining editor increments

The order below keeps each increment playable and avoids building a giant form.

### 1. Finish scene writing

- Add, remove, and reorder lines with controller-safe focus.
- Add and remove cast members through named pickers.
- Preview backdrop and speaker portrait in the same screen.
- Make Save the only persistence action and preserve one-command undo.

Exit: a new chapter can receive a complete multi-line conversation without
editing JSON.

### 2. Grow the army

- A Characters screen lists the campaign company in plain language.
- Add a hero from a small class/archetype template, then edit name and core
  stats through bounded controls.
- Attach a recruit to a chapter and preview when that character joins.
- Add starting equipment and chapter rewards through searchable name pickers.
- Always show PC, Nintendo 64, and PlayStation budget impact before saving an
  addition that could exceed a target.

Exit: the five-stage Emberbound-style growth curve can be authored natively.

### 3. Finish battle data

- Focused editors for unit types, classes, factions, weapons, items, and
  abilities; raw stable IDs remain hidden during ordinary work.
- Rename maps and battles without changing their stable identity.
- Copy a battle setup from another chapter deliberately, instead of only when
  adding the next chapter.
- Surface compile diagnostics on the relevant character, placement, or field.

Exit: every portable battle field used by the shipped campaign is editable in
the Godot application.

### 4. Campaign flow

- A readable chapter list is the default; a graph is an advanced view.
- Add story, battle, and ending moments; reorder a linear route safely.
- Add outcome/inventory/world-state branches with named targets and preview the
  possible routes.
- Edit recruits, grants, and terminal endings at the chapter that owns them.

Exit: branching projects no longer require the earlier editor or hand-edited
JSON.

### 5. Presentation and replacement art

- Choose installed terrain/character/effect packs and preview the actual
  runtime result.
- Import project-owned replacements with explicit dimensions, pivots,
  animation names, and target budgets.
- Preserve independent PC assets and generated console derivatives without
  embedding machine paths.

Exit: an author can replace the initial art without changing gameplay source
or repository code.

### 6. Export Center

- Export a portable editable project.
- Build a player-only Linux game from a signed/prebuilt shell.
- Build Nintendo 64 and PlayStation targets through separately installed,
  versioned builder payloads.
- Report each target independently with warnings, source/content identity,
  builder version, art pack, and hashes.
- Never copy credentials, container state, absolute paths, or local settings
  into a project or report.

Exit: the installed PC application can create the three 0.3 target artifacts
without requiring a source checkout.

## Feature-parity acceptance

Parity means identical portable behavior, not identical pixels or menus.

For Emberbound, verify on PC, Nintendo 64, and PlayStation:

- the same five battles and story order;
- solo mode fields the complete allied company for player one;
- two-player mode gives each player their own units and fields authored
  two-player opponents;
- both players can keep navigating and submit work without waiting for the
  other player's cursor;
- every objective ends the round/battle correctly, including total defeat;
- deterministic opponents complete their turns;
- movement, weapon, ability, damage, defeat, and story presentation occur;
- save data resumes the same campaign standing and rejects incompatible data;
- no stage with fixed deployment asks an empty deployment question;
- the final boss reaches the terminal ending.

PC additionally verifies the home screen, creator, manual save slots,
presentation settings, replaceable graphics profiles, and clean exit. Console
clients additionally verify both controller ports and their native persistent
storage paths.

## Release gates

A final release commit must satisfy all of the following.

### Source and privacy

- Worktree is clean and the branch contains no setup-specific value.
- `rg` finds no local hostname, address, username, token, checkout path, or
  retired optional-model configuration in tracked files or artifacts.
- Generated art/schema files pass their drift checks.
- Every release commit is reviewable and describes one coherent change.

### Shared code

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGRANDLEON_WERROR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Linux PC

```sh
platform/godot/scripts/build-steamos.sh \
  --build-dir build-godot-release \
  --output out/grandleon-pc-0.3.0

platform/godot/scripts/smoke-pc-release.sh \
  --native-build build \
  --godot-build build-godot-release \
  --bundle out/grandleon-pc-0.3.0
```

The aggregate smoke validates the exact clean bundle. Then inspect the creator
screenshots and perform a one- and two-controller playthrough on the target
Linux PC. Building does not authorize launching on another machine.

### Nintendo 64

```sh
GRANDLEON_N64_BUILD_DIR="$PWD/build-n64-release" \
  platform/nintendo64/scripts/build-n64.sh \
  --project games/emberbound/source/project.json \
  --targets grandleon_n64_campaign
```

Run the repository's ares checks against the exact ROM, then record its hash,
size, save type, and clean source revision.

### PlayStation

```sh
GRANDLEON_PLAYSTATION_BUILD_DIR="$PWD/build-playstation-release" \
  platform/playstation/scripts/build-playstation.sh \
  --project games/emberbound/source/project.json \
  --targets grandleon_playstation_campaign

GRANDLEON_PLAYSTATION_BUILD_DIR="$PWD/build-playstation-release" \
  platform/playstation/scripts/build-disc.sh
```

Run the MIPS-I, heap-room, emulator, controller, save-card, disc, and
no-license-sector checks against the exact executable/image. The BIN/CUE is for
compatible emulators or development systems; it does not contain Sony's
license sector and is not represented as a stock-console disc.

### Documentation and artifacts

- Replace browser-editor README instructions with the native flow.
- Rewrite `docs/CREATING_A_GAME.md` from a clean user-data directory.
- Generate screenshots from the final UI and verify every caption/action.
- Write release notes with known limitations and compatibility changes.
- Rebuild all artifacts from the same clean release commit.
- Create `SHA256SUMS` and `SOURCE-REVISION`, verify hashes after packaging, and
  inspect archive contents before publishing.

## Deliberately outside 0.3

- local or network model-driven opponents, dialogue, or campaign generation;
- Windows artifacts and Windows-hosted console builder containers;
- uploading generated games to Steam or storing Steam credentials;
- stock PlayStation boot claims;
- visual equality between PC and the fixed-budget consoles;
- unrestricted project scripting or executable mods.
