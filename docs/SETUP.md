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

### Windows builds for 0.6

Windows support is being developed on the local 0.6 branch. There is no
Windows release download yet. The Windows x64 engine DLL and campaign compiler
build with MinGW-w64 GCC 13; all 64 compiled native tests pass under Wine.
This does not replace testing the application on a real Windows desktop.

Developers can build the creator bundle with Git, Python 3.11 or newer,
CMake, Ninja and a MinGW-w64 compiler with C++ threading support on their PATH:

```powershell
python platform/godot/scripts/build-windows.py
```

The script downloads the pinned Godot 4.7.2 Windows runtime, verifies its
checksum, and packages it with the native extension, artwork, translations,
Steel and Cedar campaign and license notices. The output is
`out/grandleon-windows`; start it with `launch-pc.cmd`. Build dependencies are
for developers; the resulting bundle includes the runtime needed by players.
This native Windows build command still needs Windows desktop validation.

Linux developers can cross-build the native targets with
`cmake/toolchains/WindowsMinGW.cmake`, then package the staged Windows app:

```sh
python3 platform/godot/scripts/build-windows.py --no-build \
  --build-dir build-windows-cross06 --package out/windows06-steel-and-cedar.gpk \
  --wine /usr/bin/wine64
```

The supplied package must be compiled from Steel and Cedar. Wine imports the
actual Windows resources and extension before packaging completes. A cross-built
bundle needs the same Windows testing as a native build. The builder refuses
to overwrite an existing output and records whether its source tree was clean.

The Windows build needs to cover both playing games and creating them:

- Validate native Windows compilation and the packaged launcher, including
  paths containing spaces or accents. Export templates for Godot's separate
  native export workflow are not included in this source-project bundle.
- Validate authoring and standalone-game export through the Windows creator
  interface. Headless Windows-engine export and independent battle launch
  already pass under Wine, including paths with accents; Unicode package and
  save-path regression checks also run against the native extension.
- Run the native tests and the create → export → play acceptance flow on
  Windows. Check saves, keyboard and controllers, audio, live language changes,
  and project paths containing spaces or accented characters. Test the packaged
  download on a clean Windows machine as well as the development build.
  Graphics and audio integration tests need a runner that can create the
  required OpenGL context and open an audio output device.
- Decide how optional console builders work on Windows. Their current Linux
  containers and shell launchers need an explicit integration plan; desktop
  playing and editing should work without installing those optional tools.

The Windows application remains a development build until these checks pass.

### Android development build for 0.6

The Android application includes both the player and campaign editor. Build it
on Linux with Java 17, CMake, Ninja, the host Godot 4.7.2 executable and the
Android SDK packages below:

```sh
sdkmanager "platform-tools" "platforms;android-36" \
    "build-tools;36.1.0" "ndk;28.1.13356709"
python3 platform/godot/scripts/build-android.py --sdk /path/to/android-sdk
```

The script builds ARM64 and x86_64 native extensions, compiles the included
campaign and downloads checksum-verified Godot 4.7.2 export templates. It creates
`out/grandleon-android-development` with an APK and build information. Choose a
new `--output` directory for subsequent builds. Use `--java-home` or `--godot`
if those tools are installed elsewhere. This is a development build, signed
with a local key kept under `build-android`; it is not an official release.

Install on a selected device with:

```sh
adb -s DEVICE_SERIAL install --no-incremental -r \
    out/grandleon-android-development/grandleon-android-development.apk
```

Android 7.0 or newer and OpenGL ES 3.0 are required. The APK includes all
built-in artwork and audio, so it is currently about 436 MB. Physical phone
and tablet testing remains on the [Android work list](../TODO.md#android-player-and-editor).

For emulator testing, use host graphics. Godot 4.7.2 can show a black screen
with `swiftshader_indirect` because of an
[upstream shader compiler issue](https://github.com/godotengine/godot/issues/109550).
On a headless Linux machine, run the emulator under Xvfb with `-gpu host`.

Tap to select units and destinations; drag to pan and pinch to zoom. The
bottom buttons provide confirm, cancel, wait, end turn and the game menu.
Two-player games also have a player-switch button. Android Back cancels the
current action or opens the menu. Returning from the background leaves the
game paused until you resume it.

In the map editor, choose terrain and drag across cells to paint. Switch on
**Pan map** when you want to move around instead; the zoom buttons change the
editing scale.

Use **Playable campaign** to export a `.grandleon.game.zip` that another
Android or PC installation can open with **Open campaign file**. Use
**Editable project** to keep a `.grandleon.zip` copy for further editing.
Android uses the system file picker, so no broad storage permission is needed.
N64 ROM and PS1 disc builds remain PC-only: transfer the editable project to
the PC editor when you need those exports.

### Campaign library in 0.6

The development branch adds **Browse Campaigns** to the Godot home menu. It
loads the [official catalogue](https://github.com/lfelipe/grandleon-campaigns)
automatically. Choose **Download**, then **Play**. Games offering a choice of
one or two players use the usual player selector. Installed campaigns remain
available offline and appear on the home screen too.

To use a community catalogue, paste its public HTTPS JSON URL into the browser
and choose **Add catalogue**. No GitHub login is needed for public catalogues.
Each source has separate downloads and saves. The first packages rely on
built-in artwork and audio; packages requiring other asset packs are shown as
unavailable until that support is implemented. The published 0.5 application
does not include this browser yet.

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
