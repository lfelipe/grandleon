# Current development TODO

0.5 is released. The next milestone is Windows support and graphics and
performance improvements across Godot, Nintendo 64 and PlayStation. Completed
items below describe the existing foundation; they are not being reopened.

## 0.6

### Windows builds

- [ ] Package the Godot creator and player for Windows with the native
  extension, required DLLs, pinned runtime and export templates. Users should
  not need a compiler or a separate Godot installation.
- [ ] Add Windows standalone-game export from the creator and verify that the
  exported game runs independently of the editor and source checkout.
- [ ] Run native tests and the complete create → export → play acceptance flow
  in Windows CI, with a runner that supports the required graphics and audio.
- [ ] Test the downloadable application on a clean Windows machine: saves,
  keyboard and controllers, audio, live language switching, and project paths
  containing spaces or accented characters.
- [ ] Provide and test the optional console-builder installation and launch
  path on Windows, with clear dependencies. Desktop editing and play must work
  without those optional tools. See [Setting up](docs/SETUP.md#windows-builds-for-06).

### Graphics and animation

- [ ] Close the PlayStation visual gap with Nintendo 64 and Godot on the board
  and in close combat. Compare the same real campaign scenes, including large
  maps and the castle, rather than relying on isolated close-up renders.
- [ ] Expand reviewed terrain textures, depth, material variety and connected
  scenery across terrain families. Refine joins and cluster variation beyond
  the existing foundation while keeping walkable boundaries readable.
- [ ] Expand thematic multi-tile landmark art beyond the current Medieval
  houses and keep: Sengoku, pirate, sci-fi and the other scenario styles need
  their own families. Add corresponding console variants, fences, gates,
  civic buildings, walls, bridges and other multi-cell features. Preserve the
  authored footprint and the editor's placement and preview workflow.
- [ ] Improve landmark silhouettes and terrain-matched backdrop variations on
  PlayStation, comparing equivalent views with Nintendo 64 and Godot.
- [ ] Adopt the genuine north/south idle and walk banks on Nintendo 64 where
  measured resident memory permits. The 112-body art foundation is complete;
  finish runtime compass coverage rather than regenerating the roster.
- [ ] Further polish movement, turns, planted feet, idle motion, attack,
  reaction and defeat animation on all three players. Preserve the accepted
  route, destination, event order and combat results.
- [ ] Expand ambient terrain motion on PlayStation without invalidating static
  terrain caches. Review water, banners and environmental details, and refine
  Godot/N64 ambient motion where it improves the board's readability.
- [ ] Add richer close-combat character animation and terrain packs beyond the
  existing forest, orchard, watch, village and castle coverage. Refine camera
  transitions, grounding, lighting and effects on all three players while
  keeping stable attacker/defender sides and a clean return to the board.
- [ ] Add optional subtle motion to combat and conversation backgrounds, such
  as water, foliage, weather, light or particles. Keep a static presentation
  available within each platform's budget.
- [ ] Polish graphical menus, portraits, text boxes, button hints, selection
  and movement highlights. Keep text readable with translations and panels
  clear of relevant units. Add compact non-controller icons only when a player
  test shows they communicate faster than the existing concise labels.
- [ ] Extend rendered hidden interiors beyond Twin Gate's single region to
  multiple authored connected rooms. Keep the underlying map, movement and
  units authoritative, with straightforward editor placement and previews.
  Gameplay fog of war and nighttime sight rules remain separate later work.

### Performance and memory

- [ ] Record the 0.5 production builds as baselines: artifact hashes, matched
  captures, frame timing, CPU/rendering cost, ROM/RAM and PS1 VRAM usage.
- [ ] Fix PS1 scenery-rendering cost and frame pacing on scrolling and castle
  views so the existing strict 30 fps checks pass. Retain the full terrain,
  units, camera behaviour and test scenarios; do not change gameplay to pass
  a performance test or weaken the checks.
- [ ] Improve console texture/palette residency and streaming where it buys
  visible quality. Measure upload, cache, geometry and memory costs before
  spending the recovered budget on richer scenery, characters or effects.
- [ ] Maintain the 30 fps console target and explicit memory limits, including
  base 4 MiB Nintendo 64 support. Improve quality within those limits rather
  than restoring retired renderers or adding silent fallbacks.
- [ ] Profile Godot's large-board, scrolling, ambient-animation and cinematic
  workloads, including on Windows, and fix observed rendering bottlenecks.

Recorded 0.5 PS1 baseline: the 600-update scrolling probe misses one display
deadline; the castle probe misses 46 and defers three renders. Re-measure the
current artifacts before comparing changes. These are 0.6 work, with the
strict checks retained.

### Review and release checks

- [ ] Review movement, two-axis scrolling, UI overlap, combat sides, castle
  entry/reveal, story and save/resume on the actual console builds. Complete
  the matched physical-target castle review on Nintendo 64 and PlayStation.
- [ ] Validate PS1 streamed poses on hardware, including identical archetypes,
  scrolling and the worst-case 30 fps budget. Check the expanded N64 poses too.
- [ ] Judge each graphics change from a normal exported campaign and retain
  its build hash, screenshots/video and measurements. Keep matched board,
  close-up and castle captures current across all three players.
- [ ] Preserve 1P/2P functional parity, saves, translations and audio while
  changing rendering or packaging. Continue hardware listening and controller
  feedback, and run the Windows and console export acceptance checks.

Campaign design, creator audio customization, AI translation suggestions and
mobile exports remain later milestones; they are not part of the 0.6 scope.

## Prior to 1.0: a polished demo campaign

- [ ] Design and playtest a cohesive campaign that introduces the rules through
  a gentle opening, builds tactical challenge across varied maps, and ends in
  a final boss battle. Give solo and local co-op distinct, balanced experiences.
  Treat the current Steel and Cedar campaign as a feature and translation test
  while this design work is planned.

## Creator audio customization

- [ ] Let authors import their own music and sound effects in the editor,
  preview them, and assign music to menus, maps, story scenes and boss battles.
- [ ] Allow distinct sounds for individual weapons and spells, including
  launch, hit, miss and healing cues synchronized with combat animations.
- [ ] Export the assigned audio with the game to Godot, Nintendo 64 and
  PlayStation, with clear file-format, duration and platform size guidance.
  Retain separate player Music and Effects volume controls.

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

- [x] Add gentle, independently phased idle breathing on all three players,
  without moving units off their cells. Reduced motion restores a still pose.
- [x] Add rooted foliage sway on Godot and Nintendo 64, keeping existing
  geometry. Godot water ripples share world coordinates across tile edges.

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
- [x] Evaluate directional board poses for the curated roster and make
  east/west travel follow the current authoritative route leg on Nintendo 64
  and PlayStation, matching Godot's existing camera-relative mirroring. The
  shared right-facing walk cells mirror at no texture-memory cost. PlayStation
  now uses compact authored north/south walk frames; Nintendo 64 chooses its
  left/right silhouette from the route's projected screen direction so a
  vertical leg no longer appears to walk backwards.
- [x] Establish the genuine north/south art pipeline with matching team masks,
  explicit profile metadata, Godot route-driven selection, fallback behavior,
  source audits, and measured compact-console projection. The current set
  covers all 112 bodies across all seven styles at 931,840 bytes of projected
  CI4 source payload per platform without changing current console executables.
- [x] Complete genuine north/south idle/walk coverage for all 112 curated
  bodies, reviewing identity, front/back readability, team tint, and planted
  walk phases for each. PlayStation enables compact north/south movement inside
  its measured RAM, VRAM, and frame budgets. The remaining Nintendo 64 compass
  bank adoption is scheduled for 0.6, with resident memory measured first.

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
Implemented console presentation work:

  - [x] Replace the flat top-down tactical board with a depth-bearing
    production view on both consoles. Start from the proven isometric textured
    terrain, elevation faces, grounded shadows, and recognizable Godot-derived
    character cutouts; do not bring back the unrecognizable low-poly character
    experiments or the floating castle cutaway.
    - [x] Nintendo 64 now uses one projected renderer for normal campaign and
      custom-project ROMs: a scrolling 10-by-8 view, filtered CI4 materials,
      shaded elevation faces, depth-ordered landmarks, projected shadows and
      Godot-derived cutouts. Tarnholt's independent framebuffer probe and the
      normal Steel and Cedar campaign flow exercise this same path. The flat
      board, floating whole-castle art and their capture-only targets are gone.
    - [x] PlayStation now uses the same production rule for Steel and Cedar,
      Tarnholt, the demo, and arbitrary exports: textured diamond terrain,
      elevation faces, diagonal depth order, smaller Godot-derived cutouts and
      popup avoidance. The flat board and floating keep code, art, tests and
      build targets are gone.
  - [x] Judge console graphics from the ordinary release artifact, driven from
    title screen through save, story, real campaign movement, and combat. Record
    the reviewed ROM or disc hash and reject capture-only viewers as release
    evidence. Compare composition, material identity, unit silhouette, terrain
    depth, UI overlap, animation readability, and stable frame time against
    representative tactical games on each machine. The accepted Steel and
    Cedar ROM and disc were each driven through title, save, story and the real
    board; the N64 autopilot and depth probe pass, while the PS1 turn, campaign,
    resume, BIOS-disc and framebuffer gates pass inside their measured budgets.
  - [x] Evaluate Pyrite/Tiny3D as the N64 production runtime, including the
    libdragon Preview toolchain, real-RSP emulator and hardware path, generated
    scene data, ROM/RAM/frame budgets, and the cost of hosting Grandleon's
    existing simulation inside Pyrite's application shell. Adopt it only if a
    playable Steel and Cedar slice materially improves the real ROM. It is now
    the sole N64 player; the previous renderer and fallback were removed.
    - [x] Prove the runtime boundary on real packages and generated character
      cutouts. The full five-battle Steel and Cedar ROM holds its intentional
      30.00 fps target over a measured 120-frame window under accurate real-RSP
      ares in 4 MB mode, including denser terrain, campaign saves, localization,
      name entry, visibility, movement and close combat. This completed the
      adoption rather than creating a second selectable renderer.
  - [x] Establish the shared console UI kit with framed tactical action panels,
    focus rows, platform-correct button badges, owner accents, concise labels,
    and health meters on Nintendo 64 and PlayStation. It uses only bounded flat
    primitives and the resident font, preserves the board viewport, and moves
    action panels away from the selected character.
  - [x] Carry the same visual grammar through console title and launch, story,
    save, help, company inventory, aftermath, Stage selection, character
    information, confirmation, and ending screens. Story panels fit their
    actual text instead of hiding the unused part of the backdrop, and button
    names use each machine's own controller vocabulary.
  - [x] Improve console terrain in both tactical and close views with a first
    deterministic composition pass: stronger material identity, stable
    variants, cleaner neighbour transitions, clustered scenery, elevation
    cues, and terrain-specific close-view composition. The shared rules cover
    all 13 terrain kinds while preserving walkable-cell clarity. The N64 pass
    adds no packaged texture payload; the PlayStation keeps its existing VRAM
    allocation and remains inside its measured frame budget.
  - [x] Set both console players to a stable 30 fps presentation target and
    spend the recovered budget on visible quality. Nintendo 64 now uses
    filtered atlas materials without cross-cell texture bleed plus denser
    scenery; PlayStation uses more frequent, more detailed native terrain
    forms and a full-frustum close-combat floor while keeping controller input
    and rules at 60 Hz. Exact-emulator probes measure the resulting frame
    budget rather than assuming it.

### Follow-up from 0.4 hardware review

The 0.4 PlayStation and Nintendo 64 builds are accepted for release. Carry
these observed issues into the next console pass rather than holding that
release:

- [x] Skip the pre-battle **Company Musters** screen when the ready formation
  leaves the player with no meaningful roster, stage, loadout, or deployment
  choice. Apply the same rule consistently in Godot, Nintendo 64, and
  PlayStation; retain the screen only when an authored decision exists.
- [x] Tighten PlayStation and Nintendo 64 board navigation so selecting a
  character, choosing an action, moving between legal cells, cancelling, and
  ending a turn feel predictable on each controller.
- [x] Reduce tactical unit scale on both consoles so characters remain distinct
  without obscuring the map or nearby cells.
- [x] Slow close-up combat timing on both consoles enough for the attack,
  impact, damage, and counter-attack to be read comfortably on hardware.

## Unified demo campaign

Steel and Cedar is the current shared feature and translation demo. Proper
campaign design and playtesting remain pre-1.0 work. The tasks below concern
story, teaching, balance and map design; the remaining landmark art and visual
polish are in [0.6](#06).

- [ ] Design and build one cohesive campaign to replace the current included
  campaigns as Grandleon's main demo on Godot, Nintendo 64, and PlayStation.
  Treat this as game design work rather than a content checklist, and do not
  ship it until both the one-player and two-player routes are enjoyable.
- [ ] Build a focused Medieval-versus-Sengoku story with a small purposeful
  cast. Introduce only the roles the campaign can teach and use well; the demo
  does not need to force every art style or archetype into one plot.
- [ ] Start with a short, forgiving scenario that teaches movement, terrain,
  attacks, counter-attacks, objectives, and ending a turn through play. Add
  mechanics gradually across later chapters instead of opening with a manual
  or a large roster.
- [ ] Create a deliberate difficulty curve through varied objectives, enemy
  roles, reinforcements, positioning problems, and meaningful terrain. Finish
  with a memorable final-boss encounter that tests the campaign's lessons
  rather than relying only on inflated health or damage.
- [ ] Design each map around its fiction and tactical purpose. Use coherent
  terrain regions, readable routes, flanks, cover, chokepoints, landmarks, and
  objective spaces; remove random-looking terrain patches and avoid repeating
  the same central-lane layout.
The current feature baseline includes editor-placeable 2×2 groves and houses,
four Medieval house variants, and matching blocked footprints on all three
players. Twin Gate has a connected 6×6 castle with doors, a throne room and an
18-cell interior reveal. The remaining art families, room expansion and
physical-target visual review are scheduled in [0.6](#06).

- [ ] Tune 1P and 2P separately. Co-op must give both players useful characters
  and decisions without merely doubling the battle size; solo play must retain
  the same strategic possibilities at a manageable pace. Playtest every map
  in both modes, including loss, retry, save, and continuation paths. The
  deterministic baseline now wins all ten variants after losing all ten before
  tuning: the tutorial ends with the full company, combat begins by round two,
  no trace has more than one quiet round in a row, both co-op players perform
  at least one quarter of their side's combat actions, the active Watch reaches
  round 20 while passive waiting loses, and the late battles finish with one or
  two survivors. Treat those numbers as guardrails, not a substitute for the
  pending human sessions.
- [x] Add a deterministic campaign balance baseline that uses authored enemy
  behaviour, proves every 1P/2P battle reaches its objective, checks scrolling
  conflict extents, opening-group distribution, dead-turn gaps, co-op combat
  participation, explicit survival-wave orders, and final-castle access, and
  pins broad difficulty signals without claiming that automation proves fun.
- [x] Run the actual candidate through the Godot Campaign and Map editors, not
  only source-level fixtures. The focused smoke imports Steel and Cedar through
  the project library, compiles direct 1P/2P tests for all five battles, proves
  the Watch previews have distinct western/eastern starts and unique roster
  identities, and checks all 23 staggered arrivals through the real force setup.
- [ ] Keep the campaign within the real Nintendo 64 and PlayStation budgets
  from the beginning. Validate the same source on all three players, then add
  automated completion routes and human playtest notes for difficulty,
  duration, clarity, dead turns, and dominant strategies before it replaces
  the existing demos.

### Later gameplay visibility and exploration

Rendered room expansion is in [0.6](#06); the gameplay changes below remain
later work.

- [ ] Later, let authors enable the same visibility foundation for ordinary
  maps, including nighttime scenarios with limited sight. Treat visual fog,
  player knowledge and gameplay rules as separate choices: define sight
  radius and blockers, per-player or shared co-op exploration, enemy and
  objective visibility, persistence in saves, AI knowledge, editor preview,
  and equivalent behavior on every renderer before making it a gameplay rule.

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

- [x] Add a clear **Game settings** screen for default turn order, permanent or
  recoverable character loss, an invulnerable testing mode, and controlled
  content-revision updates. Also expose per-battle turn-order overrides.
- [x] Add straightforward opening-force and reinforcement-wave editing,
  including arrival round, repeat cadence, wave count, hold or pursue
  behaviour, co-op-only forces, 1P/2P layout previews, alternate co-op starts,
  and a focused two-player opening-story action.
- [x] Complete advanced battle setup: draw numbered patrol paths on the map,
  set a reachable deployment capacity with company-size guidance, and order
  reusable scenes for the battle opening or when a named character falls.
- [x] Let an author playtest the currently selected battle directly, including
  later battles, without having to play from the campaign entrance. Direct 1P
  and 2P tests use the authored battle inside a clean save-isolated preview and
  return explicitly to the campaign editor; whole-campaign testing remains
  available for progression checks.
- [x] Add combat-design tools for weapon strengths and weaknesses, damage and
  accuracy bonuses, a two-character matchup preview, and a visual preview of
  attack range and area shapes.
- [x] Support more than one campaign in a project without silently editing,
  testing, playing, or exporting only the first campaign. The campaign outline,
  project play list, Linux build, and console builders all carry an explicit
  campaign identity; a portable project continues to keep every campaign.
- [x] Add safe deletion with a dependency preview, **Where used** summaries,
  and search and filtering for projects with large rosters or content lists.
  - [x] Build the shared dependency index and fail-closed deletion decision for
    campaigns, battles, maps, rosters, roles, factions, gear, dialogue, and
    presentation.
  - [x] Expose named, confirmation-based safe deletion for project-owned maps,
    gear, abilities, and reusable story scenes. Add search for projects,
    rosters, gear, story scenes, and translation catalogs.
  - [x] Apply the same dependency preview consistently to campaign routes,
    battles, roles, and factions. Terrain art, character bodies, themes, and
    backdrops are shared library choices: authors can replace them, but the
    editor does not pretend they are project-owned definitions that can be
    deleted. Add safe deletion if project-owned presentation definitions are
    introduced later.
- [x] Add a guided character flow that creates a useful character, role,
  appearance, side, weapon, and starting loadout together, with an optional
  randomized starting point.
- [x] Finish story-node sequencing outside battles: attach and detach multiple
  reusable scenes around a story node, show their numbered real play order,
  and move scenes earlier or later without editing portable source by hand.
- [x] Add human-readable project migration and recovery: explain an upcoming
  schema update, make a backup before rewriting, and preserve an unreadable
  source file so an author can recover or report it. Listing and search are
  read-only; project cards warn before the 1.3 → 1.4 update, opening keeps the
  original source in project recovery data, and completed recovery remains
  visible.

Browser-only infrastructure is deliberately not a Godot requirement. This
includes the PWA shell, IndexedDB storage, browser-side WebAssembly play mode,
HTTP ROM client, raw schema-record editor, and arbitrary terrain identifiers
without a matching art workflow. Godot should provide the equivalent author
outcomes through its native project library, player, and export tools rather
than reproduce those implementation details.

- [x] Add an end-to-end editor acceptance test that begins with an empty project
  library, uses the normal **New Campaign** workflow to author a small complete
  campaign, validates and exports it, then launches the exported player-only
  game and wins its first battle through normal controls. Cover authored
  terrain, units, teams, deployment, dialogue, victory rules, and player-count
  policy so this proves the full path from creation to play rather than export
  alone.
- [x] Let a campaign author choose one player, two players, or a choice at
  launch from the Campaign screen. Give immediate ownership guidance, disable
  impossible previews, and provide separate isolated **Test 1 Player** and
  **Test 2 Players** actions.
- [x] Carry the same player-count readiness checks into export, with focused
  tests for each policy and for a battle that is missing a Player 2 character.

### Localization

- [x] Localize Grandleon's own Godot player and editor. Use stable message
  identifiers, an explicit source language, predictable fallback, fonts and
  layouts that survive longer translated text, and language selection that is
  available before a project is open.
  - [x] Ship the first end-to-end shell slice in English, Spanish, and
    Brazilian Portuguese: a
    persistent language picker on the home and project screens, stable message identifiers,
    explicit English source text, deterministic English fallback, and localized
    home/player-launch and creator-project screens. Use a supported system
    locale only on first launch, persist the user's explicit override, and
    switch either open shell live without restarting or losing the project list.
  - [x] Move every current creator screen into the same catalog and audit longer
    translations, focus order, and compact layouts. English, Spanish, and
    Brazilian Portuguese now have exactly the same non-empty message IDs and
    formatting placeholders; the localization smoke test rejects catalog drift.
  - [x] Move the Godot player's session menu, display settings, checkpoint
    flow, diagnostics, and dialogue-continue prompt into the same catalog, with
    a Portuguese smoke test over the real player controls and compact layout.
  - [x] Move the Godot turn chip, unit card, action popup, contextual controls,
    controller ownership, and common command refusals into the same catalog;
    exercise the Portuguese battle HUD in the real player smoke test.
  - [x] Move the remaining Godot campaign outcomes, deployment notices,
    connection changes, optional Company screen, and uncommon error copy into
    the same catalog. Exercise the full Company workflow in Portuguese and
    keep exact catalog and placeholder parity under the localization smoke test.
- [x] Add portable project data for a game's source language and translations.
  Migration must preserve every authored string, and runtimes must fall back to
  the source text when a translation is missing rather than showing an empty
  label or an internal identifier.
- [x] Add the first focused **Translations** editor that gathers player-facing
  source copy, dialogue, stage and campaign labels, content names, and character
  names. It supports manual language codes, a first-class Brazilian Portuguese
  shortcut, source/translation preview, and missing/outdated filters.
- [x] Add lossless gettext `.po` export and import to the **Translations**
  screen so human translators can review a catalog in familiar tools. Keep the
  portable project catalog as the runtime source of truth for Godot, Nintendo
  64, and PlayStation; preserve stable message IDs, source text, review state,
  language tags, and named placeholders across the round trip. Stable IDs use
  gettext context, outdated reviews remain `fuzzy` with their prior source,
  and imports reject the wrong language or missing named placeholders.
- [x] Complete the final **Translations** gathering audit for every
  player-facing field in the current schema: project, campaign and stage text,
  dialogue, objectives, named battle placements, content names, and character
  names. Authors can filter missing or outdated entries and enter translations
  manually without hunting through the rest of the project. Internal route
  identities and author notes are deliberately excluded; the current schema
  does not yet define player-facing choice labels or content descriptions.
- [x] Treat character names as translatable authored text rather than assuming
  every proper name is universal. An author can save the same spelling as the
  reviewed translation when a proper name is deliberately shared.
- [x] Let authors preview the game in a selected language and validate
  completeness. Authored catalogs travel through the portable project and
  compiled package, the Godot player offers the authored languages before
  launch, and it localizes the project title, campaign/stage names, dialogue,
  objectives, company character names, named battle placements, unit types,
  weapons, abilities, and items with current-source fallback, including the
  console and shell surfaces. The workflow stays fully useful offline and
  without an external service.
  - [x] Add **Play Language** to the Translations screen with an explicit
    campaign and valid 1P/2P choice. It compiles the current project into a
    save-isolated preview, launches directly in the selected language, and
    returns to the same translation workflow.
  - [x] Reconnect authored-language selection to the new Pyrite/Tiny3D and
    Mipsync players. Godot, Nintendo 64, and PlayStation can now change among a
    game's authored languages from the live Settings menu, immediately refresh
    already-loaded title, stage, objective, dialogue, company, and named-content
    copy, preserve missing/outdated current-source fallback, and remember the
    choice without requiring a restart. The PS1 probe exercises an actual
    translated campaign disc and memory-card restore; N64 and Godot builds
    exercise their package-backed path without the retired renderer.
  - [x] Finish the compact-console interface-copy and glyph audit. Title,
    launch, help, pause, Settings, naming, tactical actions and results,
    deployment, aftermath, errors, and authored game copy follow the selected
    language. Automated checks keep English, Spanish, and Brazilian Portuguese
    inside the shared console glyph set and the measured PlayStation panels.
- [ ] Later, explore optional translation suggestions from an external AI model.
  Suggestions must be explicitly requested, remain visibly unapproved until a
  person accepts them, preserve source and manual translations, and never be a
  requirement for localization or export.

### Player-named characters

- [x] Let an author mark specific characters as player-nameable while requiring
  a normal authored default. The translated default appears until the player
  chooses another name and remains the safe fallback when naming is skipped.
- [x] Carry the chosen name through dialogue and objective substitutions,
  rosters, saves, campaign continuation, co-op views, and every export target.
  Player-entered names are never sent for translation; validate length, glyphs,
  duplicate-name clarity, and each platform's text-entry limitations.
  - [x] Save and restore an eligible character's chosen name in Godot, keep the
    authored and translated defaults separate, validate portable UTF-8 input,
    and fail closed on invalid or unauthorized save data.
  - [x] Apply chosen names to exact Godot battle units and explicit dialogue or
    objective member tokens, and warn clearly about duplicate company names
    without forbidding a deliberate duplicate.
  - [x] Reconnect controller-driven player name entry to the new Pyrite/Tiny3D
    and Mipsync players, with an explicit 18-glyph shared-console limit,
    authored-default reset, Cancel-without-writing, fixed-name exclusion, and
    no repeated prompt when a save is continued. Both players store the name in
    the portable campaign state rather than renderer-only data. The N64 ROM
    cross-build exercises the sole Pyrite player; the PS1 emulator probe enters
    a name on a real campaign disc, reboots over the same memory card, restores
    it, and proves that Continue does not repeat the prompt.
  - [x] Apply the chosen literal to explicit member tokens on both compact
    console players after localization. Nintendo 64 uses it in dialogue,
    objectives, and its deployment roster; PlayStation resolves the same token
    from generated stable member identities. Unknown, empty, or malformed
    tokens remain visible. The PS1 campaign probe enters `A`, renders
    `Hold, A.`, then proves the saved name survives reboot.

## Cinematic battle presentation

The Nintendo 64, PlayStation, and Godot players now consume real hit and miss
events in terrain-derived close views. Damage comes from the
simulation, and a legal counter-attack arrives as the next authoritative event
with the roles reversed. Further art, animation and presentation work is scheduled in [0.6](#06).

- [x] Add the first optional Godot close-up: frontal attacker and defender,
  terrain-derived backdrop, reported damage or miss, authoritative counter and
  defeat order, board-only fallback, persisted on/off and reduced-motion
  settings, hold-to-fast-forward, and skip.
- [x] Hand the Godot close-up smoothly from the real board to the focused
  attacker/target stage and back to the authoritative settled board, with a
  calm crossfade under Reduced motion.
- [x] Make the preference a portable game setting authors can choose, while
  retaining the local player override because the view adds meaningful time to
  every exchange. Older games default to enabled, while an explicit local or
  command-line player choice remains final.
- [x] Cover multi-target abilities, special results, and rapid queued combat.
  Area attacks keep their initiating character planted while targets rotate
  through the other stage slot; restoration, defeat, endurance and item drops
  use authoritative event results; queued exchanges use a shorter but complete
  board handoff. The close view is localized in English, Spanish and Brazilian
  Portuguese.
- [x] Reuse the same data-driven backdrop pools for in-game conversations.
  A conversation on a map should derive a calm composition from the speakers'
  locations and nearby terrain; an author can choose an explicit backdrop for
  an off-map scene. Reserve clear areas for portraits, choices, and dialogue,
  keep one variant stable for the scene, and fall back cleanly when a platform
  does not have that backdrop pack. Godot now plays native stage-opening, Talk
  and character-fall moments over the board, anchors them to the exact authored
  placement, and shares the same forest, orchard, watch, village and castle
  pools with combat.
  Unsupported scenes retain the existing readable wash until more art pools
  are authored.

## Implemented PlayStation graphics foundation

The remaining visual and performance tasks are scheduled in [0.6](#06).

- [x] Add indexed textures and a shared stage palette, restore 64×64 compass
  poses, and page walking frames through shared VRAM slots.
- [x] Increase native terrain density and add material textures to existing
  forms, grouping triangle pairs by material.
- [x] Use 4-bit palettes for terrain materials while preserving 8-bit units.
- [x] Reuse compatible Godot panoramas for console close-combat backdrops.
