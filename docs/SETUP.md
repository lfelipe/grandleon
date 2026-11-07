# Setting up

## Making or playing a game

Use the Linux PC release. The same application contains the Godot player and
the supported creator. Choose **Create / Edit Games** and follow
[Creating a game](CREATING_A_GAME.md). You do not need a source checkout,
system-wide Godot installation, container runtime, or network service.

To build that application from a checkout:

```sh
platform/godot/scripts/build-steamos.sh --output out/grandleon-pc
GRANDLEON_WINDOWED=1 out/grandleon-pc/launch-pc.sh
```

The builder downloads the pinned Godot runtime and verifies its checksum.
Creating games, importing and exporting portable projects, playtesting, and
making Linux player builds all work locally. Nintendo 64 and PlayStation
exports become available when their optional builder payload is installed.

## Working on Grandleon

On Ubuntu 26.04, install the host tools:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential ca-certificates clang cmake git \
    libsdl2-dev nodejs npm python3 python3-venv xvfb
```

Then prepare the checkout and run the gate:

```sh
git clone <url> grandleon
cd grandleon
scripts/setup.sh
scripts/local-ci.sh
```

`scripts/setup.sh --check` reports missing dependencies without changing the
machine. The gate tests a clean clone of the current commit, so commit local
changes before running it.

The dev container is an optional equivalent environment. It runs
`scripts/setup.sh` when created; see [the dev-container notes](../.devcontainer/README.md).

## Console builds

Console builds use pinned containers. Install any Docker-compatible runtime,
then configure CMake with its command name if it is not `docker`:

```sh
cmake -S . -B build \
    -DGRANDLEON_BUILD_TESTS=ON \
    -DGRANDLEON_DOCKER=podman
```

The local gate runs console checks automatically when a container runtime is
available. These flags select them explicitly:

```sh
scripts/local-ci.sh --n64
scripts/local-ci.sh --playstation
scripts/local-ci.sh --consoles
scripts/local-ci.sh --no-consoles
```

The first console build takes longer because it obtains the pinned toolchain.
Run console targets from the host rather than from inside the dev container;
the host container daemon cannot resolve the checkout's container-only path.

See [Console renderer](CONSOLE_RENDERER.md) for the playable console experience,
exports, and current limits.

## Optional SDL2

Without `libsdl2-dev`, the terminal client still builds and plays. The SDL
window and its presentation test are the only pieces omitted. `ccache` is also
optional; CMake uses it automatically when available.

For build conventions and contribution checks, continue with
[Coding](../CODING.md).
