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
