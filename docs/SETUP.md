# Setting up

## Just want to make a game? One command

If you are here to *use* the editor and build a ROM rather than to work on the
repository itself, none of the rest of this page is for you:

```sh
scripts/start.sh
```

Clone, run that, open <http://localhost:5173>. The editor and the ROM build
service come up together, the ROM button works, and nothing is installed on
your machine.

**It needs Docker and the Compose plugin, and nothing else** — no Node, no
Python, no CMake, no virtual environment. On Ubuntu:

```sh
sudo apt-get install docker.io docker-compose-v2
```

Elsewhere, <https://docs.docker.com/compose/install/>. `scripts/start.sh`
refuses by name, with the install line, if either is missing.

Two things worth knowing before you run it. It **mounts the checkout into the
containers and runs them as you**, so what they write belongs to you and shows
up in your working tree. And it **mounts the Docker socket**, because a ROM
build starts the pinned toolchain image as a sibling container — which gives
those containers control of this machine's daemon. `compose.yaml` says why both
are necessary and neither is decoration.

Reaching it from another machine works without configuration: every name this
one answers to is passed to both servers. Set `GRANDLEON_HOST` if you reach it
by some name it cannot work out for itself, and `GRANDLEON_EDITOR_PORT` to move
it off 5173.

## Working on the repository

From a bare machine to a green gate. The gate is one command:

```sh
scripts/local-ci.sh --preview-port 4521
```

The port is a parameter because the browser suite refuses to reuse a server it
did not start; pick one nobody else on the machine is using.

**The gate runs against a clone of HEAD in `/tmp`, not your working tree**, so
uncommitted work is invisible to it: commit first, or the gate is reporting on
the commit before yours.

## Most of it needs no container

Of the seventeen legs that run by default, **one needs a container**:
`WebAssembly module is not stale`. Everything else is the apt packages below
plus what `scripts/setup.sh` fetches. The Nintendo 64 and PlayStation checks are
opt-in and are the other place a container runtime is required.

So: install the packages, run the gate, and add a container runtime when you
want a green one. The WebAssembly leg is not skipped and fails without it.

**If you do want one, read this first.** The console and WebAssembly targets
shell out to pinned, digest-verified images; they are not compilers this
document can tell you to install. Inside the dev container that works only
because `devcontainer.json` bind-mounts the host's `/var/run/docker.sock`, which
grants the container control of the host daemon: **effectively host root**. Do
not use that configuration on a machine where that matters. On bare metal you
run the same targets against your own daemon and the question does not arise.
Any Docker-compatible runtime will do; `GRANDLEON_DOCKER` names the binary.

**It is read when you configure, not when you build.** The CMake targets bake
its value into the environment they hand each build script, so setting it on a
`cmake --build` line changes nothing and podman users get
`docker: command not found` from every containerised target. Name it on the
configure line instead, and reconfigure an existing `build/` to change it:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON -DGRANDLEON_DOCKER=podman
```

The build scripts under `platform/*/scripts/` read the environment variable
directly, so it does work when you run one of those by hand.

## Route A: the dev container

Open the repository in any editor that understands `devcontainer.json`. It
builds Ubuntu 26.04 with everything Route B installs and runs `scripts/setup.sh`
on create.

Every containerised target (the WebAssembly module and both consoles) has to be
run from the host rather than from in here. Each bind-mounts the checkout by
the path it sees, and the host daemon cannot resolve `/workspaces/grandleon`.
This route reaches every other leg.

[../.devcontainer/README.md](../.devcontainer/README.md) has the trade-offs.

## Route B: bare metal, Ubuntu 26.04

Every dependency is in the distribution's own repositories at a version that
satisfies the requirement, Node included. No third-party apt source is needed.

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential ca-certificates clang cmake git \
    libsdl2-dev nodejs npm python3 python3-venv
```

| Package | 26.04 ships | Required |
|---|---|---|
| `build-essential` | 12.12 (GCC 15.2) | any C++17 |
| `clang` | 21.1.8 | any C++17; the gate's second `-Werror` leg, whose warnings differ from GCC's |
| `cmake` | 4.2.3 | 3.20 or newer |
| `nodejs` + `npm` | 22.22.1 | 22.12 or newer |
| `python3` + `python3-venv` | 3.14 | the placeholder-art virtual environment |
| `libsdl2-dev` | 2.32 | optional, see below |

Then the checkout:

```sh
git clone <url> grandleon && cd grandleon
scripts/setup.sh
sudo npx --prefix editor playwright install-deps chromium
```

`scripts/setup.sh` installs both npm trees, the Chromium the browser suite
pins, and the Pillow virtual environment the art checks use. It takes a few
minutes the first time and seconds afterwards; `scripts/setup.sh --check`
reports what is missing and changes nothing.

The `install-deps` line is separate because it is the one step needing root:
`setup.sh` installs the Chromium binary but not the shared libraries it loads,
and the `--with-deps` variant of the install is deliberately excluded so that
setup never asks for a password.

For the container legs, add a runtime and log back in so the group takes:

```sh
sudo apt-get install -y docker.io
sudo usermod -aG docker "$USER"
```

## The legs, and what each needs

| Leg | Needs |
|---|---|
| clone HEAD | git |
| npm ci | Node |
| native gate, GCC `-Werror` | GCC, CMake, and Node, because `tests/CMakeLists.txt` will not configure without it |
| native gate, Clang `-Werror` | Clang |
| desktop client without SDL2 | GCC |
| editor: production build, typecheck, unit tests | Node |
| editor: real browser | Chromium **and its system libraries** |
| editor: the development server is styled and error-free | Chromium, as above. Every other browser leg runs against a production build, so this is the only one that loads `npm run dev` |
| editor: startup, deployment, offline smoke | Node |
| ROM build service: refusals and failure paths | Node. It counts the tests, because `node --test` on an emptied file exits 0 reporting one passing test |
| generated art is not stale | the Pillow venv |
| editor board assets are not stale | the Pillow venv |
| the Creating a Game pictures are not stale | Chromium, as above |
| provided art: every rule refuses | the Pillow venv |
| WebAssembly module is not stale | **a container runtime** |
| every documented path and anchor resolves | Python3 |
| whitespace | git |

Three more legs are behind flags, because each needs a container runtime and
builds a cross toolchain and an emulator from source the first time it is asked
to:

| Flag | Leg | Needs |
|---|---|---|
| `--n64` | Nintendo 64: every check, over one build of the ROMs | **a container runtime** |
| `--playstation` | PlayStation: conformance, what it drew, a played turn, the memory card, and both campaigns | **a container runtime** |
| `--consoles` | both of the above | **a container runtime** |

`--n64` runs all four Nintendo 64 checks (the conformance ROM, the render
probe, the autopilot and the campaign that survives the power switch) through
one build of the ROMs, which is what `grandleon_n64_check_all` is for. Asking
for them one at a time builds every ROM once per check.

The same checks are also CMake targets, which is what a person debugging one
wants. They build through `build/`, so configure it first if you have not
already:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
```

Then run them from the repository root. From `editor/`, `build` resolves to
`editor/build` and every one of them silently does nothing:

```sh
cmake --build build --target grandleon_n64_check_all      # all four at once
cmake --build build --target grandleon_n64_check          # conformance only
cmake --build build --target grandleon_n64_play_check     # the render probe
cmake --build build --target grandleon_n64_autopilot_check
cmake --build build --target grandleon_n64_campaign_check
cmake --build build --target grandleon_playstation_check
cmake --build build --target grandleon_playstation_render_check
cmake --build build --target grandleon_playstation_turn_check
cmake --build build --target grandleon_playstation_card_check
cmake --build build --target grandleon_playstation_campaign_check
cmake --build build --target grandleon_playstation_disc_check
```

Each pulls or builds its pinned toolchain image on first use, which costs
minutes.

## Without SDL2

`libsdl2-dev` is genuinely optional. Leave it out and `grandleon_play` still
builds and still plays, in a terminal rather than a window, and the one host
test lane that disappears is `grandleon.sdl_presenter`. The gate compiles that
configuration on every run so the claim keeps being true.

`ccache` is optional in the same way: present, the configure finds it and the
gate's second cold build of an unchanged file is a lookup; absent, everything
builds exactly as before.

[../CODING.md](../CODING.md) has the rest: the build, the toolchain pinning,
and the specification workflow.
