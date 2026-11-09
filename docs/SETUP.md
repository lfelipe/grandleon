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

### Planned Windows edition

Windows support still needs to be built and validated. The current PC bundle,
Godot export preset and standalone-game exporter target Linux. CMake already
has MSVC warning settings, but that does not establish Windows compatibility.

The Windows build needs to cover both playing games and creating them:

- Choose and test a Windows C++ toolchain for the shared rules, content tools
  and Godot extension. Start with one supported compiler and record its version.
  Resolve each tool to a single executable when several versions are on `PATH`.
- Obtain the matching pinned Windows Godot runtime and export templates, with
  verified checksums. Add Windows DLL entries to the extension manifest and
  handle Windows DLL output paths in the CMake staging rules.
- Provide a Windows launcher and a complete downloadable application, including
  artwork, translations and required runtime DLLs. Users should not need Godot
  or a compiler installed to play or create a game.
- Add a Windows standalone-game export path in the creator. It must produce a
  game that launches independently of the creator and source checkout.
- Run the native tests and the create → export → play acceptance flow on
  Windows. Check saves, keyboard and controllers, audio, live language changes,
  and project paths containing spaces or accented characters. Test the packaged
  download on a clean Windows machine as well as the development build.
  Graphics and audio integration tests need a runner that can create the
  required OpenGL context and open an audio output device.
- Decide how optional console builders work on Windows. Their current Linux
  containers and shell launchers need an explicit integration plan; desktop
  playing and editing should work without installing those optional tools.

These are preparation tasks, not instructions for an available Windows release.

### Linux development environment

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
