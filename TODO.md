# Current development TODO

This is the short list of active presentation work. These changes may improve
how a battle looks and moves, but must not change the authoritative grid,
terrain rules, routes, timing, or combat results.

## Terrain joins and variety

- [ ] Make transitions between neighbouring terrain scenes feel seamless.
  Check borders, foundations, height, lighting, and repeated props so adjacent
  tiles read as one landscape instead of separate blocks.
- [ ] Add deterministic cluster-aware variants for groups of the same or
  closely related terrain. A group of forest tiles could grow taller trees or
  a denser shared canopy in its interior, while its outer tiles use smaller
  trees, gaps, roots, or shrubs to form a readable edge.
- [ ] Apply the same idea where it helps other terrain families, without
  hiding walkable boundaries or placing scenery over a different terrain kind.
  The result must be derived from terrain identity, neighbours, and coordinates
  so the same map always produces the same scene.

## Character movement

- [x] Replace visibly linear tile-to-tile motion with a smoother presentation
  curve: short acceleration, steady travel, and deceleration at the destination.
- [x] Blend direction changes across route corners so characters turn into the
  next segment instead of snapping to a new facing.
- [x] Keep feet, body bob, terrain height, and walk animation in phase, and test
  short moves, long routes, simultaneous co-op movement, and interrupted or
  overlapping presentation events.
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
- [ ] Compare the new low-detail commander proxy on PlayStation and Nintendo 64
  at real board scale, then decide whether the recognizability and frame budget
  justify expanding the console 3D roster.

## 0.5 editor and player setup

- [ ] Let a campaign author choose whether a game is one-player, two-player,
  or allows the players to choose at launch. Treat this as a complete authoring
  workflow rather than a presentation toggle: define controllable-side
  ownership, validate unsupported combinations, expose seat assignment and
  launch choices, preview both modes, migrate schema versions, and test each
  allowed configuration through play and export.

## 0.6 cinematic battle presentation

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
