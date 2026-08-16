# The ROM build service

Builds a console ROM of an author's own project, on this machine, with the
pinned toolchain this repository already uses for every checked image.

```
node tools/rom_service/serve.mjs [--port 4699]
```

The editor talks to it through Vite's proxy at `/api/n64`. With it running, the
**Nintendo 64 ROM** button beside *Export* is enabled; without it, the button is
disabled and says why.

There is also a shell form, which is what the two console lanes use:

```
node tools/rom_service/build-rom.mjs games/demo/source/project.json out.z64
```

## Why this and not a patchable template

The other way to hand an author a ROM is to ship one pre-built image with a
reserved slot in it and have the editor overwrite the slot. Its great virtue is
that the machine doing the patching needs no toolchain at all.

It does not work on this console, for a reason that is in
`platform/nintendo64/CMakeLists.txt` rather than in anybody's opinion: the
image is a `n64elfcompress`-compressed ELF packed behind an IPL3 header, so the
embedded `project.json` is not addressable in the file. (`n64tool` packs a
*list* of entries and would take an uncompressed one, at the price of a slot,
an alignment, a reserve and checksum arithmetic.)

But the constraint that made patching necessary does not hold: the browser
does not have to be where the ROM is assembled. And once that is true, the
patch route is paying for a slot, an alignment, a reserve, checksum arithmetic
and a byte-identity gate, all of which exist only to make an image patchable.

The decisive argument is honesty rather than effort. On the patch route, *"the
ROM an author downloads is the ROM this repository checks"* is a property that
has to be enforced by a gate watching a patcher, and a template that patches
cleanly and runs the wrong game satisfies every structural check such a gate can
make. On this route it is not a property at all. It is the same build.

## What it refuses, by name

Everything decidable from the project alone is decided in milliseconds, before
a container starts.

| code | when |
|---|---|
| `project_unreadable` | not JSON, or not a JSON object |
| `project_without_campaign` | no campaigns, or a first campaign with no id (the ROM runs one) |
| `campaign_id_not_an_identifier` | a first campaign whose id is not a source identifier |
| `character_style_not_served` | a style or figure the art library does not hold, whether the game's or a character's |
| `project_too_large_for_the_console` | source past the embed budget |
| `project_does_not_compile` | the host compiler's own diagnostics, verbatim |

`project_does_not_compile` earns its place more than the others. The Nintendo 64
compiles its project *on the console*, so a project the compiler rejects yields
a ROM that builds perfectly, boots, and stops on an assertion an author cannot
read. The host compiler answers the same question in two seconds.

**There is deliberately no mixed-style refusal.** The Nintendo 64 build embeds
the drawings a project's content actually draws, so a project drawing a
`medieval` knight, a `medieval` mage at the second figure and two `nature`
archers is served. What is checked instead is that every style and every
figure the content names (the game's own and each character's) is art the
library holds, with the path of the character that named it. Without that
check, art the library does not hold reaches the build's configure and fails
it with a fatal error the author sees as a container exiting non-zero.

This is also the reason the route is a real build rather than a patched
template: a template could never have served the combination, because there is
one per style-and-figure pairing and templates cannot be built for all of them.

`campaign_id_not_an_identifier` is the one refusal that is not about whether the
console can play the project. The build writes that id straight into a C++
string literal in `platform/nintendo64/CMakeLists.txt`, so by the time the ROM
compiles it is code rather than data: an id carrying a quote closes the literal,
and an id carrying a `#include` names a file the container's preprocessor then
reads and reports on. Project files are content people share, so *open somebody
else's project and press Build* has to be safe. Both ends refuse it: here, and
in the configure, so a `cmake` invocation that never went near this service is
covered too.

## Who is allowed to ask

Everything is refused except the editor addressed as localhost, or as a name
this service was started with. The machine's own hostname is refused by
default: a peer on the network reaching the editor sends exactly that `Host`,
and nothing here can tell the two apart.

| code | when |
|---|---|
| `request_not_addressed_locally` | a `Host` that is not `localhost`, `127.x`, `[::1]`, or a name given to `--allow-host` |
| `request_from_another_site` | `Sec-Fetch-Site` other than `same-origin`/`none`, or an `Origin` that is not the `Host` |

This is not paranoia about the network; it is about the browser. Left alone,
`POST /api/n64/build` needs no header a page has to be given permission to send,
which makes it a CORS *simple* request: any page an author has open could launch
a two-minute container build on their machine with no preflight and no consent.
Listening on the loopback interface is not a defence, because loopback is
exactly where such a request comes from.

The `Host` check is what the editor's own Vite proxy relies on. That proxy binds
every interface, so the editor is reachable from a tablet on the same desk, and
forwards `/api/n64` here with the browser's `Host` intact. A peer on the
network therefore arrives with a `Host` that is not local and is refused
here, in the one place that can tell. It is also what stops DNS rebinding: a
page served from a name the attacker controls, resolved to `127.0.0.1`, would
otherwise share this service's origin and read every answer it gives, including
the build log and the ROM.

### Editing from another computer

Authoring from a second machine is an ordinary way to work, and the default
refuses it. Name the address you use:

```sh
node tools/rom_service/serve.mjs --allow-host cruncher
GRANDLEON_ROM_SERVICE_ALLOWED_HOSTS=cruncher,cruncher.local node …/serve.mjs
```

Only the hostname is compared; the editor may serve on any port. The service
prints the names it will answer to, every time it starts, because widening this
should never be something the operator has to remember they did.

**It stays an allow-list rather than becoming a switch, and that is what keeps
the rebinding defence.** A page served from a name an attacker controls is
still refused, because what is compared is the name and not where it resolved
to. Widening `Host` also widens nothing else: `Sec-Fetch-Site` is the check a
page cannot forge, and a cross-site request to an allowed name is still refused
by that.

## What it does when things go wrong

| | |
|---|---|
| no container runtime | `container_runtime_missing`, told apart from a failed build, with the script's own message |
| a build that fails | the job goes to `failed` carrying the last of the toolchain's own output |
| a build that never finishes | killed and failed as `rom_build_timed_out` past `GRANDLEON_ROM_BUILD_TIMEOUT_MS`, default forty-five minutes |
| two builds at once | one runs; the rest queue and are told their position; past the depth, `rom_build_queue_full` |
| a client that goes away | finished jobs and their build trees are reaped after an idle window |
| a service that was killed | its staged trees belong to no job, and the next service's reaper takes them |

The timeout is what keeps the last two rows from being wishes. A build with no
deadline holds the one running slot for as long as the process lives: every
later request is refused `rom_build_queue_full`, the job never records a finish
so the reaper is forbidden to touch its 87 MB of staging, and the editor shows a
spinner rather than an error.

## Why it is a job and not a request

A cold container build of every target measured **1 m 55 s** and **1 m 59 s** on
the machine that developed this; one campaign ROM into a warm tree is under a
minute. A request that blocked for that long would be indistinguishable from a
hang, so a build is enqueued, polled and collected, and the editor shows the
state.

```
POST /api/n64/build          the project as the body -> 202 { id, state, ... }
GET  /api/n64/build/:id      queued | building | done | failed
GET  /api/n64/build/:id/rom  the .z64
GET  /api/n64/health         whether this machine can build at all
```

## Where a build happens

Under `build-n64/requests/<job>/`, inside the repository. `build-n64.sh`
requires that, because only the repository is mounted into the container. The
author's `project.json` is staged there too, so a request never writes over a
checked-in game.

## What checks it

- `node --test tools/rom_service/serve.test.mjs` covers every refusal and every
  failure path, with no container involved. That is deliberate: the service's
  job is to have said no to everything it can before a container exists.
- `grandleon.nintendo64_editor_rom` builds a *different* game through this
  path first, then the shipped project, and requires the second to be the
  toolchain's own ROM byte for byte. The ordering is what makes it non-trivial:
  a stale tree or a cached answer fails on the bytes.
- `grandleon.nintendo64_other_game` builds `games/demo` through this path,
  boots it under ares, and requires the machine to name that game's campaign
  and never the shipped one. No host check can make that claim.
