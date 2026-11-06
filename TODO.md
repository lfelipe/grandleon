# Current development TODO

This is the short list of active presentation work. These changes may improve
how a battle looks and moves, but must not change the authoritative grid,
terrain rules, routes, timing, or combat results.

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

## Character art and console experiments

- [x] Complete male and female high-resolution bodies for every intended
  archetype and style. All eight archetype pairs in all seven styles are
  complete and pass the shared compact-art-direction audit.
- [x] Add reusable team-colour masks to each complete curated roster so one
  body sheet can render the six faction colours without recolouring faces,
  hair, equipment, or effects.
- [x] Compare the new low-detail commander and rogue proxies on PlayStation and
  Nintendo 64 at real board scale. N64 keeps a separate reproducible prototype
  ROM after passing its frame target; PlayStation keeps the supported sprite
  renderer because the sixteen-unit 3D board reached only 18 fps. See
  [the console 3D experiment](docs/CONSOLE_3D_EXPERIMENT.md).

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

## 0.6 cinematic battle presentation

The Sevenfold N64 review ROM now proves the basic two-character composition,
terrain-derived backdrop, attack/counter motion, damage callouts, and frame
budget. It remains an isolated presentation prototype: it does not consume a
real combat result, offer a campaign setting, or make this section complete.

- [ ] Add an optional close-up attack presentation inspired by tactical RPG
  battle scenes. When enabled, an attack leaves the board view for a frontal
  attacker-and-defender view, uses the current terrain tiles to choose and
  compose the backdrop, plays the attack and any legal counter-attack, and
  shows the damage dealt with clear numbers.
- [ ] Make the cinematic a game setting because it adds meaningful time to
  every exchange. Support the normal board-only presentation when disabled,
  define how players skip or accelerate a running cinematic, preserve the
  simulation's authoritative event order, and test attacks with no counter,
  misses, critical or special results, defeat, and rapid repeated combat.
