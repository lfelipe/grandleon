#!/usr/bin/env node
// SPDX-License-Identifier: MIT

// The local console build service.
//
// The editor asks this for a Nintendo 64 ROM or a PlayStation disc of the
// project the author is holding, and it answers with what the pinned toolchain
// builds: the same `platform/nintendo64/scripts/build-n64.sh` and
// `platform/playstation/scripts/build-playstation.sh` the gate runs, with one
// path changed. Nothing is patched into a pre-built image, so there is no
// patcher to be wrong and no byte-identity property to enforce: what an author
// downloads is the checked build because it is the checked build.
//
//   node tools/rom_service/serve.mjs [--port 4699]
//
// Two things shape the interface, and both are consequences of the build being
// real rather than instant.
//
// **It is a job, not a request.** A container build of one campaign image is
// tens of seconds to several minutes. A request that blocked for that long is
// indistinguishable from a hang, so a build is enqueued, polled and then
// collected. The editor shows the state; nobody watches a spinner.
//
// **It refuses before it spends.** Every refusal that can be decided from the
// project alone is decided in milliseconds, before docker is invoked. The
// expensive one is `project_does_not_compile`, and it is the one that matters
// most, differently on each console. The Nintendo 64 compiles its project *on
// the machine*, so a project that does not compile produces a ROM that builds
// perfectly and then dies at boot with nothing useful on screen. The
// PlayStation compiles on the host and embeds the result, so the same project
// spends a container build to fail inside it. The host compiler is asked
// first either way, and its own diagnostics are what comes back.
//
// **A console is a table entry, not a branch.** Everything either console
// needs — the words its refusals are said in, the pins its script demands,
// what it builds, what it hands back — is one object below, and the routes,
// the queues and the health checks are written once over it. A path or a
// message that named a console in prose would be the thing that made a third
// one a rewrite.

import { spawn } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";
import { createServer } from "node:http";
import { mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repositoryRoot = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "..",
    ".."
);

/**
 * The toolchain pins, read from the file that owns them.
 *
 * Every console's build script refuses without its pins, by name.
 * `platform/nintendo64/scripts/build-n64.sh` wants
 * `GRANDLEON_LIBDRAGON_COMMIT`; the PlayStation's two want the image digest,
 * the SDK revision and the disc writer's. Under the gate, cmake supplies them
 * from the files in `cmake/`, and this service asks those same files. A
 * service reading only the environment would accept a job from somebody
 * following `tools/rom_service/README.md`, spend minutes on it, and then hand
 * the author the words "GRANDLEON_LIBDRAGON_COMMIT is not set": an environment
 * variable, in a browser, from a document that never mentioned it.
 *
 * One pin, one home, and the two roads to an image agree because they read it
 * rather than because somebody remembered to export it. An override wins, for
 * a contributor testing a bump.
 */
function pinnedFromCMake(file, name) {
    const source = path.join(repositoryRoot, "cmake", file);
    if (!existsSync(source)) return "";
    const text = readFileSync(source, "utf8");
    // `set(NAME "value" CACHE ...)`, which is how every pin in those files is
    // written. A pin that stops matching this reads as absent, and absent is
    // refused loudly by the build script rather than guessed at.
    const match = new RegExp(`set\\(\\s*${name}\\s*\\n?\\s*"([^"]*)"`).exec(text);
    return match?.[1] ?? "";
}

function pinsFrom(file, names) {
    const resolved = {};
    for (const name of names) resolved[name] = pinnedFromCMake(file, name);
    return resolved;
}

/** The pins a build runs with: the file's, unless the caller overrode one. */
export function toolchainEnvironment(target, overrides = process.env) {
    const resolved = {};
    for (const [name, pinned] of Object.entries(target.pins)) {
        const value = overrides[name] || pinned;
        if (value) resolved[name] = value;
    }
    return resolved;
}

// The words each refusal is said in.
//
// The codes are one set across every console, because what went wrong is the
// same thing; the words are not, because "the Nintendo 64 build failed" said
// about a disc is worse than no message at all. Written once here and given
// each console's own name, so that the editor, the tests and this file cannot
// disagree about what a code means and no console can end up with a message
// about another one.
//
// Two of them are not about a console at all. Where a request came from is
// decided before the path has been read, so those live on their own and are
// spread into every console's table, rather than being one console's words
// used for every console's refusal.
export const requestRefusals = {
    request_from_another_site:
        "That request did not come from the editor running on this machine.",
    request_not_addressed_locally:
        "This service answers only requests addressed to localhost, or to a " +
        "name it was started with. Open the editor at a localhost address, " +
        "or start the service with --allow-host <the name you use>, to " +
        "build for a console."
};

function refusalsFor(name, image, oversize) {
    return {
        ...requestRefusals,
        project_unreadable: "That file is not a Grandleon project.",
        project_without_campaign:
            `This project has no campaign. The ${name} ${image} plays a ` +
            "campaign, so there is nothing for it to run.",
        character_style_not_served:
            "This project asks for character art the art library does not hold.",
        character_art_is_not_one_combination:
            "This project draws characters in more than one way, and the " +
            `${name} ${image} carries one.`,
        project_too_large_for_the_console: oversize,
        campaign_id_not_an_identifier:
            "This project's campaign has an id that is not a source " +
            `identifier, and the ${name} build writes it into the ${image}'s ` +
            "own code.",
        project_does_not_compile:
            "This project does not compile, so it would not run on the console.",
        container_runtime_missing:
            `The container toolchain that builds ${name} ${image}s is not ` +
            "available on this machine.",
        rom_build_failed: `The ${name} build failed.`,
        rom_build_timed_out:
            `The ${name} build ran longer than this service waits and was ` +
            "given up on.",
        rom_build_queue_full:
            "Too many builds are already waiting. Try again when one finishes.",
        rom_build_unknown: "That build is not one this service is holding."
    };
}

// The name a downloaded file is given, out of the project's own id. A file
// name and nothing more: what a console *calls* the game is the package's
// business, and both of them read it from there.
export function artifactBase(project) {
    const named =
        typeof project.gameId === "string" && project.gameId !== ""
            ? project.gameId
            : "grandleon";
    return named.replace(/[^a-z0-9._-]+/gi, "-");
}

// The consoles this service builds for, keyed by the path segment each one is
// addressed at.
export const consoles = {
    // The Nintendo 64's segment is `n64` rather than its own id, because that
    // is the path the editor, the proxy and every document have always used,
    // and renaming it would buy nothing.
    n64: {
        id: "nintendo64",
        name: "Nintendo 64",
        image: "ROM",
        // The image an author wants is the campaign one: it is the build with
        // a title screen, a slot screen and a save. The probe and autopilot
        // variants are the gate's, not an author's.
        target: "grandleon_n64_campaign",
        buildScript: "platform/nintendo64/scripts/build-n64.sh",
        artManifest: "tools/placeholder_art/assets/n64_ci4/manifest.json",
        // Where a request's build tree is staged, inside the repository
        // because only the repository is mounted into the container.
        staging: "build-n64",
        pins: pinsFrom("GrandleonNintendo64.cmake", [
            "GRANDLEON_LIBDRAGON_COMMIT",
            "GRANDLEON_LIBDRAGON_BASE_DIGEST"
        ]),
        toolchainImage: (environment) =>
            `grandleon/n64-toolchain:${
                environment.GRANDLEON_LIBDRAGON_COMMIT ?? ""
            }`,
        // The source project is embedded in the ROM and parsed into RDRAM on
        // the console. The shipped project is 87,287 bytes and is known to
        // parse and compile there; the ceiling has never been measured.
        //
        // So this bound is *declared*, not derived, and it is written down as
        // such: six times the largest project known to work. It exists to turn
        // a project that would almost certainly fail on the machine into a
        // refusal that costs a second, not to mark the exact edge. Measuring
        // the real ceiling on the console is what would replace it.
        projectSizeBudget: 512 * 1024,
        // This console embeds the drawings its content actually draws, so a
        // project drawing a medieval knight, a medieval mage at the second
        // figure and two nature archers is served.
        oneCharacterCombination: false,
        refusals: refusalsFor(
            "Nintendo 64", "ROM",
            "This project is larger than the Nintendo 64 build embeds."
        ),
        runBuild: (job, root, onOutput, timeoutMilliseconds) =>
            runNintendo64Build(job, root, onOutput, timeoutMilliseconds)
    },
    playstation: {
        id: "playstation",
        name: "PlayStation",
        image: "disc",
        target: "grandleon_playstation_campaign",
        buildScript: "platform/playstation/scripts/build-playstation.sh",
        discScript: "platform/playstation/scripts/build-disc.sh",
        // The executable the disc boots. `SYSTEM.CNF` names one and there is
        // no menu on the image to choose between two, so it carries the
        // campaign, which is the thing a person plays.
        executable: "grandleon_psx_campaign.ps-exe",
        artManifest: "tools/placeholder_art/assets/n64_ci4/manifest.json",
        staging: "build-playstation",
        pins: pinsFrom("GrandleonPlayStation.cmake", [
            "GRANDLEON_PCSX_REDUX_BUILD_IMAGE",
            "GRANDLEON_PCSX_REDUX_BUILD_DIGEST",
            "GRANDLEON_NUGGET_REVISION",
            "GRANDLEON_MKPSXISO_REVISION"
        ]),
        toolchainImage: (environment) =>
            `grandleon/playstation-toolchain:${
                (environment.GRANDLEON_PCSX_REDUX_BUILD_DIGEST ?? "")
                    .replace(/^sha256:/, "")
            }`,
        // This console does not embed the source project at all: it compiles
        // it on the host and embeds the package, so the size of the JSON is
        // not what binds. What binds is the executable fitting in the
        // console's main RAM, and that is refused where it can be measured, by
        // `platform/playstation/scripts/check-heap-room.sh` over the image the
        // linker has just produced, rather than guessed at here from a source
        // file.
        //
        // So this bound is *declared*, and for a smaller reason than the other
        // console's: a request has to be bounded by something before it is
        // read. It is deliberately the same number, so that a project one
        // console will consider is a project the other will consider.
        projectSizeBudget: 512 * 1024,
        // And this console cannot do what the other one does. The art library
        // emits one character header per style and they all declare the same
        // symbols, so an executable includes exactly one;
        // `grandleon_require_single_character_combination` is the
        // configure-time refusal, and this is that refusal several minutes
        // earlier.
        oneCharacterCombination: true,
        refusals: refusalsFor(
            "PlayStation", "disc",
            "This project is larger than the PlayStation build service accepts."
        ),
        runBuild: (job, root, onOutput, timeoutMilliseconds) =>
            runPlayStationBuild(job, root, onOutput, timeoutMilliseconds)
    }
};

// How many builds may be waiting behind the one in flight. A queue is a
// courtesy to a second author; an unbounded queue is a way to accumulate two
// hours of work nobody is waiting for any more.
const queueDepth = 2;

// A finished job's artifacts are kept this long so the browser can collect
// them, then the tree goes. Long enough for a download, short enough that a
// service left running overnight is not holding build trees.
const jobIdleMilliseconds = 30 * 60 * 1000;

// How long one build may take before it is given up on.
//
// Declared rather than derived, and generously. A warm Nintendo 64 build of
// one campaign ROM is a couple of minutes; a PlayStation disc is a host build,
// a cross build and a disc writer in one request; and the first build on a
// machine also builds a toolchain image from source, which is the long one.
// So this sits above any build that is going to finish and below the budget of
// the checks around it.
//
// It exists so that a build which will never finish (a container waiting on
// something, a toolchain that wedged) ends as a named failure instead of
// holding the one running slot for as long as the service lives. Every later
// request is then refused `rom_build_queue_full`, the job never records a
// finish so the reaper may not take its staging, and the editor shows a
// spinner rather than an error: an unbounded build is not one slow request,
// it is a dead service.
export const buildTimeoutMilliseconds = Number(
    process.env.GRANDLEON_ROM_BUILD_TIMEOUT_MS ?? 45 * 60 * 1000
);

export class RefusalError extends Error {
    // `status` is the HTTP status this refusal answers with. 400 for everything
    // decided from the project, because the project is what was wrong with the
    // request; the two refusals about where a request came from say 403,
    // because nothing about the body would have made them yes.
    constructor(code, message, detail = "", status = 400) {
        super(message);
        this.code = code;
        this.detail = detail;
        this.status = status;
    }
}

// Every refusal this service can make, with the words it says them in. Named
// in one table so that the editor, the tests and this file cannot disagree
// about what a code means.
export const refusals = {
    project_unreadable: "That file is not a Grandleon project.",
    project_without_campaign:
        "This project has no campaign. The Nintendo 64 ROM plays a campaign, " +
        "so there is nothing for it to run.",
    character_style_not_served:
        "This project asks for character art the art library does not hold.",
    project_too_large_for_the_console:
        "This project is larger than the Nintendo 64 build embeds.",
    campaign_id_not_an_identifier:
        "This project's campaign has an id that is not a source identifier, " +
        "and the Nintendo 64 build writes it into the ROM's own code.",
    project_does_not_compile:
        "This project does not compile, so it would not run on the console.",
    container_runtime_missing:
        "The container toolchain that builds Nintendo 64 ROMs is not available " +
        "on this machine.",
    rom_build_failed: "The Nintendo 64 build failed.",
    rom_build_timed_out:
        "The Nintendo 64 build ran longer than this service waits and was " +
        "given up on.",
    rom_build_queue_full:
        "Too many ROM builds are already waiting. Try again when one finishes.",
    rom_build_unknown: "That build is not one this service is holding.",
    request_from_another_site:
        "That request did not come from the editor running on this machine.",
    request_not_addressed_locally:
        "This service answers only requests addressed to localhost, or to a " +
        "name it was started with. Open the editor at a localhost address, " +
        "or start the service with --allow-host <the name you use>, to " +
        "build a ROM."
};

// ---------------------------------------------------------------------------
// Who is allowed to ask

// The source contract's stable identifier, the same pattern
// `schemas/source/v1/common.schema.json` states.
const stableIdentifier = /^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$/;

// Names this machine may be reached by, beyond loopback, because whoever
// started the service said so.
//
// Editing from a second computer is an ordinary way to use this: the editor
// binds every interface, and a person opens it at this machine's name. Their
// browser is then on the editor's own origin, so `Sec-Fetch-Site` below still
// says `same-origin` and still refuses another site's page — the only test
// such a request fails is the `Host` one, because the proxy forwards the name
// they typed.
//
// It is an allow-list and not a switch, which is what keeps the rebinding
// defence intact: a name an attacker controls is refused because it is not in
// this list, not because of where it resolved to. Naming a wildcard is not
// possible, deliberately.
//
//     node tools/rom_service/serve.mjs --allow-host cruncher
//     GRANDLEON_ROM_SERVICE_ALLOWED_HOSTS=cruncher,cruncher.local
//
// Only the hostname is compared; a port is not part of the question, since the
// editor may serve on any.
function configuredHosts(environment = process.env, argv = []) {
    const named = [];
    for (let index = 0; index < argv.length; index += 1) {
        if (argv[index] === "--allow-host") named.push(argv[index + 1] ?? "");
        else if (argv[index]?.startsWith("--allow-host=")) {
            named.push(argv[index].slice("--allow-host=".length));
        }
    }
    named.push(...(environment.GRANDLEON_ROM_SERVICE_ALLOWED_HOSTS ?? "")
        .split(","));
    return named
        .map((name) => name.trim().toLowerCase())
        .filter((name) => name !== "");
}

let allowedHosts = configuredHosts();

export function setAllowedHosts(hosts) {
    allowedHosts = hosts.map((name) => name.trim().toLowerCase())
        .filter((name) => name !== "");
    return allowedHosts;
}

export { configuredHosts };

// Hostnames that mean "this machine". A request addressed to anything else is
// either a DNS-rebinding page (a name the attacker controls, resolved to
// 127.0.0.1) or a peer on the network reaching the editor's own proxy, which
// forwards the browser's `Host` verbatim. Both are refused here, in the one
// place that can tell, rather than in the proxy that cannot — unless the
// operator has named the host themselves, above.
function addressedLocally(host) {
    if (typeof host !== "string" || host === "") return false;
    // `URL` is the parser rather than a split on ':', because an IPv6 literal
    // is bracketed and full of them.
    let hostname;
    try {
        hostname = new URL(`http://${host}`).hostname;
    } catch {
        return false;
    }
    if (hostname === "localhost") return true;
    if (hostname === "[::1]" || hostname === "::1") return true;
    if (/^127\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(hostname)) return true;
    return allowedHosts.includes(hostname.toLowerCase());
}

// Refuse a request that some other page drove, and answer with the origin this
// service was actually addressed as.
//
// `POST /api/<console>/build` carries no header a browser has to be given permission
// to send, so left alone it is a CORS *simple* request: any page an author
// visits can launch a two-minute container build on their machine, and no
// preflight is asked. Binding to the loopback interface does not help, because
// that is exactly where such a request comes from.
//
// Two headers decide it, and both are ones a browser writes and a page cannot
// forge. `Sec-Fetch-Site` says whether the fetch came from this service's own
// origin. `Origin` says which one, when one is stated at all. A caller that
// states neither (curl, a test, a script on this machine) is not a page and is
// answered, which is the same trust boundary `Host` above draws.
export function refuseForeignRequest(request) {
    const host = request.headers?.host;
    if (!addressedLocally(host)) {
        throw new RefusalError(
            "request_not_addressed_locally",
            requestRefusals.request_not_addressed_locally,
            `addressed to '${host ?? ""}'`,
            403
        );
    }
    const site = request.headers?.["sec-fetch-site"];
    if (site !== undefined && site !== "same-origin" && site !== "none") {
        throw new RefusalError(
            "request_from_another_site",
            requestRefusals.request_from_another_site,
            `Sec-Fetch-Site: ${site}`,
            403
        );
    }
    const origin = request.headers?.origin;
    if (origin !== undefined) {
        let stated = null;
        try {
            stated = new URL(origin).host;
        } catch {
            stated = null;
        }
        if (stated !== host) {
            throw new RefusalError(
                "request_from_another_site",
                requestRefusals.request_from_another_site,
                `Origin: ${origin}`,
                403
            );
        }
    }
    return `http://${host}`;
}

// ---------------------------------------------------------------------------
// The refusals that need only the project

// The art library's style menu, read from the manifest rather than listed
// here, so a style added to the library is served without touching this file.
//
// One manifest for every console, because it is one library. A console names
// the file it reads rather than this function assuming it, and today they name
// the same one.
export async function servedCharacterStyles(
    root = repositoryRoot, target = consoles.n64
) {
    const manifest = JSON.parse(
        await readFile(path.join(root, target.artManifest), "utf8")
    );
    return {
        menu: manifest.character_styles.menu.map((entry) => entry.name),
        fallback: manifest.character_styles.default,
        figures: manifest.character_styles.figures.menu.map(
            (entry) => entry.name
        ),
        figureFallback: manifest.character_styles.figures.default
    };
}

// Refuse what this service cannot serve, from the project alone.
//
// Deliberately ordered cheapest-first and deliberately total: by the time this
// returns, the only things that can still go wrong are the machine's: no
// container, or a build that breaks. Everything about the *project* has been
// decided.
export function refuseProject(text, styles, target = consoles.n64) {
    const refusals = target.refusals;
    let project;
    try {
        project = JSON.parse(text);
    } catch (error) {
        throw new RefusalError(
            "project_unreadable",
            refusals.project_unreadable,
            String(error.message)
        );
    }
    if (project === null || typeof project !== "object" ||
        Array.isArray(project)) {
        throw new RefusalError(
            "project_unreadable",
            refusals.project_unreadable,
            "the document is not a JSON object"
        );
    }

    const bytes = Buffer.byteLength(text, "utf8");
    if (bytes > target.projectSizeBudget) {
        throw new RefusalError(
            "project_too_large_for_the_console",
            refusals.project_too_large_for_the_console,
            `${bytes} bytes; this service accepts at most ` +
                `${target.projectSizeBudget}`
        );
    }

    if (!Array.isArray(project.campaigns) || project.campaigns.length === 0) {
        throw new RefusalError(
            "project_without_campaign",
            refusals.project_without_campaign,
            "the project carries no campaigns"
        );
    }
    if (typeof project.campaigns[0]?.id !== "string" ||
        project.campaigns[0].id === "") {
        throw new RefusalError(
            "project_without_campaign",
            refusals.project_without_campaign,
            "the first campaign has no id, so the ROM could not name it"
        );
    }
    // The Nintendo 64 build writes this id into a C++ string literal in a
    // generated header, so it is code rather than data by the time the ROM is
    // compiled: an id carrying a quote closes the literal, and one carrying a
    // `#include` names a file the container's preprocessor then reads. Project
    // files are content people share, so "open somebody else's project and
    // press Build" has to be safe. The identifier grammar the source contract
    // already states is the answer: a campaign id that is a source identifier
    // cannot be anything but a name.
    if (!stableIdentifier.test(project.campaigns[0].id)) {
        throw new RefusalError(
            "campaign_id_not_an_identifier",
            refusals.campaign_id_not_an_identifier,
            `$.campaigns[0].id: '${project.campaigns[0].id}' is not a source ` +
                "identifier (lowercase letters and digits, separated by " +
                "'.', '_' or '-')"
        );
    }

    // Every style and every figure the content draws (the game's own, and each
    // character's where it names one) has to be art the library holds.
    // Anything else fails the build's configure with a fatal error, and a
    // refusal the service can name beats a container exiting non-zero.
    //
    // Naming nothing is not naming something wrong: a project or a character
    // that names no style is drawn in the library's default, exactly as it is
    // by every console build.
    //
    // **There is no mixed-style refusal here any more, and its absence is the
    // point.** There was one, written in the shape it would have had and
    // marked unreachable because `unitType.characterStyleId` was not yet in
    // the schema. The field landed, which made it reachable; and then the
    // Nintendo 64 build learned to embed the drawings a project's content
    // actually draws, which made it false. A project drawing a `medieval`
    // knight, a `medieval` mage at the second figure and two `nature` archers
    // is served. It is the campaign this route exists for.
    const drawings = [
        ["$.characterStyleId", project.characterStyleId, styles.menu, "style"],
        ["$.characterFigureId", project.characterFigureId, styles.figures,
            "figure"]
    ];
    const unitTypes =
        Array.isArray(project.unitTypes) ? project.unitTypes : [];
    unitTypes.forEach((unit, index) => {
        drawings.push([
            `$.unitTypes[${index}].characterStyleId`,
            unit?.characterStyleId, styles.menu, "style"
        ]);
        drawings.push([
            `$.unitTypes[${index}].characterFigureId`,
            unit?.characterFigureId, styles.figures, "figure"
        ]);
    });
    for (const [where, named, menu, axis] of drawings) {
        if (typeof named !== "string" || named === "") continue;
        if (menu.includes(named)) continue;
        throw new RefusalError(
            "character_style_not_served",
            refusals.character_style_not_served,
            `${where}: '${named}' is not a ${axis} the art library holds ` +
                `(${menu.join(", ")})`
        );
    }

    // **And the refusal the paragraph above says the other console does not
    // make.** The PlayStation consumes the art library as one generated header
    // per style, all of them declaring the same symbols, so an executable
    // includes exactly one and a project drawing two combinations cannot be
    // built at all. `grandleon_require_single_character_combination` refuses it
    // at configure time, several minutes into a container build; this is the
    // same refusal, from the same project, before one starts.
    //
    // The two axes are counted together because the console carries one of
    // each: a project drawing `medieval` at two figures is as unbuildable as
    // one drawing two styles.
    if (target.oneCharacterCombination) {
        const combinations = new Set();
        const named = (value, fallback) =>
            typeof value === "string" && value !== "" ? value : fallback;
        const style = named(project.characterStyleId, styles.fallback);
        const figure = named(project.characterFigureId, styles.figureFallback);
        combinations.add(`${style}/${figure}`);
        for (const unit of unitTypes) {
            combinations.add(`${
                named(unit?.characterStyleId, style)
            }/${
                named(unit?.characterFigureId, figure)
            }`);
        }
        if (combinations.size > 1) {
            throw new RefusalError(
                "character_art_is_not_one_combination",
                refusals.character_art_is_not_one_combination,
                `this project's characters are drawn as ` +
                    `${[...combinations].sort().join(", ")}`
            );
        }
    }

    return project;
}

// ---------------------------------------------------------------------------
// The refusal that needs the compiler

// Where a host build put the content compiler. The service does not build it.
// It is the same binary `tools/game_content/main.cpp` produces for everything
// else, and asking a service to build the repository would be a surprise.
export function locateCompiler(root = repositoryRoot) {
    for (const candidate of [
        path.join(root, "build", "grandleon_content_compile"),
        path.join(root, "build-clang", "grandleon_content_compile")
    ]) {
        if (existsSync(candidate)) return candidate;
    }
    return null;
}

function run(command, args, options = {}) {
    return new Promise((resolve) => {
        const child = spawn(command, args, {
            cwd: options.cwd ?? repositoryRoot,
            env: { ...process.env, ...(options.env ?? {}) },
            // Node kills the child itself when this elapses. The queue races
            // the same deadline, because a child that ignores the signal must
            // not decide how long the one running slot is held.
            ...(options.timeoutMilliseconds === undefined
                ? {}
                : {
                    timeout: options.timeoutMilliseconds,
                    killSignal: "SIGKILL"
                })
        });
        let stdout = "";
        let stderr = "";
        child.stdout.on("data", (chunk) => {
            stdout += chunk;
            options.onOutput?.(String(chunk));
        });
        child.stderr.on("data", (chunk) => {
            stderr += chunk;
            options.onOutput?.(String(chunk));
        });
        child.on("error", (error) => {
            resolve({ code: -1, stdout, stderr: stderr + String(error.message) });
        });
        child.on("close", (code) => resolve({ code, stdout, stderr }));
    });
}

// Compile the project on the host before building a ROM of it.
//
// This is the check no console can make for us in time, for two different
// reasons. The Nintendo 64 compiles its project at boot, so a project the
// compiler rejects yields a ROM that builds, boots, and stops on a failed
// assertion the author cannot read. The PlayStation compiles it in the
// container, so the same project spends a host build and a cross build to
// arrive at the same diagnostic. Two seconds here replaces both.
export async function refuseUncompilable(
    projectPath, compiler, workDir, target = consoles.n64
) {
    if (compiler === null) return;
    const output = path.join(workDir, "probe.gpk");
    const result = await run(compiler, [projectPath, output]);
    if (result.code !== 0) {
        throw new RefusalError(
            "project_does_not_compile",
            target.refusals.project_does_not_compile,
            // The compiler's own diagnostics, verbatim. A message this service
            // composed would be a second opinion about content, which is
            // exactly the thing there is only supposed to be one of.
            (result.stderr || result.stdout).trim()
        );
    }
}

// ---------------------------------------------------------------------------
// Is the machine able to build at all?

export async function health(
    target = consoles.n64, root = repositoryRoot, options = {}
) {
    const docker = options.docker ?? process.env.GRANDLEON_DOCKER ?? "docker";
    const image = target.toolchainImage(toolchainEnvironment(target));
    const compiler = locateCompiler(root);

    const runtime = await run(docker, ["version", "--format", "{{.Server.Os}}"]);
    if (runtime.code !== 0) {
        return {
            ready: false,
            code: "container_runtime_missing",
            message: target.refusals.container_runtime_missing,
            detail: `'${docker}' did not answer: ` +
                `${(runtime.stderr || runtime.stdout).trim()}`,
            console: target.id
        };
    }
    const present = await run(docker, ["image", "inspect", image]);
    return {
        ready: true,
        console: target.id,
        // Not a refusal: every build script here builds its toolchain image
        // when it is missing. It is reported because the first build then
        // takes tens of minutes instead of a few, and an author is owed that
        // warning before they press the button rather than after.
        toolchainImagePresent: present.code === 0,
        // Also not a refusal. Without the host compiler the service still
        // builds, it just cannot refuse an uncompilable project early, so it
        // is said out loud rather than silently degraded.
        compilerPresent: compiler !== null
    };
}

// ---------------------------------------------------------------------------
// The jobs

// One queue per console, and that is a decision rather than an accident of
// the shape.
//
// A queue is a promise about the machine: one build at a time, because two
// container builds racing for the same cores make both slower and neither
// clearer. That promise is about the machine and not about the console, so the
// service holds a queue for each and lets a disc and a ROM run at once, which
// is the arrangement an author who wants both actually asks for. What stops
// that from being two of everything is that the queue does not know which
// console it is: it is told one, and the console is what carries the words,
// the pins and the build.
export class BuildQueue {
    constructor(options = {}) {
        this.target = options.target ?? consoles.n64;
        this.root = options.root ?? repositoryRoot;
        this.jobs = new Map();
        this.waiting = [];
        this.running = null;
        // Injected so the tests can exercise every state without a container.
        this.runBuild = options.runBuild ?? this.target.runBuild;
        this.now = options.now ?? (() => Date.now());
        this.buildTimeoutMilliseconds =
            options.buildTimeoutMilliseconds ?? buildTimeoutMilliseconds;
    }

    get(id) {
        return this.jobs.get(id) ?? null;
    }

    // Accept a validated project. Refusals have already been raised by the
    // caller, so the only thing that can be said no to here is congestion.
    enqueue(projectText, project) {
        if (this.waiting.length >= queueDepth) {
            throw new RefusalError(
                "rom_build_queue_full",
                this.target.refusals.rom_build_queue_full,
                `${this.waiting.length} already waiting`
            );
        }
        const id = randomUUID();
        const job = {
            id,
            state: this.running === null ? "building" : "queued",
            target: this.target,
            console: this.target.id,
            gameId: typeof project.gameId === "string" ? project.gameId : "",
            title: typeof project.title === "string" ? project.title : "",
            campaign: project.campaigns[0].id,
            // What the files this build produces are called. Decided when the
            // job is accepted rather than when it finishes, because a disc's
            // cue sheet names its own bin: the build has to be told the name,
            // not asked for one afterwards.
            base: artifactBase(project),
            projectText,
            log: "",
            // Every file a finished build hands back, as
            // `{ name, path, bytes, md5 }`. A list rather than one path
            // because a disc is two files and a `.bin` without its `.cue` is
            // not a disc; the Nintendo 64's list happens to have one entry.
            artifacts: [],
            error: null,
            queuedAt: this.now(),
            finishedAt: 0
        };
        this.jobs.set(id, job);
        this.waiting.push(job);
        void this.#pump();
        return job;
    }

    // Where a job sits, from the outside. `position` is the honest answer to
    // "how long?" that this service can actually give: it knows how many
    // builds are ahead, and it does not know how long one takes on this
    // machine, so it says the first and not the second.
    describe(job) {
        const position = this.waiting.indexOf(job);
        return {
            id: job.id,
            state: job.state,
            console: job.console,
            title: job.title,
            campaign: job.campaign,
            position: position < 0 ? 0 : position,
            waiting: this.waiting.length,
            artifacts: job.artifacts.map(({ name, bytes, md5 }) => ({
                name, bytes, md5
            })),
            // Bounded: the tail is what says why a build failed, and the head
            // of a libdragon build is the same two hundred lines every time.
            log: job.log.slice(-4000),
            error: job.error
        };
    }

    async #pump() {
        if (this.running !== null) return;
        const job = this.waiting[0];
        if (job === undefined) return;
        this.running = job;
        job.state = "building";
        let deadline;
        try {
            // The one running slot is held for a bounded time whatever the
            // build does with itself. `runBuild` is asked to stop as well, and
            // that is what actually kills the container. The queue does not
            // wait to find out whether it obeyed, because a build that never
            // returns would otherwise refuse every later request for as long
            // as the service lives.
            const built = await Promise.race([
                this.runBuild(
                    job,
                    this.root,
                    (chunk) => { job.log += chunk; },
                    this.buildTimeoutMilliseconds
                ),
                new Promise((_resolve, reject) => {
                    deadline = setTimeout(() => {
                        reject(new RefusalError(
                            "rom_build_timed_out",
                            this.target.refusals.rom_build_timed_out,
                            `nothing built after ` +
                                `${this.buildTimeoutMilliseconds} ms`
                        ));
                    }, this.buildTimeoutMilliseconds);
                    deadline.unref?.();
                })
            ]);
            job.artifacts = built;
            job.state = "done";
        } catch (error) {
            job.state = "failed";
            job.error = {
                code: error instanceof RefusalError
                    ? error.code
                    : "rom_build_failed",
                message: error instanceof RefusalError
                    ? error.message
                    : this.target.refusals.rom_build_failed,
                detail: error instanceof RefusalError
                    ? error.detail
                    : String(error.message)
            };
        } finally {
            clearTimeout(deadline);
            job.finishedAt = this.now();
            this.waiting.shift();
            this.running = null;
            void this.#pump();
        }
    }

    // Drop what nobody is coming back for, and take its build tree with it.
    //
    // Also takes the trees of jobs this service does not hold at all. A
    // Nintendo 64 build stages 87 MB under `build-n64/requests/<id>/` and a
    // PlayStation one several times that, since it carries a host build and a
    // cross build; a service that was killed, or one whose process simply
    // ended, leaves that behind with nobody left who knows the id. The
    // directory names *are* the job ids, so "no job owns this" is a question
    // the filesystem can be asked directly.
    //
    // Each queue sweeps its own console's staging and no other's, which is
    // what keeps two queues from taking each other's live build trees: the
    // other console's ids are not in this queue's map and would look
    // abandoned.
    async reap(idleMilliseconds = jobIdleMilliseconds) {
        const cutoff = this.now() - idleMilliseconds;
        for (const [id, job] of [...this.jobs]) {
            if (job.finishedAt === 0 || job.finishedAt > cutoff) continue;
            this.jobs.delete(id);
            await this.#discard(id);
        }
        const staged = await readdir(this.#requests(), { withFileTypes: true })
            .catch(() => []);
        for (const entry of staged) {
            if (!entry.isDirectory() || this.jobs.has(entry.name)) continue;
            await this.#discard(entry.name);
        }
    }

    #requests() {
        return path.join(this.root, this.target.staging, "requests");
    }

    async #discard(id) {
        await rm(path.join(this.#requests(), id), {
            recursive: true,
            force: true
        }).catch(() => {});
    }
}

// Where a request is staged, and why it is inside the repository: only the
// repository is mounted into the container, so a project outside it is
// invisible, and a request must never write over a checked-in game.
async function stageRequest(job, root) {
    const requestDir = path.join(
        root, job.target.staging, "requests", job.id
    );
    await mkdir(requestDir, { recursive: true });
    const projectPath = path.join(requestDir, "project.json");
    await writeFile(projectPath, job.projectText);

    // The last refusal, and the only one that needs a tool. Made here rather
    // than at the door because it costs a subprocess, and made *before* the
    // container because the container costs a hundred times more.
    await refuseUncompilable(
        projectPath, locateCompiler(root), requestDir, job.target
    );
    return { requestDir, projectPath, buildDir: path.join(requestDir, "build") };
}

// What a failed script means, told apart by the only distinction an author can
// act on: install a container runtime, or read what the toolchain said.
function refuseFailedBuild(target, result) {
    const output = (result.stderr || result.stdout).trim();
    if (/is not on PATH|Cannot connect to the Docker daemon/i.test(output)) {
        throw new RefusalError(
            "container_runtime_missing",
            target.refusals.container_runtime_missing,
            output
        );
    }
    throw new RefusalError(
        "rom_build_failed",
        target.refusals.rom_build_failed,
        output.split("\n").slice(-40).join("\n")
    );
}

// One finished file, weighed and fingerprinted so the editor can say what it
// is about to hand over before it hands it over.
async function collected(name, filePath) {
    const bytes = await readFile(filePath);
    return {
        name,
        path: filePath,
        bytes: bytes.length,
        md5: createHash("md5").update(bytes).digest("hex")
    };
}

// The real Nintendo 64 build: stage the project inside the repository, then
// run the same script the gate runs.
async function runNintendo64Build(job, root, onOutput, timeoutMilliseconds) {
    const target = job.target;
    const { projectPath, buildDir } = await stageRequest(job, root);

    const result = await run(
        path.join(root, target.buildScript),
        ["--project", projectPath, "--targets", target.target],
        {
            cwd: root,
            env: {
                GRANDLEON_N64_BUILD_DIR: buildDir,
                ...toolchainEnvironment(target)
            },
            onOutput,
            timeoutMilliseconds
        }
    );
    if (result.code !== 0) refuseFailedBuild(target, result);

    return [await collected(
        `${job.base}.z64`,
        path.join(
            buildDir, "platform", "nintendo64", `${target.target}.z64`
        )
    )];
}

// The real PlayStation build, which is two scripts rather than one.
//
// `build-playstation.sh` produces the campaign executable and `build-disc.sh`
// writes it onto an image, and they are separate here because they are
// separate in the gate: the disc target runs exactly this pair. A `.ps-exe` is
// not something a person can use — an emulator takes one only if told two
// flags nobody guesses, and a console will not take one at all — so the disc
// is the artifact and the executable is a step on the way to it.
//
// Two files come back, and both of them. The bin is 2352-byte Mode 2 Form 1
// sectors with no table of contents in them and the cue is the table of
// contents, so burning software handed the bin alone has nothing to tell it
// where the track starts. The cue also names its bin by file name, which is
// why the disc is built under the name it will be downloaded under rather than
// renamed afterwards: a renamed pair is a cue pointing at a file that is not
// there.
//
// A request costs more here than on the other console. The script runs a host
// build of the content compiler and then the cross build, both inside the
// request's own tree, so the first disc on a machine is several minutes and
// several hundred megabytes of staging. The reaper takes it on the same
// schedule it takes a ROM's.
async function runPlayStationBuild(job, root, onOutput, timeoutMilliseconds) {
    const target = job.target;
    const { projectPath, buildDir } = await stageRequest(job, root);
    const environment = {
        GRANDLEON_PLAYSTATION_BUILD_DIR: buildDir,
        ...toolchainEnvironment(target)
    };

    const built = await run(
        path.join(root, target.buildScript),
        ["--project", projectPath, "--targets", target.target],
        { cwd: root, env: environment, onOutput, timeoutMilliseconds }
    );
    if (built.code !== 0) refuseFailedBuild(target, built);

    const cut = await run(
        path.join(root, target.discScript),
        [],
        {
            cwd: root,
            env: {
                ...environment,
                GRANDLEON_PLAYSTATION_DISC_EXECUTABLE: target.executable,
                GRANDLEON_PLAYSTATION_DISC_NAME: job.base,
                // The volume identifier, which is what a disc browser and a
                // burning program show the disc as. ISO 9660 level 1 allows
                // thirty-two characters of A-Z, 0-9 and underscore, so the
                // game's own id is folded into that alphabet rather than
                // cut to a name this service invented.
                GRANDLEON_PLAYSTATION_DISC_VOLUME:
                    job.base.toUpperCase().replace(/[^A-Z0-9_]/g, "_")
                        .slice(0, 32)
            },
            onOutput,
            timeoutMilliseconds
        }
    );
    if (cut.code !== 0) refuseFailedBuild(target, cut);

    const disc = path.join(buildDir, "disc");
    return [
        await collected(`${job.base}.bin`, path.join(disc, `${job.base}.bin`)),
        await collected(`${job.base}.cue`, path.join(disc, `${job.base}.cue`))
    ];
}

// ---------------------------------------------------------------------------
// The HTTP surface

function sendJson(response, status, body) {
    const text = JSON.stringify(body);
    response.writeHead(status, {
        "content-type": "application/json",
        "content-length": Buffer.byteLength(text)
    });
    response.end(text);
}

async function readBody(request, limit, target) {
    const chunks = [];
    let total = 0;
    for await (const chunk of request) {
        total += chunk.length;
        if (total > limit) throw new RefusalError(
            "project_too_large_for_the_console",
            target.refusals.project_too_large_for_the_console,
            `more than ${limit} bytes were sent`
        );
        chunks.push(chunk);
    }
    return Buffer.concat(chunks).toString("utf8");
}

// Four routes, one set, one per console:
//
//   GET  /api/<console>/health                       can this machine build?
//   POST /api/<console>/build                        the project as the body
//   GET  /api/<console>/build/:id                    queued|building|done|failed
//   GET  /api/<console>/build/:id/artifact/:name     one finished file
//
// The artifact is addressed by its own name rather than by an index or by a
// fixed word like `rom`, because a disc is two files whose names refer to each
// other and a caller has to be able to ask for the one it means. The names are
// the job's own, so a request for a name this job did not produce is a 404
// rather than a path this service is asked to open.
export function createRomService(options = {}) {
    const root = options.root ?? repositoryRoot;
    // One queue per console. A caller may supply the ones it wants to hold
    // itself — a test that drives a build, `build-rom.mjs` that reads a job's
    // log — and the rest are made here, so that every console this service
    // names is a console it can actually answer for.
    const supplied = options.queues ?? {};
    const queues = Object.fromEntries(
        Object.entries(consoles).map(([route, target]) => [
            route, supplied[route] ?? new BuildQueue({ root, target })
        ])
    );

    const server = createServer(async (request, response) => {
        try {
            // Before anything is read off the request, and before the URL is
            // built, because the base of that URL is the `Host` this checks.
            const origin = refuseForeignRequest(request);
            const url = new URL(request.url ?? "/", origin);
            const parts = url.pathname.split("/").filter(Boolean);
            const target = parts[0] === "api" ? consoles[parts[1]] : undefined;
            if (target !== undefined) {
                const queue = queues[parts[1]];
                // GET /api/<console>/health
                if (request.method === "GET" && parts.length === 3 &&
                    parts[2] === "health") {
                    return sendJson(
                        response, 200, await health(target, root, options)
                    );
                }
                // POST /api/<console>/build
                if (request.method === "POST" && parts.length === 3 &&
                    parts[2] === "build") {
                    const text = await readBody(
                        request, target.projectSizeBudget * 2, target
                    );
                    const styles = await servedCharacterStyles(root, target);
                    const project = refuseProject(text, styles, target);
                    const job = queue.enqueue(text, project);
                    return sendJson(response, 202, queue.describe(job));
                }
                // GET /api/<console>/build/:id and .../artifact/:name
                if (request.method === "GET" && parts.length >= 4 &&
                    parts[2] === "build") {
                    const job = queue.get(parts[3]);
                    if (job === null) {
                        return sendJson(response, 404, {
                            code: "rom_build_unknown",
                            message: target.refusals.rom_build_unknown
                        });
                    }
                    if (parts[4] === undefined) {
                        return sendJson(response, 200, queue.describe(job));
                    }
                    if (parts[4] !== "artifact" || parts[5] === undefined) {
                        return sendJson(response, 404, {
                            code: "not_found",
                            message: "No such endpoint."
                        });
                    }
                    if (job.state !== "done") {
                        return sendJson(response, 409, {
                            code: "rom_build_unfinished",
                            message: `That build has not produced a ${
                                target.image
                            }.`,
                            state: job.state
                        });
                    }
                    const wanted = decodeURIComponent(parts[5]);
                    const artifact = job.artifacts.find(
                        (entry) => entry.name === wanted
                    );
                    if (artifact === undefined) {
                        return sendJson(response, 404, {
                            code: "rom_build_unknown",
                            message: "That build produced no such file.",
                            detail: `it produced ${
                                job.artifacts.map((one) => one.name).join(", ")
                            }`
                        });
                    }
                    const bytes = await readFile(artifact.path);
                    response.writeHead(200, {
                        "content-type": "application/octet-stream",
                        "content-length": bytes.length,
                        "content-disposition":
                            `attachment; filename="${artifact.name}"`
                    });
                    return response.end(bytes);
                }
            }
            return sendJson(response, 404, {
                code: "not_found",
                message: "No such endpoint."
            });
        } catch (error) {
            if (error instanceof RefusalError) {
                return sendJson(response, error.status, {
                    code: error.code,
                    message: error.message,
                    detail: error.detail
                });
            }
            return sendJson(response, 500, {
                code: "rom_service_failed",
                message: "The build service failed.",
                detail: String(error?.message ?? error)
            });
        }
    });

    // The reaper belongs to the service rather than to the command line: a
    // build tree is tens to hundreds of megabytes, and a service embedded in
    // something else holds them exactly as long as one started from a shell
    // does.
    const reaper = setInterval(() => {
        for (const queue of Object.values(queues)) void queue.reap();
    }, 60_000);
    reaper.unref?.();
    server.on("close", () => clearInterval(reaper));
    return server;
}

const invokedDirectly =
    process.argv[1] !== undefined &&
    path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);

if (invokedDirectly) {
    const portIndex = process.argv.indexOf("--port");
    const port = portIndex >= 0
        ? Number(process.argv[portIndex + 1])
        : Number(process.env.GRANDLEON_ROM_SERVICE_PORT ?? 4699);
    setAllowedHosts(configuredHosts(process.env, process.argv.slice(2)));
    const queues = Object.fromEntries(
        Object.entries(consoles).map(([route, target]) => [
            route, new BuildQueue({ target })
        ])
    );
    const server = createRomService({ queues });
    // Once at startup as well as every minute afterwards: these queues hold no
    // jobs yet, so every tree under a console's `requests/` belongs to a
    // service process that has already exited.
    for (const queue of Object.values(queues)) void queue.reap();
    // Loopback, because a build service is a thing you run for yourself and
    // binding every interface by accident is how it stops being that.
    //
    // `GRANDLEON_ROM_SERVICE_BIND` is for the one arrangement where loopback is
    // the wrong answer: under `compose.yaml` the editor is a *different
    // container*, so it reaches this one by service name over the compose
    // network and 127.0.0.1 here is nobody. What keeps that from publishing a
    // build service is unchanged and is the part that matters — the `Host`
    // check still refuses anything that is not loopback or a name this process
    // was started with, and compose does not publish this port.
    const bind = process.env.GRANDLEON_ROM_SERVICE_BIND ?? "127.0.0.1";
    server.listen(port, bind, () => {
        process.stdout.write(`Build service on http://${bind}:${port}\n`);
        for (const [route, target] of Object.entries(consoles)) {
            process.stdout.write(
                `  /api/${route}  ${target.name} ${target.image}, ` +
                `target ${target.target}\n`
            );
        }
        // Said out loud, every time. Answering a name other than loopback is
        // the operator widening a deliberate refusal, and it should never be
        // something they have to remember they did.
        if (allowedHosts.length > 0) {
            process.stdout.write(
                `  also answering requests addressed to: ` +
                `${allowedHosts.join(", ")}\n`
            );
        }
    });
}
