// SPDX-License-Identifier: MIT
// The ROM service's refusals and its failure paths.
//
// None of this starts a container. That is deliberate and it is the point: the
// service's job is to have said no to everything it can say no to *before* a
// container is involved, so a test that needed one would be testing the wrong
// half. The half that needs a container is a console lane, not a unit test.
//
//   node --test tools/rom_service/

import assert from "node:assert/strict";
import { chmod, mkdir, mkdtemp, readFile, readdir, rm, writeFile }
    from "node:fs/promises";
import { existsSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { test } from "node:test";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
    toolchainEnvironment,
    BuildQueue,
    RefusalError,
    createRomService,
    health,
    locateCompiler,
    projectSizeBudget,
    refuseForeignRequest,
    refuseProject,
    configuredHosts,
    setAllowedHosts,
    refuseUncompilable,
    servedCharacterStyles
} from "./serve.mjs";

const repositoryRoot = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "..",
    ".."
);

const shippedProject = path.join(
    repositoryRoot, "games", "tarnholt", "source", "project.json"
);
const otherProject = path.join(
    repositoryRoot, "games", "demo", "source", "project.json"
);

const styles = await servedCharacterStyles(repositoryRoot);

function refusalOf(text) {
    try {
        refuseProject(text, styles);
    } catch (error) {
        assert.ok(error instanceof RefusalError, `not a refusal: ${error}`);
        return error;
    }
    return null;
}

// -- the refusals ------------------------------------------------------------

test("the shipped project is served", async () => {
    const project = refuseProject(await readFile(shippedProject, "utf8"), styles);
    assert.equal(project.gameId, "grandleon.tarnholt");
    assert.equal(project.campaigns[0].id, "tarnholt_line");
});

test("a second, different project is also served", async () => {
    const project = refuseProject(await readFile(otherProject, "utf8"), styles);
    assert.equal(project.gameId, "grandleon.demo");
    // The check below the whole change rests on: the two games are genuinely
    // different, so a ROM built for this one cannot pass for the other.
    assert.notEqual(project.campaigns[0].id, "tarnholt_line");
});

test("something that is not a project is refused by name", () => {
    assert.equal(refusalOf("not json at all").code, "project_unreadable");
    assert.equal(refusalOf("[1, 2, 3]").code, "project_unreadable");
    assert.equal(refusalOf("\"a string\"").code, "project_unreadable");
    assert.equal(refusalOf("null").code, "project_unreadable");
});

test("a project with no campaign is refused by name", () => {
    assert.equal(
        refusalOf(JSON.stringify({ gameId: "g", campaigns: [] })).code,
        "project_without_campaign"
    );
    assert.equal(
        refusalOf(JSON.stringify({ gameId: "g" })).code,
        "project_without_campaign"
    );
    // A campaign the ROM could not name is the same refusal: the console has
    // to ask for one by id, and there is nothing to ask for.
    assert.equal(
        refusalOf(JSON.stringify({ gameId: "g", campaigns: [{}] })).code,
        "project_without_campaign"
    );
});

test("a style the art library does not hold is refused by name", () => {
    const refusal = refusalOf(JSON.stringify({
        gameId: "g",
        campaigns: [{ id: "c" }],
        characterStyleId: "steampunk"
    }));
    assert.equal(refusal.code, "character_style_not_served");
    // The author is told what they could have asked for.
    for (const style of styles.menu) assert.match(refusal.detail, new RegExp(style));
});

test("naming no style at all is not a refusal", () => {
    assert.equal(
        refusalOf(JSON.stringify({ gameId: "g", campaigns: [{ id: "c" }] })),
        null
    );
});

test("every style the library does hold is served", () => {
    for (const style of styles.menu) {
        assert.equal(
            refusalOf(JSON.stringify({
                gameId: "g",
                campaigns: [{ id: "c" }],
                characterStyleId: style
            })),
            null,
            `the library holds ${style} but the service refused it`
        );
    }
});

test("a project past the console's budget is refused by name", () => {
    const padded = {
        gameId: "g",
        campaigns: [{ id: "c" }],
        notes: "x".repeat(projectSizeBudget)
    };
    const refusal = refusalOf(JSON.stringify(padded));
    assert.equal(refusal.code, "project_too_large_for_the_console");
    assert.match(refusal.detail, /bytes/);
});

// A project drawn in more than one character style is served rather than
// refused: the Nintendo 64 build embeds the drawings a project's content
// actually draws, so a mix of styles is nothing for it to object to. The guard
// below is on the schema, because the test means nothing if a character cannot
// name a style at all.
test("a project drawn by two hands is served, not refused", async () => {
    const schema = JSON.parse(await readFile(
        path.join(repositoryRoot, "schemas", "source", "v1", "unit-type.schema.json"),
        "utf8"
    ));
    assert.ok(
        schema.properties?.characterStyleId,
        "a character can no longer name its own style; this test is about a " +
        "capability that has been withdrawn"
    );
    assert.equal(
        refusalOf(JSON.stringify({
            gameId: "g",
            campaigns: [{ id: "c" }],
            characterStyleId: "medieval",
            unitTypes: [
                { id: "house_knight", characterStyleId: "medieval" },
                { id: "house_mage", characterFigureId: "second" },
                { id: "wood_archer", characterStyleId: "nature" },
                { id: "wild_archer", characterStyleId: "nature" }
            ]
        })),
        null
    );
});

test("a character naming art the library does not hold is refused by name", () => {
    const badStyle = refusalOf(JSON.stringify({
        gameId: "g",
        campaigns: [{ id: "c" }],
        unitTypes: [{ id: "a" }, { id: "b", characterStyleId: "steampunk" }]
    }));
    assert.equal(badStyle.code, "character_style_not_served");
    // Which character, so an author can find it.
    assert.match(badStyle.detail, /unitTypes\[1\]\.characterStyleId/);
    assert.match(badStyle.detail, /steampunk/);

    const badFigure = refusalOf(JSON.stringify({
        gameId: "g",
        campaigns: [{ id: "c" }],
        unitTypes: [{ id: "a", characterFigureId: "third" }]
    }));
    assert.equal(badFigure.code, "character_style_not_served");
    assert.match(badFigure.detail, /unitTypes\[0\]\.characterFigureId/);
    assert.match(badFigure.detail, /third/);
});

test("every figure the library does draw is served", () => {
    for (const figure of styles.figures) {
        assert.equal(
            refusalOf(JSON.stringify({
                gameId: "g",
                campaigns: [{ id: "c" }],
                characterFigureId: figure
            })),
            null,
            `the library draws ${figure} but the service refused it`
        );
    }
});

// -- the compiler refusal ----------------------------------------------------
//
// Driven by a stand-in compiler rather than by the host build, and that is the
// point: this is the last refusal a project passes through before a container
// is started, and a test that ran only where `grandleon_content_compile`
// happens to have been built is a test that reports "skipped" on exactly the
// machines where nobody notices. What is under test here is the service's
// half: that a non-zero exit becomes a named refusal carrying the tool's own
// words, and that a zero exit is silence. The real compiler's judgement is
// checked below, unconditionally.

// A compiler-shaped executable that says what it is told to say.
async function standInCompiler(workDir, exitCode, message) {
    const script = path.join(workDir, "stand-in-compiler");
    await writeFile(
        script,
        `#!/bin/sh\nprintf '%s\\n' ${JSON.stringify(message)} >&2\n` +
        `exit ${exitCode}\n`
    );
    await chmod(script, 0o755);
    return script;
}

async function inWorkDir(body) {
    const workDir = await mkdtemp(path.join(tmpdir(), "rom-service-"));
    try {
        return await body(workDir);
    } finally {
        await rm(workDir, { recursive: true, force: true });
    }
}

test("a project the compiler rejects is refused in the compiler's words",
    async () => {
        await inWorkDir(async (workDir) => {
            const compiler = await standInCompiler(
                workDir, 1, "$.campaigns[0]: 'nowhere' is not an encounter"
            );
            const broken = path.join(workDir, "project.json");
            await writeFile(broken, "{}");
            await assert.rejects(
                () => refuseUncompilable(broken, compiler, workDir),
                (error) => {
                    assert.ok(error instanceof RefusalError);
                    assert.equal(error.code, "project_does_not_compile");
                    // The compiler's own diagnostics, verbatim, not a message
                    // this service composed.
                    assert.equal(
                        error.detail,
                        "$.campaigns[0]: 'nowhere' is not an encounter"
                    );
                    return true;
                }
            );
        });
    });

test("a project the compiler accepts is not refused", async () => {
    await inWorkDir(async (workDir) => {
        const compiler = await standInCompiler(workDir, 0, "");
        const fine = path.join(workDir, "project.json");
        await writeFile(fine, "{}");
        await refuseUncompilable(fine, compiler, workDir);
    });
});

test("with no compiler at all there is nothing to refuse", async () => {
    await inWorkDir(async (workDir) => {
        await refuseUncompilable(
            path.join(workDir, "absent.json"), null, workDir
        );
    });
});

// And the real thing. This one cannot quietly not run: a checkout with a host
// build must produce a compiler, and the assertion below is what says so. The
// only way past it is a checkout that has never been built, which is a fact
// about the machine rather than a test opting itself out.
test("the shipped project compiles under the real host compiler", async () => {
    const compiler = locateCompiler(repositoryRoot);
    const built =
        existsSync(path.join(repositoryRoot, "build")) ||
        existsSync(path.join(repositoryRoot, "build-clang"));
    if (compiler === null) {
        assert.equal(
            built,
            false,
            "this checkout has a host build tree but no " +
            "grandleon_content_compile in it"
        );
        return;
    }
    await inWorkDir(async (workDir) => {
        await refuseUncompilable(shippedProject, compiler, workDir);
    });
});

// -- the campaign id the build writes into the ROM's own code ----------------

test("a campaign id that is not an identifier is refused by name", () => {
    // The shape that matters: this id closes the C++ string literal the
    // Nintendo 64 build writes it into, and the rest of it is code.
    const hostile =
        'x";\n#include "/src/games/tarnholt/source/project.json"\n' +
        'const char* ignored = "';
    const refusal = refusalOf(JSON.stringify({
        gameId: "grandleon.x",
        campaigns: [{ id: hostile }]
    }));
    assert.equal(refusal.code, "campaign_id_not_an_identifier");
    assert.match(refusal.detail, /campaigns\[0\]\.id/);

    // And the quieter shapes, each of which is still not a name.
    for (const id of ['a"b', "a\nb", "A", "1a", "a b", "a..b", "a-", "../x"]) {
        assert.equal(
            refusalOf(JSON.stringify({
                gameId: "g", campaigns: [{ id }]
            }))?.code,
            "campaign_id_not_an_identifier",
            `'${id}' was served`
        );
    }
});

test("the identifiers a project may actually carry are served", () => {
    for (const id of [
        "tarnholt_line", "a", "a1", "the.first-campaign_2", "x.y.z"
    ]) {
        assert.equal(
            refusalOf(JSON.stringify({ gameId: "g", campaigns: [{ id }] })),
            null,
            `'${id}' is a source identifier and was refused`
        );
    }
});

// -- the failure paths -------------------------------------------------------

const validProject = { gameId: "grandleon.x", campaigns: [{ id: "c" }] };
const validText = JSON.stringify(validProject);

test("one build runs at a time and a second is told it is queued", async () => {
    let release;
    const started = [];
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: async (job) => {
            started.push(job.id);
            await new Promise((resolve) => { release = resolve; });
            return { romPath: "/dev/null", bytes: 1, md5: "x" };
        }
    });

    const first = queue.enqueue(validText, validProject);
    const second = queue.enqueue(validText, validProject);
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(queue.describe(first).state, "building");
    assert.equal(queue.describe(second).state, "queued");
    assert.equal(queue.describe(second).position, 1);
    // The strong version of "one at a time": the second build's function was
    // never entered while the first was in it.
    assert.deepEqual(started, [first.id]);

    release();
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(queue.describe(first).state, "done");
    assert.deepEqual(started, [first.id, second.id]);
});

test("a queue already full refuses by name rather than growing", async () => {
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: () => new Promise(() => {})
    });
    queue.enqueue(validText, validProject);
    queue.enqueue(validText, validProject);
    assert.throws(
        () => queue.enqueue(validText, validProject),
        (error) => {
            assert.equal(error.code, "rom_build_queue_full");
            return true;
        }
    );
});

test("a build that fails carries the toolchain's own output", async () => {
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: async () => {
            throw new RefusalError(
                "rom_build_failed",
                "The Nintendo 64 build failed.",
                "mips64-elf-gcc: error: play_rom.cpp: no such file"
            );
        }
    });
    const job = queue.enqueue(validText, validProject);
    await new Promise((resolve) => setTimeout(resolve, 10));
    const state = queue.describe(job);
    assert.equal(state.state, "failed");
    assert.equal(state.error.code, "rom_build_failed");
    assert.match(state.error.detail, /mips64-elf-gcc/);
});

test("a missing container runtime is told apart from a broken build", async () => {
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: async () => {
            throw new RefusalError(
                "container_runtime_missing",
                "no runtime",
                "error: 'docker' is not on PATH."
            );
        }
    });
    const job = queue.enqueue(validText, validProject);
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(queue.describe(job).error.code, "container_runtime_missing");
});

test("an unexpected throw still ends the job rather than wedging the queue",
    async () => {
        const queue = new BuildQueue({
            root: repositoryRoot,
            runBuild: async () => { throw new Error("something unforeseen"); }
        });
        const first = queue.enqueue(validText, validProject);
        await new Promise((resolve) => setTimeout(resolve, 10));
        assert.equal(queue.describe(first).state, "failed");
        assert.equal(queue.describe(first).error.code, "rom_build_failed");
        // And the next build still starts, which is the property that matters:
        // one bad request must not stop the service.
        const second = queue.enqueue(validText, validProject);
        await new Promise((resolve) => setTimeout(resolve, 10));
        assert.equal(queue.describe(second).state, "failed");
    });

test("a build that never finishes is given up on rather than wedging the queue",
    async () => {
        let asked = 0;
        const queue = new BuildQueue({
            root: repositoryRoot,
            buildTimeoutMilliseconds: 20,
            runBuild: () => { asked += 1; return new Promise(() => {}); }
        });
        const wedged = queue.enqueue(validText, validProject);
        await new Promise((resolve) => setTimeout(resolve, 60));

        const state = queue.describe(wedged);
        assert.equal(state.state, "failed");
        assert.equal(state.error.code, "rom_build_timed_out");
        assert.match(state.error.detail, /20 ms/);

        // The property the timeout exists for: the one running slot is free, so
        // the next author is building rather than being told the queue is full.
        const next = queue.enqueue(validText, validProject);
        await new Promise((resolve) => setImmediate(resolve));
        assert.equal(queue.describe(next).state, "building");
        assert.equal(asked, 2);

        // And it is finished, so the reaper will eventually take its tree; a
        // build still marked unfinished is exempt forever.
        assert.notEqual(queue.get(wedged.id).finishedAt, 0);
    });

test("build trees no job owns are swept, and live ones are left alone",
    async () => {
        const root = await mkdtemp(path.join(tmpdir(), "rom-service-root-"));
        try {
            const requests = path.join(root, "build-n64", "requests");
            const orphan = path.join(requests, "left-behind-by-a-dead-service");
            await mkdir(orphan, { recursive: true });
            await writeFile(path.join(orphan, "project.json"), "{}");

            const queue = new BuildQueue({
                root,
                runBuild: () => new Promise(() => {})
            });
            const running = queue.enqueue(validText, validProject);
            const live = path.join(requests, running.id);
            await mkdir(live, { recursive: true });

            await queue.reap();
            assert.equal(
                existsSync(orphan), false, "an orphaned build tree was kept"
            );
            assert.equal(
                existsSync(live), true, "a running build's tree was swept"
            );
            assert.deepEqual(await readdir(requests), [running.id]);
        } finally {
            await rm(root, { recursive: true, force: true });
        }
    });

// Its own root, because reaping deletes build trees and a test has no business
// deciding that about the checkout it is running in.
test("finished jobs are reaped and unfinished ones are not", async () => {
    const root = await mkdtemp(path.join(tmpdir(), "rom-service-root-"));
    try {
        let clock = 1_000_000;
        const queue = new BuildQueue({
            root,
            now: () => clock,
            runBuild: async () => ({ romPath: "/dev/null", bytes: 1, md5: "x" })
        });
        const job = queue.enqueue(validText, validProject);
        await new Promise((resolve) => setTimeout(resolve, 10));
        assert.equal(queue.get(job.id).state, "done");

        await queue.reap(1000);
        assert.notEqual(
            queue.get(job.id), null, "reaped a job still in its window"
        );

        clock += 10_000;
        await queue.reap(1000);
        assert.equal(queue.get(job.id), null, "a stale job was not reaped");
    } finally {
        await rm(root, { recursive: true, force: true });
    }
});

// -- the HTTP surface --------------------------------------------------------

async function withService(queue, body) {
    const server = createRomService({ root: repositoryRoot, queue });
    await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
        return await body(base);
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
}

test("a refused project answers 400 with the code, before any build", async () => {
    let builds = 0;
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: async () => { builds += 1; return {}; }
    });
    await withService(queue, async (base) => {
        const response = await fetch(`${base}/api/n64/build`, {
            method: "POST",
            body: JSON.stringify({ gameId: "g", campaigns: [] })
        });
        assert.equal(response.status, 400);
        const refusal = await response.json();
        assert.equal(refusal.code, "project_without_campaign");
        assert.match(refusal.message, /campaign/i);
    });
    assert.equal(builds, 0, "a refused project reached the build");
});

test("an accepted project answers 202 and can then be polled", async () => {
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: async () => ({ romPath: "/dev/null", bytes: 42, md5: "abc" })
    });
    await withService(queue, async (base) => {
        const accepted = await fetch(`${base}/api/n64/build`, {
            method: "POST",
            body: validText
        });
        assert.equal(accepted.status, 202);
        const { id } = await accepted.json();
        await new Promise((resolve) => setTimeout(resolve, 20));
        const polled = await (await fetch(`${base}/api/n64/build/${id}`)).json();
        assert.equal(polled.state, "done");
        assert.equal(polled.bytes, 42);
    });
});

test("a build nobody started is not one this service will pretend to hold",
    async () => {
        const queue = new BuildQueue({ root: repositoryRoot });
        await withService(queue, async (base) => {
            const response = await fetch(
                `${base}/api/n64/build/00000000-0000-4000-8000-000000000000`
            );
            assert.equal(response.status, 404);
            assert.equal((await response.json()).code, "rom_build_unknown");
        });
    });

test("an unfinished build refuses to hand over a ROM", async () => {
    const queue = new BuildQueue({
        root: repositoryRoot,
        runBuild: () => new Promise(() => {})
    });
    await withService(queue, async (base) => {
        const { id } = await (await fetch(`${base}/api/n64/build`, {
            method: "POST",
            body: validText
        })).json();
        const response = await fetch(`${base}/api/n64/build/${id}/rom`);
        assert.equal(response.status, 409);
        assert.equal((await response.json()).code, "rom_build_unfinished");
    });
});

// -- who is allowed to ask ---------------------------------------------------
//
// `POST /api/n64/build` carries nothing a browser needs permission to send, so
// without these checks it is a CORS simple request: no preflight, no consent,
// and a two-minute container build for any page an author happens to have open.
// Binding to the loopback interface is not a defence: loopback is where the
// request comes from.

function refusalOfRequest(headers) {
    try {
        refuseForeignRequest({ headers });
    } catch (error) {
        assert.ok(error instanceof RefusalError, `not a refusal: ${error}`);
        return error;
    }
    return null;
}

test("a name the service was not started with is refused", () => {
    setAllowedHosts([]);
    const refusal = refusalOfRequest({ host: "cruncher:5173" });
    assert.ok(refusal, "a request addressed to a bare hostname was accepted");
    assert.equal(refusal.code, "request_not_addressed_locally");
});

test("a name the service was started with is answered", () => {
    setAllowedHosts(["cruncher"]);
    try {
        // The editor may serve on any port; only the name was allowed.
        assert.equal(refusalOfRequest({ host: "cruncher:5173" }), null);
        assert.equal(refusalOfRequest({ host: "cruncher:4173" }), null);
        // And loopback still works, which is the case nobody configures.
        assert.equal(refusalOfRequest({ host: "localhost:5173" }), null);
    } finally {
        setAllowedHosts([]);
    }
});

test("allowing one name does not allow another", () => {
    setAllowedHosts(["cruncher"]);
    try {
        // The whole point of an allow-list over a switch: a rebinding page
        // resolves its own name to 127.0.0.1 and is still refused, because
        // what is compared is the name and not where it pointed.
        const refusal = refusalOfRequest({ host: "attacker.example:5173" });
        assert.ok(refusal, "an unnamed host was accepted");
        assert.equal(refusal.code, "request_not_addressed_locally");
    } finally {
        setAllowedHosts([]);
    }
});

test("a named host is still refused when another site drove the request", () => {
    setAllowedHosts(["cruncher"]);
    try {
        // Widening `Host` must not widen anything else: `Sec-Fetch-Site` is
        // the check a page cannot forge, and it still decides.
        const refusal = refusalOfRequest({
            host: "cruncher:5173",
            "sec-fetch-site": "cross-site"
        });
        assert.ok(refusal, "a cross-site request to a named host was accepted");
        assert.equal(refusal.code, "request_from_another_site");
    } finally {
        setAllowedHosts([]);
    }
});

test("hosts are read from the command line and the environment", () => {
    assert.deepEqual(
        configuredHosts({}, ["--allow-host", "cruncher"]), ["cruncher"]
    );
    assert.deepEqual(
        configuredHosts({}, ["--allow-host=Cruncher.Local"]), ["cruncher.local"]
    );
    assert.deepEqual(
        configuredHosts({ GRANDLEON_ROM_SERVICE_ALLOWED_HOSTS: "a, b ," }, []),
        ["a", "b"]
    );
    assert.deepEqual(configuredHosts({}, []), []);
});

test("a request another site drove is refused by name, before any build",
    async () => {
        let builds = 0;
        const queue = new BuildQueue({
            root: repositoryRoot,
            runBuild: async () => { builds += 1; return {}; }
        });
        await withService(queue, async (base) => {
            const response = await fetch(`${base}/api/n64/build`, {
                method: "POST",
                headers: {
                    origin: "https://evil.example",
                    "content-type": "text/plain"
                },
                body: validText
            });
            assert.equal(response.status, 403);
            const refusal = await response.json();
            assert.equal(refusal.code, "request_from_another_site");
            assert.match(refusal.detail, /evil\.example/);
        });
        assert.equal(builds, 0, "a page on another site launched a build");
    });

test("a fetch a browser marked cross-site is refused by name", () => {
    for (const site of ["cross-site", "same-site", "nonsense"]) {
        const refusal = refusalOfRequest({
            host: "127.0.0.1:4699", "sec-fetch-site": site
        });
        assert.equal(refusal.code, "request_from_another_site");
        assert.match(refusal.detail, new RegExp(site));
    }
});

test("a request addressed to a name that is not this machine is refused", () => {
    // The rebinding shape: a name the attacker controls, resolved to 127.0.0.1,
    // so that the page and the service share an origin and every answer, the
    // build log and the ROM, is readable.
    const rebound = refusalOfRequest({
        host: "evil.example:4699", origin: "http://evil.example:4699"
    });
    assert.equal(rebound.code, "request_not_addressed_locally");
    assert.match(rebound.detail, /evil\.example/);

    // And the LAN shape: the editor's dev server binds every interface and
    // proxies `/api/n64` here with the browser's own Host, so a peer on the
    // network would otherwise read the same answers.
    assert.equal(
        refusalOfRequest({ host: "workshop.local:5173" }).code,
        "request_not_addressed_locally"
    );
    assert.equal(
        refusalOfRequest({ host: "192.168.1.20:5173" }).code,
        "request_not_addressed_locally"
    );
    assert.equal(refusalOfRequest({}).code, "request_not_addressed_locally");
});

test("the editor on this machine, proxied or direct, is answered", () => {
    for (const host of [
        "localhost:5173", "127.0.0.1:4699", "127.0.0.1:4173", "[::1]:4699"
    ]) {
        assert.equal(
            refusalOfRequest({
                host,
                origin: `http://${host}`,
                "sec-fetch-site": "same-origin"
            }),
            null,
            `the editor at ${host} was refused`
        );
    }
    // A caller that states no origin at all (curl, a script, this test) is
    // not a page, and is answered on the same terms the Host draws.
    assert.equal(refusalOfRequest({ host: "localhost:4699" }), null);
    // A navigation rather than a fetch.
    assert.equal(
        refusalOfRequest({ host: "localhost:4699", "sec-fetch-site": "none" }),
        null
    );
});

test("health says whether this machine can build at all", async () => {
    const answer = await health(repositoryRoot, { docker: "definitely-not-docker" });
    assert.equal(answer.ready, false);
    assert.equal(answer.code, "container_runtime_missing");
    assert.match(answer.message, /container/i);
});

test("takes the toolchain pins from the file that owns them", () => {
    // `build-n64.sh` refuses without `GRANDLEON_LIBDRAGON_COMMIT`, by name.
    // A service reading only the environment would let somebody following the
    // README accept a job, spend two minutes on it, and then hand the author an
    // environment variable in a browser.
    const pinned = toolchainEnvironment({});
    assert.match(
        pinned.GRANDLEON_LIBDRAGON_COMMIT,
        /^[0-9a-f]{40}$/,
        "the libdragon commit must come from cmake/GrandleonNintendo64.cmake"
    );
    assert.match(pinned.GRANDLEON_LIBDRAGON_BASE_DIGEST, /^sha256:[0-9a-f]{64}$/);

    // And it is the same value cmake passes, rather than a second copy that
    // could drift from it.
    const cmake = readFileSync(
        path.join(repositoryRoot, "cmake", "GrandleonNintendo64.cmake"),
        "utf8"
    );
    assert.ok(
        cmake.includes(pinned.GRANDLEON_LIBDRAGON_COMMIT),
        "the pin this service uses is not in the file it claims to read"
    );

    // An override still wins, so a contributor can test a bump without editing
    // the pin everything else builds against.
    assert.equal(
        toolchainEnvironment({ GRANDLEON_LIBDRAGON_COMMIT: "abc" })
            .GRANDLEON_LIBDRAGON_COMMIT,
        "abc"
    );
});
