# Current development TODO

This is the short list of active presentation work. These changes may improve
how a battle looks and moves, but must not change the authoritative grid,
terrain rules, routes, timing, or combat results.

The 0.4 release scope below is complete. Unchecked work is either the ongoing
console-graphics track or a later milestone; it is not a hidden 0.4 blocker.

## 0.4 release acceptance

- [x] Package the Godot 0.4 player/creator with the separate one-player and
  one-or-two-player campaigns, and pass the native and packaged integration
  suites.
- [x] Build Sevenfold's sprite and playable 3D campaigns for Nintendo 64 and
  PlayStation, retain the complete-roster diagnostic viewers, and pass the
  console campaign and renderer checks.
- [x] Keep the README, creator walkthrough, artwork guides, platform guides,
  private review page, controls, build commands, release number, and current
  limitations aligned with the builds reviewers receive.

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

## Character art and console 3D

- [x] Complete male and female high-resolution bodies for every intended
  archetype and style. All eight archetype pairs in all seven styles are
  complete and pass the shared compact-art-direction audit.
- [x] Add reusable team-colour masks to each complete curated roster so one
  body sheet can render the six faction colours without recolouring faces,
  hair, equipment, or effects.
- [x] Expand console 3D to all 112 style/archetype/body models and use them in
  playable Sevenfold campaign builds on N64 and PS1. Keep the complete-roster
  viewers as reproducible model, camera, and performance diagnostics. See
  [Console 3D](docs/CONSOLE_3D_EXPERIMENT.md).
- [ ] Continue improving console graphics across releases: model readability,
  terrain depth and variety, camera composition, movement and attack
  animation, effects, UI clarity, close-up scenes, and measured performance on
  original-style hardware.

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

- [ ] Let a campaign author choose whether a game is one-player, two-player,
  or allows the players to choose at launch through an ordinary editor control.
  The portable format, migration, package data, and launchers now exist; finish
  the human workflow with combination validation, ownership guidance, explicit
  one/two-player preview actions, and export-focused tests.

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

The Sevenfold N64 and PS1 3D campaigns and the Godot player now consume real
hit and miss events in terrain-derived close views. Damage comes from the
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
