# PC character prototypes

`rook_commander_hd.png` is the first higher-resolution sprite replacement used
to exercise the PC graphics-profile contract with production-shaped input. It
is a transparent 4×2 sheet with 443×443 cells in row-major order:

1. idle A;
2. idle B;
3. walk A;
4. walk B;
5. weapon wind-up;
6. weapon follow-through;
7. relay cast;
8. hurt or defeat.

The image was generated specifically for this prototype, then trimmed to exact
cell boundaries. The runtime consumes this committed PNG directly; screenshots
of it in play are therefore implementation evidence rather than concept art.
Its binding, pivot, and timing live in `content/graphics_profiles.json`. Removing
that one binding or asset restores the generated 32-pixel commander without a
content, package, or rules change.

`relay_commander_3d.tscn` and `relay_drone_3d.tscn` are the first authored 3D
replacement scenes. Their shared script constructs deliberately low-poly,
one-cell-scale geometry and installs in-place `idle`, `run`, `attack`,
`ability`, `hurt`, and `defeat` clips. The full-voxel profile binds commander
and beast archetypes to those scenes; every other archetype keeps the
procedural voxel fallback. These scenes prove the runtime and replacement
contract, not the final production art quality. A future GLB can replace either
path and map different exported clip names in the profile.
