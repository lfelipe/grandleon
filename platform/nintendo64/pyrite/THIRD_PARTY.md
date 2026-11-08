# Pyrite target: third-party notice and pins

This target uses Pyrite64's runtime and generated project shell. It does not
require Grandleon creators to use the Pyrite editor and it does not include any
third-party game assets.

Pyrite64 v0.10.0 is pinned to commit
`eb8545428f1059841c844169b7b307535e0bfeb2`. Pyrite64 is MIT licensed:

> Copyright (c) 2025-2026 Max Bebök
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

The build fetches the official Linux v0.10.0 release archive and verifies both
the archive and embedded AppImage hashes from `versions.env`. Nothing from the
Pyrite repository is vendored here. Before shipping a source or binary release,
retain Pyrite's notice above alongside the notices exported by the toolchain.

Pyrite v0.10.0 records libdragon
`fab335ef93e785dd074aa0e43d9ac1c8bcdf3eae` and Tiny3D
`6f5ac0ba098f86d04e549ea754834f1be3c150e1` as its submodules. Grandleon's
first measured comparison instead uses libdragon
`c79a52b42ac790e06e797aede43914dd8754cd5f` and Tiny3D
`517156d9464aaab95bf9531336d9a57624405ca7`. That exact pair produced the 4 MB
and 8 MB measurements and is therefore pinned by this package. Moving either
pin requires rebuilding, booting under an accurate real-RSP emulator, and
re-recording the memory/frame baseline.
