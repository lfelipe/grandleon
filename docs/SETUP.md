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

Windows support is in development for 0.6; there is no official Windows download
yet. To build the creator, put Git, Python 3.11 or newer, CMake, Ninja and a
MinGW-w64 compiler with C++ threading support on your PATH, then run:

```powershell
python platform/godot/scripts/build-windows.py
```

The output is `out/grandleon-windows`. Start it with `launch-pc.cmd`. The bundle
includes Godot 4.7.2, the native extension, artwork, audio, translations and Steel
and Cedar. Players need no separate compiler or Godot installation.

To check the installed creator, export a new campaign and play it to victory:

```powershell
python platform/godot/scripts/check-pc-acceptance.py `
  --bundle out/grandleon-windows --output out/windows-acceptance
```

This check uses its own project library and leaves the installed bundle alone.
It removes its disposable creator copy before launching the exported game, and
keeps logs and a victory screenshot. Use a new output directory for each run.

Linux developers can cross-build with `cmake/toolchains/WindowsMinGW.cmake`,
then package and check the Windows application under Wine:

```sh
python3 platform/godot/scripts/build-windows.py --no-build \
  --build-dir build-windows-cross06 --package path/to/steel-and-cedar.gpk \
  --wine /usr/bin/wine64 --output out/grandleon-windows
python3 platform/godot/scripts/check-pc-acceptance.py \
  --bundle out/grandleon-windows --output out/windows-acceptance \
  --wine /usr/bin/wine64
```

Compile the supplied package from Steel and Cedar. Packaging verifies the pinned
runtime download, imports Windows resources and refuses to overwrite an existing
output. The complete 74-check Windows-target suite and rendered creator-to-victory
flow pass under Wine. Wine runs the engine directly because its console relay can
hang after the game exits; the native Windows check uses `launch-pc.cmd`.

Before an official release, validate the native build and launcher on Windows,
then check audio, controllers, saves, language switching and accented paths on a
clean Windows machine. These Wine results do not cover that hardware review.
Optional console builders also need Windows integration; desktop editing and play
work without them.

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

A 64-bit Android 7.0 or newer device with OpenGL ES 3.0 is required. The APK includes all
built-in artwork and audio, so it is currently about 450 MB. Physical phone
and tablet testing remains on the [Android work list](../TODO.md#android-player-and-editor).

For emulator testing, use host graphics, or `-gpu swangle` on a headless
machine. Direct SwiftShader modes can fail to compile Godot canvas shaders.
Software emulation is useful for interaction checks; it does not measure
phone or tablet graphics performance.

Tap to select units and destinations; drag to pan and pinch to zoom. Unit
actions appear after selection; secondary controls live in the game menu.
Same-device two-player games also have a player-switch button. Android Back cancels the
current action or opens the menu. Returning from the background leaves the
game paused until you resume it.

**Settings → Graphics** offers Auto, Low, Medium and High. Auto uses a
conservative hardware recommendation; a manual choice stays selected after
restarting. Changes apply immediately. Low reduces 3D
resolution and disables dynamic shadows; Medium keeps shadows at a lower 3D
resolution. Text, touch controls and campaign rules stay the same at every level.

In the map editor, choose terrain and drag across cells to paint. Switch on
**Pan map** when you want to move around instead; the zoom buttons change the
editing scale.

Use **Playable campaign** to export a `.grandleon.game.zip` that another
Android or PC installation can open with **Open campaign file**. Use
**Editable project** to keep a `.grandleon.zip` copy for further editing.
Android uses the system file picker, so no broad storage permission is needed.
N64 ROM and PS1 disc builds remain PC-only: transfer the editable project to
the PC editor when you need those exports.

For automated Android editor checks, build a separate test application with
`build-android.py --ui-test --output out/android-ui-checks`. It uses its own
package and storage, leaving the regular app's campaigns and saves alone.
With an Android emulator running, use:

```sh
python3 platform/godot/scripts/test-android-ui.py --serial emulator-5554 \
    --apk out/android-ui-checks/grandleon-android-development.apk \
    --profile compact-phone --output out/android-ui-results
```

The other profiles are `wide-phone` and `tablet`; `--cutout corner` also
checks an Android display cutout. The runner uses actual Android input and
system file pickers, writes screenshots and results, and needs a keyboard-free
emulator configuration to exercise the on-screen keyboard. The runner checks
that `adb -s emulator-5554 shell am get-config` reports `nokeys`; an AVD setting
alone is insufficient if its image still exposes an AT keyboard. Detach that
emulated keyboard or use another touch-only image before running the check.
Hardware keyboards, controllers, audio and graphics speed still need device testing. Do not share
the test APK as the player application.

### Campaign library in 0.6

The development branch adds **Browse Campaigns** to the Godot home menu. It
loads the [official catalogue](https://github.com/lfelipe/grandleon-campaigns)
automatically. Choose **Download**, then **Play**. Games offering a choice of
one or two players use the usual player selector. Installed campaigns remain
available offline and appear on the home screen too.

Newer versions offer **Update**. The player checks the downloaded package
before switching versions. Compatible updates keep your progress. If saves
are incompatible, choose **Keep installed version** to continue your game,
or **Update and start fresh**. Previous save files remain on your device.

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


### Checking LAN co-op

Use matching 0.6 development builds on the same network. Host a campaign on
one device and select it from **Join a LAN game** on the other. The host stores
shared saves; hosting that campaign again resumes both armies. Direct joining
also accepts `host-address:port` when needed. Internet matchmaking is not part
of the LAN build.

Run the real UDP discovery, ENet, campaign save/resume, guest-input and bell
movement regressions with:

```sh
platform/godot/scripts/smoke-lan.sh build-godot
```


### Checking touch layouts

Run the local input, phone/tablet layout and touch-player checks with:

```sh
platform/godot/scripts/smoke-mobile.sh build-godot
```

The campaign checks include long chapter names, larger text, scrolling to the
last action, and reordering chapters in English and Portuguese. The suite also
creates equipment, story scenes and roles with accented names and compiles the
results. Desktop layout checks complement Android emulator and physical-device
testing.

### Checking interrupted local games

The Godot player keeps automatic recovery for local solo and same-device
co-op separately. Manual save slots remain between-battle checkpoints.
Run the native recovery, full-player pause/resume and forced-termination tests:

```sh
platform/godot/scripts/smoke-local-recovery.sh build-godot
```

These checks kill desktop Godot processes without shutdown callbacks. They
also exercise accented save paths, compatible campaign updates, interrupted
file replacement and damaged-file fallback. An Android 15 x86_64 emulator also restores solo and same-device co-op after
a force-stop through the normal menus, including across an in-place app update.
Physical-device termination and update checks remain on the release checklist.
