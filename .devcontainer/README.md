# Dev container

Ubuntu 26.04 with GCC and Clang, CMake, Ninja, Node.js 22, Python 3, SDL2, the
container CLI, and Chromium's shared libraries. On create it runs
`scripts/setup.sh`, so the checkout comes out gate-ready.

It is a **convenience, not the definition of the build**. The gate runs on a
plain machine against the versions listed in [../CODING.md](../CODING.md), and
the build must keep working there. If the two ever disagree, the plain machine
is right. [../docs/SETUP.md](../docs/SETUP.md) is the same environment built by
hand.

## Using it

Open the repository in any editor that understands `devcontainer.json`, or build
it directly:

```sh
docker build -t grandleon-dev .devcontainer
docker run --rm -it \
    -v "$PWD:/workspaces/grandleon" \
    -v /var/run/docker.sock:/var/run/docker.sock \
    grandleon-dev
```

## What is deliberately *not* baked in

**The Emscripten SDK.** The WebAssembly build runs in its own pinned
`emscripten/emsdk` image with a verified digest, because its output is committed
to the repository and must be byte-reproducible. Installing a second copy of
Emscripten here would create exactly the drift that pinning exists to prevent.

`grandleon_wasm` shells out to a container runtime and starts the Emscripten
container as a sibling, not a child. That is why the host socket is mounted.
Two things follow:

- the mounted socket grants the dev container control of the host daemon, which
  is effectively host root. Do not use this configuration on a machine where
  that matters.
- **run the WebAssembly target from the host anyway.** It bind-mounts the
  checkout by the path it sees, and the host daemon cannot resolve
  `/workspaces/grandleon`. Both console targets mount the same way and want the
  same treatment. Everything else in the container is unaffected, because the
  compiled module is committed.

**The Chromium binary.** Only its system libraries are installed here. The
browser itself is fetched per-checkout into the gitignored
`.playwright-browsers/` so that its version is governed by
`editor/package-lock.json` rather than by whenever this image was last built.
`scripts/setup.sh` does that on create, and `PLAYWRIGHT_BROWSERS_PATH` is
already set in the container environment.
