# Shared console character art

The Nintendo 64 and PlayStation builds generate their character cutouts from
the same curated high-resolution sheets and team-colour masks as Godot. Run
`generate-character-art.py` with a project source and a platform name; the
output belongs in a build directory, not in source control.

Each base body sheet is a 4×2 animation sheet: two idle cells, two walking
cells, wind-up, follow-through, ability and hurt. It has one native
right-facing view. The renderers use that view for eastward travel and mirror
it for westward travel. This costs no additional texture memory and keeps the
current character ROM, RDRAM/VRAM and frame budgets unchanged.

Thirty-nine bodies now have a separate authored 4×2 directional sheet:
genuine north idle/walk on the top row and genuine south idle/walk on the
bottom row. They span all seven styles. The matching team mask and explicit
`directionalSheet` profile binding keep the same faction-colour workflow.
Godot uses these views on vertical route legs and falls back to the side walk
for the remaining 73 bodies. The compact-console generator reports which
project drawings have source coverage and their projected CI4 cost; console
runtime integration remains deliberately disabled until more roster coverage
and a renderer-specific visual/performance gate are ready.

Run the focused source audit with:

```sh
python3 platform/console_art/check-directional-art.py
```

The current 39-body projection is 324,480 bytes per platform: four 64×64 CI4
frames per body, including one 32-byte palette per frame. That is source/ROM
payload, not persistent TMEM/VRAM; a later renderer can stream into the current
character slot. The complete 112-body projection remains 931,840 bytes, so
project-specific selection remains important. The generator records coverage
and projected bytes in the N64 manifest and PS1 header without changing either
production renderer or its current executable budget.
