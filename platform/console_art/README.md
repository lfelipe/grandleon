# Shared console character art

The Nintendo 64 and PlayStation builds generate their character cutouts from
the same curated high-resolution sheets and team-colour masks as Godot. Run
`generate-character-art.py` with a project source and a platform name; the
output belongs in a build directory, not in source control.

Each current body sheet is a 4×2 animation sheet: two idle cells, two walking
cells, wind-up, follow-through, ability and hurt. It has one native
right-facing view. The renderers use that view for eastward travel and mirror
it for westward travel. This costs no additional texture memory and keeps the
current character ROM, RDRAM/VRAM and frame budgets unchanged.

There are no authored north- or south-facing cells in the current 112-body
roster. Vertical route legs therefore retain the last horizontal silhouette;
the route and destination still come directly from the simulation. We do not
stretch, squash or relabel a side view as a compass view because that would
make the roster less recognizable. Real north/south presentation needs two
additional walk cells per body, matching team masks, profile metadata and new
measured console budgets before it can be enabled.

Run the focused source audit with:

```sh
python3 platform/console_art/check-directional-art.py
```

The generator records this coverage in the N64 manifest and PS1 header. That
makes a future directional-art addition an explicit pipeline contract change,
instead of an accidental increase hidden inside a renderer.
