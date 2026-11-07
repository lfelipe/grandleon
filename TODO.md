# Current development TODO

This is the short list of active presentation work. These changes may improve
how a battle looks and moves, but must not change the authoritative grid,
terrain rules, routes, timing, or combat results.

The checked items below are implemented. Unchecked items in **0.4 release
acceptance** block the release; unchecked items in later sections are ongoing
roadmap work.

## 0.4 release acceptance

- [x] Package the Godot 0.4 player/creator with the separate one-player and
  one-or-two-player campaigns, and pass the native and packaged integration
  suites.
- [x] Use the console renderer in the playable Nintendo 64 and PlayStation
  campaign builds, with campaign, co-op, save, disc, renderer, and packaging
  checks exercising those same executables. The superseded console renderer,
  its prototype viewers, and its generated art are no longer in the project.
- [x] Streamline the first-play path on both consoles. Choosing **1 Player**
  must lead straight to the save choice, when one is needed, and then the
  campaign. Do not put a controls page, company screen, roster picker, or
  deployment prompt in front of a fixed ready-to-play formation. Keep controls
  as a compact **Help** choice on the start menu and as contextual button hints;
  preserve management or deployment only when the authored game presents a
  real player decision. Exercise 1P, 2P, new-save, and continue paths in the
  final ROM and disc.
- [x] Build and verify the final ROM, BIN/CUE, and Godot package; publish the
  review branch; and copy the exact console images to the MiSTer for hardware
  review. Tag 0.4 only after the branch is approved and merged.

## Terrain joins and variety

- [x] Make transitions between neighbouring terrain scenes feel seamless.
  Check borders, foundations, height, lighting, and repeated props so adjacent
  tiles read as one landscape instead of separate blocks.
- [x] Add deterministic cluster-aware variants for groups of the same or
  closely related terrain. A group of forest tiles could grow taller trees or
  a denser shared canopy in its interior, while its outer tiles use smaller
  trees, gaps, roots, or shrubs to form a readable edge.
- [x] Apply the same idea where it helps other terrain families, without
  hiding walkable boundaries or placing scenery over a different terrain kind.
  The result must be derived from terrain identity, neighbours, and coordinates
  so the same map always produces the same scene.

## Character movement

- [x] Replace visibly linear tile-to-tile motion with a smoother presentation
  curve: short acceleration, steady travel, and deceleration at the destination.
- [x] Blend direction changes across route corners so characters turn into the
  next segment instead of snapping to a new facing.
- [x] Slow visual travel enough to read at tactical distance, then keep planted
  contacts, alternating weight transfer, body rise, terrain height, and walk
  frames in phase from distance travelled. Test short moves, long routes,
  simultaneous co-op movement, and interrupted or overlapping presentation
  events.
- [x] Preserve the simulation's authoritative route and destination. Smoothing
  may interpolate the picture between accepted states; it must never invent a
  position, shorten a route, delay input authority, or change a state hash.

## Character art and console graphics

- [x] Complete male and female high-resolution bodies for every intended
  archetype and style. All eight archetype pairs in all seven styles are
  complete and pass the shared compact-art-direction audit.
- [x] Add reusable team-colour masks to each complete curated roster so one
  body sheet can render the six faction colours without recolouring faces,
  hair, equipment, or effects.
- [x] Make every style, archetype and body used by Sevenfold available to the
  console campaign through the same curated cutout pipeline and team masks.
  Keep the tactical and close-view checks reproducible. See
  [Console renderer](docs/CONSOLE_RENDERER.md).
- [ ] Continue improving console graphics across releases: character readability,
  terrain depth and variety, camera composition, movement and attack
  animation, effects, UI clarity, close-up scenes, and measured performance on
  original-style hardware.
  - Replace text drawn directly over backgrounds with a small coherent console
    UI kit: readable panels, selection frames, button glyphs, icons, meters,
    focus states, and concise labels. Keep the board visible and use text where
    it communicates something useful rather than as the entire visual design.
  - Improve console terrain in both tactical and close views: stronger
    material identity, less repetitive tiles, cleaner neighbour transitions,
    clustered scenery, elevation cues, and terrain-specific battle backdrops.
    Preserve walkable-cell clarity and measure the memory and frame cost on
    each console.

## 0.4 player foundation and battle view

- [x] Add portable per-campaign launch policy for one player, two players, or a
  choice at launch. Carry it through source schema 1.3.0, migration, compiled
  packages, Godot, Nintendo 64, and PlayStation while preserving older package
  inference.
- [x] Make the Godot board the dominant surface. Replace the permanent side
  manual with a compact action strip and contextual unit card.
- [x] Place actionable popups from live board geometry so they avoid units,
  cursors, highlighted destinations and targets, and another player's menu.

## 0.5 editor workflow

### Web editor retirement audit

The retired browser editor was compared with the Godot editor before removal.
Godot already covers the everyday authoring path: projects and portable
archives, maps and terrain, units and deployment, dialogue, campaign routes and
outcomes, armies, gear, validation, playtesting, saves, and exports. The
remaining author-facing gaps are tracked here rather than preserving a second
editor and its separate browser runtime.

- [ ] Add a clear **Game settings** screen for default turn order, permanent or
  recoverable character loss, an invulnerable testing mode, and controlled
  content-revision updates. Also expose per-battle turn-order overrides.
- [ ] Complete advanced battle setup: enemy behaviour and patrol paths,
  deployment capacity, timed and repeating reinforcement waves, and scripted
  moments when a battle opens or a character falls.
- [ ] Let an author playtest the currently selected battle directly, including
  later battles, without having to play from the campaign entrance.
- [ ] Add combat-design tools for weapon strengths and weaknesses, damage and
  accuracy bonuses, a two-character matchup preview, and a visual preview of
  attack range and area shapes.
- [ ] Support more than one campaign in a project without silently editing or
  exporting only the first campaign.
- [ ] Add safe deletion with a dependency preview, **Where used** summaries,
  and search and filtering for projects with large rosters or content lists.
- [ ] Add a guided character flow that creates a useful character, role,
  appearance, side, weapon, and starting loadout together, with an optional
  randomized starting point.
- [ ] Finish story sequencing: order, attach, detach, and reorder multiple
  scenes around a story or battle, including mid-battle moments.
- [ ] Add human-readable project migration and recovery: explain an upcoming
  schema update, make a backup before rewriting, and preserve an unreadable
  source file so an author can recover or report it.

Browser-only infrastructure is deliberately not a Godot requirement. This
includes the PWA shell, IndexedDB storage, browser-side WebAssembly play mode,
HTTP ROM client, raw schema-record editor, and arbitrary terrain identifiers
without a matching art workflow. Godot should provide the equivalent author
outcomes through its native project library, player, and export tools rather
than reproduce those implementation details.

- [ ] Add an end-to-end editor acceptance test that begins with an empty
  project, creates a small but complete campaign through the normal human
  workflow, validates and exports it, then launches the exported game and
  plays its first battle to a deterministic objective. Cover authored terrain,
  units, teams, deployment, dialogue, victory rules, and player-count policy so
  this proves the full path from creation to play rather than export alone.
- [x] Let a campaign author choose one player, two players, or a choice at
  launch from the Campaign screen. Give immediate ownership guidance, disable
  impossible previews, and provide separate isolated **Test 1 Player** and
  **Test 2 Players** actions.
- [ ] Carry the same player-count readiness checks into export, with focused
  tests for each policy and for a battle that is missing a Player 2 character.

### Localization

- [ ] Localize Grandleon's own Godot player and editor. Use stable message
  identifiers, an explicit source language, predictable fallback, fonts and
  layouts that survive longer translated text, and language selection that is
  available before a project is open.
- [ ] Add portable project data for a game's source language and translations.
  Migration must preserve every authored string, and runtimes must fall back to
  the source text when a translation is missing rather than showing an empty
  label or an internal identifier.
- [ ] Add a final **Translations** step to the editor that gathers every
  player-facing string in one place: campaign and stage text, dialogue,
  objectives, choices, unit and item names and descriptions, and other authored
  labels. Let authors filter missing or outdated entries and enter translations
  manually without hunting through the rest of the project.
- [ ] Treat character names as translatable authored text rather than assuming
  every proper name is universal. Let an author translate a default name per
  language or deliberately mark it as shared unchanged across languages.
- [ ] Let authors preview the game in a selected language, validate completeness,
  and export all chosen languages through the same portable package. Keep the
  initial workflow fully useful offline and without an external service.
- [ ] Later, explore optional translation suggestions from an external AI model.
  Suggestions must be explicitly requested, remain visibly unapproved until a
  person accepts them, preserve source and manual translations, and never be a
  requirement for localization or export.

### Player-named characters

- [ ] Let an author mark specific characters as player-nameable while requiring
  a normal authored default. The translated default appears until the player
  chooses another name and remains the safe fallback when naming is skipped.
- [ ] Carry the chosen name through dialogue and objective substitutions,
  rosters, saves, campaign continuation, co-op views, and every export target.
  Player-entered names are never sent for translation; validate length, glyphs,
  duplicate-name clarity, and each platform's text-entry limitations.

## Cinematic battle presentation

The Sevenfold console renderer and the Godot player now consume real hit and
miss events in terrain-derived close views. Damage comes from the
simulation, and a legal counter-attack arrives as the next authoritative event
with the roles reversed. The remaining work is richer art and animation,
portable campaign authoring, and complete combat-outcome coverage.

- [x] Add the first optional Godot close-up: frontal attacker and defender,
  terrain-derived backdrop, reported damage or miss, authoritative counter and
  defeat order, board-only fallback, persisted on/off and reduced-motion
  settings, hold-to-fast-forward, and skip.
- [x] Hand the Godot close-up smoothly from the real board to the focused
  attacker/target stage and back to the authoritative settled board, with a
  calm crossfade under Reduced motion.
- [ ] Make the preference a portable game setting authors can choose, while
  retaining the local player override because the view adds meaningful time to
  every exchange.
- [ ] Expand the close view with richer terrain detail and character animation,
  then cover multi-target abilities, special results, and rapid queued combat.
- [ ] Reuse the same data-driven backdrop pools for in-game conversations.
  A conversation on a map should derive a calm composition from the speakers'
  locations and nearby terrain; an author can choose an explicit backdrop for
  an off-map scene. Reserve clear areas for portraits, choices, and dialogue,
  keep one variant stable for the scene, and fall back cleanly when a platform
  does not have that backdrop pack.
