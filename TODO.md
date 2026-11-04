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

- [ ] Replace visibly linear tile-to-tile motion with a smoother presentation
  curve: short acceleration, steady travel, and deceleration at the destination.
- [ ] Blend direction changes across route corners so characters turn into the
  next segment instead of snapping to a new facing.
- [ ] Keep feet, body bob, terrain height, and walk animation in phase, and test
  short moves, long routes, simultaneous co-op movement, and interrupted or
  overlapping presentation events.
- [ ] Preserve the simulation's authoritative route and destination. Smoothing
  may interpolate the picture between accepted states; it must never invent a
  position, shorten a route, delay input authority, or change a state hash.

## Character art and console experiments

- [ ] Complete male and female high-resolution bodies for every intended
  archetype and style.
- [ ] Add reusable team-colour masks so one body sheet can render the six
  faction colours without recolouring faces, hair, equipment, or effects.
- [ ] Compare the new low-detail commander proxy on PlayStation and Nintendo 64
  at real board scale, then decide whether the recognizability and frame budget
  justify expanding the console 3D roster.
