# Third-party boundaries

Mipsync is MIT-licensed and pinned to
`866f405a0e34b5925a8d832272fe52c657688430`. Grandleon does not vendor its
source. The build fetches that revision and applies the three engine-generic
patches under `tools/upstream_patches/engines/mipsync` with `git am`.
Grandleon package loading, assets and runtime adaptation remain MIT code under
`platform/playstation/mipsync`.

Mipsync is young and its Windows editor, Mips# VM, physics and generic audio
runtime are not adopted. The PS1 template itself cross-builds independently on
Linux with PSn00bSDK. The renderer-only seam preserves a separate Grandleon
audio boundary; it does not mean the shipping PlayStation port will lack music
or effects.

PSn00bSDK's core is MPL-2.0 and its host tools carry their own licenses,
including GPL components. The build consumes the installed SDK and does not
copy those tools into the game. Its selected GPU, event and API archives are
linked to the target after symbol-table normalization for the Debian linker;
that normalization changes no code or data. Mipsync intentionally supplies an
original GTE compatibility layer and `libpsxgte` is not linked.

Other projects were evaluated only as references:

- PSYQo/Nugget is MIT and remains a viable lower-level fallback;
- PSoXide is GPL-2.0-or-later;
- Tundra is GPL-3.0 and discontinued;
- PSn00bSDK is the underlying toolchain, not a comparable high-level renderer.

No code was copied from PSoXide, Tundra, the Ocarina of Time PS1 port or other
incompatible/reference projects.

