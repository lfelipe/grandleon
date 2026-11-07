# Matched renderer showcase

This folder defines one short visual benchmark that Godot, Nintendo 64, and
PlayStation record independently. It is deliberately a renderer showcase, not
a production campaign battle. The renderer changes; the complete environment,
combatants, health, damage, counterattack, camera landmarks, HUD limits, and
timing do not.

[`scene.json`](scene.json) is the contract. Its active board is the familiar
6×4 medieval forest/grass/paved duel, but the visual scene is 12×10: three
explicit rings of clustered forest and edge-extended terrain surround the
playable cells. The camera must frame that complete scene. At 320×240 there are
no title or footer bands; the board and surroundings use nearly the whole
screen.

Only compact health bars, selected/target cells, and transient damage numbers
belong in the capture HUD. Build diagnostics, controls, FPS counters, and
persistent phase labels do not.

The close view freezes a deterministic terrain descriptor and source-layer
variant for both attack and counter. Godot's first proof uses forest as the
dominant terrain, grass as secondary, and a paved transition. Its panorama,
transition, midground, and prop plates come from existing Godot terrain scenes.
Consoles downconvert those same source PNGs instead of inventing separate
background artwork. The pre-rendered terrain now continues through the lower
edge instead of meeting a separately rendered floor at a visible seam. Units,
shadows, attack effects, health, and damage remain live on every platform.

The shared sequence is:

1. Show the complete tactical scene.
2. Fly or focus toward the selected pair.
3. Play the attack and damage reaction.
4. Play the counterattack without swapping sides or backdrop.
5. Return to the exact settled tactical camera.

Each recorder owns its implementation, but must honour phase and impact times
in `scene.json`. The composition script rejects clips with the wrong duration
instead of silently retiming unrelated footage.

Compose the three finished clips with:

```sh
tools/renderer_comparison/compose-renderer-comparison.sh \
    /path/to/godot.mp4 \
    /path/to/nintendo64.mp4 \
    /path/to/playstation.mp4 \
    /tmp/grandleon-renderer-comparison
```

The output folder contains a full MP4, a smaller animated GIF, and the shared
first-impact poster. Do not hand-edit one platform's clip to make it agree.
Fix its deterministic capture, regenerate it, and then compose again.
