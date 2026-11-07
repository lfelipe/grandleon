# Matched renderer comparison

This folder defines one short battle that Godot, Nintendo 64, and PlayStation
can record independently. The result is an honest side-by-side comparison:
the renderer changes, while the map, units, health, damage, counterattack, and
timing stay the same.

[`scene.json`](scene.json) is the contract. It uses a compact 6×4 medieval
board with forest edges, a grass clearing, and a short stone path. A blue male
knight attacks a red female knight for 3 damage; the red knight counters for 4.
The units keep their left/right positions in the close view and both return to
their original board cells.

The shared timing is deliberately simple:

1. Show the board.
2. Focus the pair and move into the close view.
3. Play the attack and damage reaction.
4. Play the counterattack without swapping sides.
5. Return to the same board view.

Each platform recorder owns its camera and rendering details. It must still
honour the phase boundaries and impact times in `scene.json`; the composition
script refuses clips whose total duration is too far from the contract rather
than silently retiming unrelated footage.

Once the three platform clips exist, compose them with:

```sh
tools/renderer_comparison/compose-renderer-comparison.sh \
    /path/to/godot.mp4 \
    /path/to/nintendo64.mp4 \
    /path/to/playstation.mp4 \
    /tmp/grandleon-renderer-comparison
```

The output folder contains:

- `renderer-comparison.mp4`: full-quality 1920×480 review video;
- `renderer-comparison.gif`: 960×240, 12 fps version intended for the README;
- `renderer-comparison-poster.png`: the shared first-impact frame.

Do not hand-edit one platform's clip to make an event look synchronized. Fix
that renderer's deterministic capture instead, then regenerate the comparison.
