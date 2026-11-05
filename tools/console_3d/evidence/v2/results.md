# Console proxy v2 measurements

Captured 2026-11-04 from local commit inputs in this directory.

## PlayStation / PCSX-Redux

Pinned inputs:

- build image digest: `sha256:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b`
- Nugget revision: `e5d63b8e5eeadaa04cc5a534c671ea5b252afc70`
- scratch style: `scifi`

```text
V2 strategic six roles plus references 21021 us; submitted 865 triangles
V2 strategic sixteen low-LOD frame 53587 us fps 18; transformed 5376 and submitted 2339 proxy triangles
V2 tactical six high-LOD frame 30624 us fps 32; transformed 3156 and submitted 1309 proxy triangles
V2 action two high-LOD frame 19171 us fps 52; transformed 1052 and submitted 483 proxy triangles
LOAD sixteen billboard units frame 1585 us fps 630; the mesh frame is 126 tenths of it
RESULT FAIL 144/147 style scifi
```

The final failure is the complete historical scratch verdict. Its three failed
roster-fit assertions occur after the captured v2 phases and predate this
experiment.

## Nintendo 64 / ares

Pinned inputs:

- libdragon: `c79a52b42ac790e06e797aede43914dd8754cd5f`
- Tiny3D: `517156d9464aaab95bf9531336d9a57624405ca7`
- ares: `0aafd85789215e84e1e43415c07d4c88461b7899` (`v148`)

```text
v2 strategic six low-LOD triangles=2232 us=10298 fps=97 depth=on
v2 strategic sixteen low-LOD triangles=5592 us=24467 fps=40 depth=on
v2 tactical six high-LOD triangles=3372 us=14975 fps=66 depth=on
v2 action two high-LOD triangles=1268 us=6199 fps=161 depth=on
RESULT PASS 4/4
```
