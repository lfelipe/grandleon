#!/usr/bin/env node
// SPDX-License-Identifier: MIT

// Ask the build service for a console image, from a shell instead of from a
// browser.
//
//   node tools/rom_service/build-rom.mjs [--console n64|playstation] \
//       <project.json> <output-directory>
//
// This is not a second path to an image. It starts the service in this process
// and then does exactly what `editor/src/platform/rom-service.ts` does: POST,
// poll, collect over HTTP. So a check that runs this is checking the surface
// the editor uses, not a convenient shortcut past it. That distinction is the
// whole value of the lanes it exists for: a lane that called the build script
// directly would prove the toolchain works and say nothing about the path an
// author's project actually takes.
//
// The output is a directory rather than a file name, and every file keeps the
// name the service gave it. A disc is two files whose cue sheet names its own
// bin, so a caller that renamed the pair would hand somebody a table of
// contents pointing at a file that is not there.

import { mkdir, writeFile } from "node:fs/promises";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { BuildQueue, consoles, createRomService } from "./serve.mjs";

const repositoryRoot = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "..",
    ".."
);

const argv = process.argv.slice(2);
let route = "n64";
const consoleIndex = argv.indexOf("--console");
if (consoleIndex >= 0) {
    route = argv[consoleIndex + 1] ?? "";
    argv.splice(consoleIndex, 2);
}
const [projectPath, outputDir] = argv;
if (projectPath === undefined || outputDir === undefined ||
    consoles[route] === undefined) {
    process.stderr.write(
        "usage: build-rom.mjs [--console " +
        `${Object.keys(consoles).join("|")}] <project.json> ` +
        "<output-directory>\n"
    );
    process.exit(2);
}

const queue = new BuildQueue({ root: repositoryRoot, target: consoles[route] });
const server = createRomService({
    root: repositoryRoot,
    queues: { [route]: queue }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;

const started = Date.now();
const elapsed = () => `${((Date.now() - started) / 1000).toFixed(0)}s`;

function fail(message, detail = "") {
    process.stderr.write(`RESULT FAIL ${message}\n`);
    if (detail) process.stderr.write(`${detail}\n`);
    server.close();
    process.exit(1);
}

const text = await readFile(projectPath, "utf8");
const accepted = await fetch(`${base}/api/${route}/build`, {
    method: "POST",
    body: text
});
const body = await accepted.json();
if (accepted.status !== 202) {
    fail(`refused ${body.code}: ${body.message}`, body.detail);
}
process.stdout.write(
    `building ${path.basename(projectPath)} for ${consoles[route].name} ` +
    `(${body.title || "untitled"}, campaign ${body.campaign})\n`
);

let status = body;
while (status.state === "queued" || status.state === "building") {
    await new Promise((resolve) => setTimeout(resolve, 2000));
    status = await (
        await fetch(`${base}/api/${route}/build/${body.id}`)
    ).json();
    process.stdout.write(`  ${elapsed()} ${status.state}\n`);
}
if (status.state !== "done") {
    fail(
        `${status.error?.code ?? "unknown"}: ${status.error?.message ?? ""}`,
        status.error?.detail ?? status.log
    );
}

await mkdir(outputDir, { recursive: true });
for (const artifact of status.artifacts) {
    const collected = Buffer.from(
        await (await fetch(
            `${base}/api/${route}/build/${body.id}/artifact/${
                encodeURIComponent(artifact.name)
            }`
        )).arrayBuffer()
    );
    const target = path.join(outputDir, artifact.name);
    await writeFile(target, collected);
    process.stdout.write(
        `FILE ${target} ${collected.length} bytes md5 ${artifact.md5}\n`
    );
}
process.stdout.write(
    `RESULT PASS ${status.artifacts.length} file(s) in ${outputDir} ` +
    `in ${elapsed()}\n`
);

// And drop the build tree, which nothing is coming back for: every file is
// written out above. A service left running keeps a finished job for half an
// hour so a browser can collect it; this process is about to exit, and a
// PlayStation request stages a host build and a cross build — a quarter of a
// gigabyte — under its own directory. A lane that ran this three times and
// left three of them behind is how a machine fills up.
const job = queue.get(body.id);
if (job !== null) await queue.forget(job);
server.close();
