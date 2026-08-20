# The authored roster

112 figures: eight archetypes, seven styles, both bodies. Each file is a table of
axis-aligned boxes in the figure's own space — the same eight integers per part
(`x0,x1,y0,y1,z0,z1,ramp,rung`) the generated console headers carry, plus a
`slot` and a `name` that only the tools here read.

These are **generated art**. They are meant to be regenerated, and the scripts
that shape them live beside them so they can be improved rather than replaced.

## The scripts, in the order they run

| | |
|---|---|
| `author_faces.py` | gives every figure a face band, measured off its own head |
| `normalise_units.py` | draw order and proportions the rules demand |
| `check_units.py` | the game's own mesh rules, over all 112 |
| `roster_comparison.py` | sprite beside solid, at matched size, for looking at |

`normalise_units.py` after any change that adds or moves a part; `check_units.py`
before believing anything.

## What has been decided, and why

**Faces are a rule, not an accident.** The roster comparison found this was the
largest single difference between a sprite and its solid: every sprite has eyes,
and at the shipped 60-degree pitch a head box shows the viewer mostly its *top*,
so a model has a bare plate where the sprite has a face. Fifty-one of the 112
units already carried a dark band proud of the head and read markedly better for
it; the other sixty-one did not, and nothing decided which was which. That was
the defect — not a missing rule but no rule at all.

Two things were measured while fixing it, and both are worth keeping:

- **Deeper is not better.** A band standing 8 units proud changes twice as many
  pixels as one standing 2, and looks worse: it stops reading as eyes and starts
  reading as a black slab across the face. The pixel count preferred the worst
  variant. Two units is what shipped.
- **The rung is chosen against the head, not fixed.** A darkest-rung band on the
  rogue's darkest-rung hood is a face nobody can see, and that is exactly what
  the first version drew. A dark head takes a light band and a light head a dark
  one, which is what `textures/README.md` already says about painting: value
  contrast is what makes a figure legible at this size, more than hue.

After the rule the bands cost about 3.7% of a figure's drawn pixels for twelve
triangles each.

**A claim that was made here and was wrong.** This said "no unit has an
invisible face". It was checked on eight units in three styles and stated about
all 112, and it did not hold: rendering every face-ish part across the roster and
removing it one at a time, **13 of 150 changed nothing on screen**. Three were
bands this tool wrote.

Two attempts to fix it geometrically both failed, and the way they failed is the
point. Placing the band two units in front of the *head* buried it inside face
boxes that reach further forward -- `sengoku/commander`'s band sits at z -7..-4
while its face box reaches -11. Placing it in front of everything in the head's
y-range moved the failure to three *different* units. Occlusion at this pitch
depends on the whole scene and the draw order, not on a comparison of two boxes.

`author_faces.py` now **renders the figure with and without the band and keeps it
only if the picture changed**, stepping it forward until it does. Zero of the
bands it writes are invisible. Nine authored face parts elsewhere still are --
`undead/healer`'s two eye lights sit behind its hood hollow, `undead/beast`'s eye
socket behind its skull -- and those are art to fix rather than a rule to
change.

## What the metrics say, and which of them to trust

Pixel-difference metrics say this roster is doing well: models differ from each
other by 94.9% against the sprites' 81.4%, and a role's two bodies by 68.9%
against 22.2%. Both are *better* than the sprites and both are close to
worthless — they measure whether two pictures differ, not whether a player can
tell what one is.

The measure that tracks what the eye reports: rescale each silhouette into a
common box and ask which sprite it overlaps best. If its own sprite wins, the
class reads.

| | reads as its own class | own-overlap |
|---|---|---|
| commissioned (hand-authored) | 33/56 (59%) | 0.672 |
| this roster | 15/56 (27%) | 0.602 |
| chance | 7/56 (12%) | |

**The hand-authored meshes are more recognisable than this roster**, which was
written to replace them. Worth saying plainly. Read it with one caveat: even the
commissioned figures beat their nearest rival by 0.005 overlap, a knife edge, so
the hit-rate amplifies very small margins.

**The metric is silhouette-only.** It cannot see a face band, a rung or a ramp,
because those sit inside the outline — removing every face band from the roster
changes it by exactly nothing. It is the right tool for asking whether a shape
reads as its class, and no use for judging the work inside that shape.

## What is still open

- **Silhouette — but not the way it first looked.** The medieval mage was picked
  as the plain case: its sprite is a cone, and the commissioned mesh appeared to
  keep that cone while the roster had a box for a head. Measured, none of that
  holds. The sprite is not a cone in width — it stays 0.78 to 0.94 of its widest
  nearly to the top, and the roster tracks that (0.73-0.87) while the
  commissioned mesh is far narrower (0.38-0.69) and so matches its own sprite's
  widths *worse*. On the class test the commissioned mage fails too: it reads as
  `commander` at 0.659 against 0.516 for its own sprite, where the roster reads
  as `commander` at 0.641 against 0.533.

  So mage-against-commander is not a defect in either mesh. It is what those two
  shapes are at this size — a robed figure with a tall thin thing beside it,
  a staff on one and a banner on the other. Whatever separates them is interior,
  and no outline work will do it.

  `taper_headgear.py` came out of that wrong premise. It adds a step where the
  figure's outline is thinner than its sprite's, fires on 56 figures, moved the
  mage from 0.513 to 0.533 and the roster as a whole not at all. It is kept
  because it is grounded in the sprite rather than in a guess, and it is not
  evidence that tapering was the problem.

- **Held objects are still the wrong shape.** A bow, a staff and a blade are
  thin and mostly silhouette, which is the one thing an axis-aligned box cannot
  be. Props already break the body outline by 7-35% of a figure's pixels on most
  units, so this is a shape problem rather than a visibility one.
  `clear_props.py` fixed the visibility half: ten units carried props breaking
  the outline nowhere at all — `sengoku/commander`'s sashimono pole contributed
  zero pixels — and they now clear it. A first version moved any prop sitting
  inside the outline, 37 of 112 units, and the measurements came back ambiguous;
  narrowed to units with *no* visible prop it touches ten and the metrics are
  flat. Kept because an invisible weapon is a defect whether or not a silhouette
  metric notices, not because a number improved. It did not.

- **Bodies do not need work.** The two figures of a role differ far more as
  models than as sprites. An earlier note here claimed the opposite; it was
  wrong, and the measurement above replaces it.

- **One bug worth remembering.** A sprite cell carries a faction disc under the
  feet, four rows of it, identical for every class. Reading a width profile from
  the whole cell maps the figure's feet to the bottom of that disc and shifts
  everything above by an eighth of the figure's height. Both `fit_silhouette.py`
  and `taper_headgear.py` had it; both now stop at `characters.GROUND_Y`.
