# PC graphics directions

Every option below keeps `.gpk` content and simulation portable. The difference
is the PC renderer and art pack, not the game rules.

The first, third, fourth, and fifth rows now have runnable implementations. Compare
their like-for-like output by running `scripts/capture-graphics-prototypes.sh`.
It writes screenshots under the ignored build tree; they are captures of the
demo package under the compatibility renderer, not concept renders.

Scores are relative, from 1 (weak/slow) to 5 (strong/fast). “First build” is
speed to a polished PC build, not the eventual production rate.

| Direction | Visual ceiling | First build | Replaceability | Console compatibility | Shared art lineage | SteamOS headroom |
|---|---:|---:|---:|---:|---:|---:|
| Enhanced 2D pixel art | 3 | 5 | 5 | 5 | 5 | 5 |
| High-resolution illustrated 2D | 4 | 3 | 5 | 5 | 3 | 5 |
| 3D diorama with 2D characters | 5 | 3 | 4 | 5 | 4 | 4 |
| Full voxel diorama | 4 | 3 | 5 | 5 | 4 | 4 |
| Voxel terrain with 2D characters | 4 | 4 | 5 | 5 | 5 | 4 |
| Full stylized low-poly 3D | 5 | 2 | 3 | 5 | 2 | 3 |
| Voxel-authored, multi-output | 5 | 2 | 5 | 5 | 5 | 4 |

## What the runnable prototypes establish

| Profile | Already real | Still blockout work | Evidence from the capture |
|---|---|---|---|
| Enhanced 2D | asset lookup, autotiling, animation strips, selection, motion | higher-resolution pack, effects and authored animation metadata | most cohesive today, but depth and lighting remain limited |
| 3D diorama + 2D characters | 3D camera, lit terrain, water, bridges, trees, mountains, billboard animation | authored meshes/materials, sprite pivots, sprite-facing and shadow polish | largest immediate upgrade while retaining the existing character library |
| Voxel terrain + 2D characters | voxel terrain/props and billboard animation | authored voxel chunks, palette/material manifest, sprite-facing and shadow polish | clearest test of the recommended modular hybrid |
| Full voxel | terrain, props, distinct humanoid/drone rigs, animation, lighting | authored voxel forms, palette/material manifest, richer silhouettes | technically clean and readable, but character art becomes a new production pipeline |

All four retain the same board API, concurrent player cursors, legal-tile
queries, and package presentation indices. Switching renderer profiles does not
fork campaign content.

## Selected sequence: voxel terrain, then multi-output

The PC build now starts with an orthographic voxel board and retains the animated 2D
characters. Terrain height, water, shadows, light, weather, and small props
create the immediate PC upgrade while the existing character roster keeps the
first build achievable and tactically legible.

If that look works on the target PC, make voxel character models optional and add
a bake step that renders the same sources to sprite sheets for constrained
targets. That grows into the “voxel-authored, multi-output” option without
making the initial deployment depend on a complete 3D character pipeline.

The renderer should expose profiles rather than replace the fallback:

```text
presentation indices from .gpk
             |
             +-- sprite profile -> PNG strips + blob terrain
             +-- voxel profile  -> GLB/voxel-derived meshes + materials
             +-- future profile -> replacement pack with the same manifest keys
```

The important boundary is the key, not the file type. A terrain lookup is still
`theme + terrain kind`; a character lookup is still
`style + figure + archetype + colour`. Profile manifests add scale, pivot,
animation names, and fallbacks.

## Option notes

- **Enhanced 2D** is the shipping baseline and safest fallback. Better
  particles, lighting overlays, camera motion, richer backdrops, and HD packs
  can take it further without a renderer rewrite.
- **Illustrated 2D** has a high ceiling but makes animation the dominant art
  cost. It needs manifest-defined pivots and animation clips before new art is
  commissioned.
- **3D diorama + 2D characters** gives the fastest dramatic lighting and depth.
  It requires careful billboard, shadow, and scale treatment so characters
  belong to the scene.
- **Full voxel** is modular and unusually well matched to small grid maps. Its
  risk is aesthetic rather than technical: generic cubes look like a tool demo,
  so silhouettes, palette, materials, and props need a strong style guide.
- **Voxel terrain + sprites** is the pragmatic hybrid. It also lets console and
  PC characters share the current sprite vocabulary.
- **Low-poly 3D** offers the broadest animation/camera range but has the most
  expensive character pipeline and the least direct connection to console art.
- **Voxel multi-output** offers the best long-term common source, but the bake,
  validation, palette reduction, and animation-view rules are a project of
  their own.

Godot provides a grid-oriented 3D mesh library through
[GridMap](https://docs.godotengine.org/en/stable/tutorials/3d/using_gridmaps.html),
recommends [glTF/GLB for 3D interchange](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html),
and can batch repeated meshes through
[MultiMesh](https://docs.godotengine.org/en/stable/tutorials/performance/using_multimesh.html).
Typical campaign boards are small enough that the design should prioritize
clean replacement and art direction before aggressive batching.
