# The build service

Builds a console image of an author's own project, on this machine, with the
pinned toolchain this repository already uses for every checked image.

```
node tools/rom_service/serve.mjs [--port 4699]
```

Two consoles, one process, a path segment each:

| segment | what it builds | what comes back |
|---|---|---|
| `/api/n64` | `grandleon_n64_campaign` | one `.z64` |
| `/api/playstation` | `grandleon_playstation_campaign`, then a disc | a `.bin` and its `.cue` |

The editor talks to it through Vite's proxy at `/api`. With it running, the
**Nintendo 64 ROM** and **PlayStation disc** buttons beside *Export* are
enabled; without it, each is disabled and says why. A disc arrives as one ZIP
of the two files, because a `.bin` without the `.cue` that is its table of
contents is not something a burning program can read, and the names inside are
the ones the build used: a cue sheet names its own bin.

**A console is a table entry in `serve.mjs`, not a branch.** What it is called,
the words its refusals are said in, the pins its script demands, what it
builds and what it hands back are one object; the routes, the queues and the
health checks are written once over the table. Each console gets a queue of its
own, so a disc and a ROM can be building at once — one build per console, which
is the promise a queue makes about the machine.

There is also a shell form, which is what the console lanes use:

```
node tools/rom_service/build-rom.mjs games/demo/source/project.json out/
node tools/rom_service/build-rom.mjs --console playstation \
    games/demo/source/project.json out/
```

It writes each file under the name the service gave it and prints one `FILE`
line per file. It is not a second path to an image: it starts the service in
its own process and then does exactly what the editor does, over HTTP.

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

| code | when | consoles |
|---|---|---|
| `project_unreadable` | not JSON, or not a JSON object | both |
| `project_without_campaign` | no campaigns, or a first campaign with no id (an image runs one) | both |
| `campaign_id_not_an_identifier` | a first campaign whose id is not a source identifier | both |
| `character_style_not_served` | a style or figure the art library does not hold, whether the game's or a character's | both |
| `character_art_is_not_one_combination` | characters drawn in more than one style or at more than one figure | PlayStation |
| `project_too_large_for_the_console` | source past the accepted size | both |
| `project_does_not_compile` | the host compiler's own diagnostics, verbatim | both |

`project_does_not_compile` earns its place more than the others, differently on
each console. The Nintendo 64 compiles its project *on the console*, so a
project the compiler rejects yields a ROM that builds perfectly, boots, and
stops on an assertion an author cannot read. The PlayStation compiles it in the
container and embeds the result, so the same project spends a host build and a
cross build to arrive at the same diagnostic. The host compiler answers the
question in two seconds either way.

**The mixed-style refusal is the PlayStation's alone, and its absence on the
other console is deliberate.** The Nintendo 64 build embeds the drawings a
project's content actually draws, so a project drawing a `medieval` knight, a
`medieval` mage at the second figure and two `nature` archers is served. The
PlayStation consumes the art library as one generated header per style, all of
them declaring the same symbols, so an executable includes exactly one:
`grandleon_require_single_character_combination` fails that build's configure,
and `character_art_is_not_one_combination` is the same refusal minutes earlier.
Lifting it is an art-library change, described in
`cmake/GrandleonCharacterStyle.cmake`.

What is checked on both is that every style and every figure the content names
(the game's own and each character's) is art the library holds, with the path of
the character that named it. Without that check, art the library does not hold
reaches the build's configure and fails it with a fatal error the author sees as
a container exiting non-zero.

This is also the reason the route is a real build rather than a patched
template: a template could never have served the combination, because there is
one per style-and-figure pairing and templates cannot be built for all of them.

### How large a project may be

512 KiB on both, and on both it is *declared* rather than derived, for two
different reasons.

The Nintendo 64 embeds the source project and parses it into RDRAM on the
console. The shipped project is 87,287 bytes and is known to parse and compile
there; the ceiling has never been measured, so the bound is six times the
largest project known to work, and it exists to turn a project that would
almost certainly fail on the machine into a refusal that costs a second.

The PlayStation does not embed the source at all: it compiles it on the host
and embeds the package. What binds there is the executable fitting in the
console's main RAM, and that is refused where it can be measured —
`platform/playstation/scripts/check-heap-room.sh`, over the image the linker
has just produced, which reports the heap that remains on every build. The
bound here is only so that a request is bounded before it is read.

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

Both are decided before the path has been read, so they are the same words
whichever console was asked for.

This is not paranoia about the network; it is about the browser. Left alone,
`POST /api/<console>/build` needs no header a page has to be given permission to
send,
which makes it a CORS *simple* request: any page an author has open could launch
a two-minute container build on their machine with no preflight and no consent.
Listening on the loopback interface is not a defence, because loopback is
exactly where such a request comes from.

The `Host` check is what the editor's own Vite proxy relies on. That proxy binds
every interface, so the editor is reachable from a tablet on the same desk, and
forwards `/api` here with the browser's `Host` intact. A peer on the
network therefore arrives with a `Host` that is not local and is refused
here, in the one place that can tell. It is also what stops DNS rebinding: a
page served from a name the attacker controls, resolved to `127.0.0.1`, would
otherwise share this service's origin and read every answer it gives, including
the build log and the image.

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
| two builds for one console at once | one runs; the rest queue and are told their position; past the depth, `rom_build_queue_full` |
| a ROM and a disc at once | both run: a queue is one per console |
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
POST /api/<console>/build                     the project as the body -> 202
GET  /api/<console>/build/:id                 queued | building | done | failed
GET  /api/<console>/build/:id/artifact/:name  one finished file
GET  /api/<console>/health                    whether this machine can build
```

A finished build reports `artifacts`, each with the name it will be downloaded
under, its size and its md5. The artifact is addressed by that name rather than
by an index or by a fixed word, because a disc is two files whose names refer
to each other and a caller has to be able to ask for the one it means. A name
the job did not produce is a 404 and never a path this service opens.

A PlayStation disc costs more than a ROM and it is worth knowing why: every
request builds the content compiler on the host and then the executable for the
R3000A, inside the request's own tree, so nothing is warm the way a second ROM
is warm.

## Where a build happens

Under `<console staging>/requests/<job>/` — `build-n64/` or
`build-playstation/` — inside the repository. Both build scripts require that,
because only the repository is mounted into the container. The author's
`project.json` is staged there too, so a request never writes over a
checked-in game. Each queue sweeps its own console's staging and no other's.

## What checks it

- `node --test tools/rom_service/serve.test.mjs` covers every refusal and every
  failure path, with no container involved. That is deliberate: the service's
  job is to have said no to everything it can before a container exists. It
  also holds every console to having its own words for every refusal, so a
  table filled in from the other console — which would read perfectly — fails
  there.
- `editor/src/platform/rom-refusals.test.ts` holds the editor's list of codes
  and its list of consoles against the service's own, so a console added to one
  side and not the other is a failing test rather than a button that asks a
  path nobody answers.
- `grandleon.nintendo64_editor_rom` builds a *different* game through this
  path first, then the shipped project, and requires the second to be the
  toolchain's own ROM byte for byte. The ordering is what makes it non-trivial:
  a stale tree or a cached answer fails on the bytes.
- `grandleon.nintendo64_other_game` builds `games/demo` through this path,
  boots it under ares, and requires the machine to name that game's campaign
  and never the shipped one. No host check can make that claim.
