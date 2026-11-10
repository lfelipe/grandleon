# Third-party boundaries

Mipsync is MIT-licensed and pinned to
`866f405a0e34b5925a8d832272fe52c657688430`. Grandleon does not vendor its
source in this repository. Release packaging fetches that revision once and
bundles it, with its MIT licence, in the optional offline console builder. The
build applies the twelve engine-generic patches under
`tools/upstream_patches/engines/mipsync` with `git am`.
Grandleon package loading, assets and runtime adaptation remain MIT code under
`platform/playstation/mipsync`.

Mipsync is young and its Windows editor, Mips# VM, physics and generic audio
runtime are not adopted. The PS1 template cross-builds independently on
Linux with PSn00bSDK. Grandleon supplies CD music and SPU sound effects through
its own audio adapter.

PSn00bSDK's core is MPL-2.0. The optional offline builder contains only the
pinned release's public headers, five core target archives, licence, and a
corresponding-source link; it does not contain PSn00bSDK's second GCC toolchain
or GPL host programs. The selected GPU, event, API, CD and SPU archives are linked to
the target after symbol-table normalization for the Debian linker; that
normalization changes no code or data. Mipsync intentionally supplies an
original GTE compatibility layer and `libpsxgte` is not linked.

The interface font is Spleen 6×12 by Frederic Cambus, pinned to version 2.2.0
under the BSD 2-Clause license. Its original BDF, bitmap atlas and license are
in `assets/fonts/spleen`. Every generated disc includes the complete notice as
`SPLEEN.TXT`; builder and desktop packages retain it as well.

Other projects were evaluated only as references:

- PSYQo/Nugget is MIT and remains a viable lower-level fallback;
- PSoXide is GPL-2.0-or-later;
- Tundra is GPL-3.0 and discontinued;
- PSn00bSDK is the underlying toolchain, not a comparable high-level renderer.

No code was copied from PSoXide, Tundra, the Ocarina of Time PS1 port or other
incompatible/reference projects.

