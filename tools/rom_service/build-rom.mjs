#!/usr/bin/env node
// SPDX-License-Identifier: MIT

// Ask the ROM service for a ROM, from a shell instead of from a browser.
//
//   node tools/rom_service/build-rom.mjs <project.json> <output.z64>
//
// This is not a second path to a ROM. It starts the service in this process
// and then does exactly what `editor/src/platform/rom-service.ts` does: POST,
// poll, collect over HTTP. So a check that runs this is checking the surface
// the editor uses, not a convenient shortcut past it. That distinction is the
// whole value of the two lanes it exists for: a lane that called the build
// script directly would prove the toolchain works and say nothing about the
// path an author's project actually takes.

import { writeFile } from "node:fs/promises";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { BuildQueue, createRomService } from "./serve.mjs";

const repositoryRoot = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "..",
    ".."
);

const [projectPath, outputPath] = process.argv.slice(2);
if (projectPath === undefined || outputPath === undefined) {
    process.stderr.write(
        "usage: build-rom.mjs <project.json> <output.z64>\n"
    );
    process.exit(2);
}

const queue = new BuildQueue({ root: repositoryRoot });
const server = createRomService({ root: repositoryRoot, queue });
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
const accepted = await fetch(`${base}/api/n64/build`, {
    method: "POST",
    body: text
});
const body = await accepted.json();
if (accepted.status !== 202) {
    fail(`refused ${body.code}: ${body.message}`, body.detail);
}
process.stdout.write(
    `building ${path.basename(projectPath)} ` +
    `(${body.title || "untitled"}, campaign ${body.campaign})\n`
);

let status = body;
while (status.state === "queued" || status.state === "building") {
    await new Promise((resolve) => setTimeout(resolve, 2000));
    status = await (await fetch(`${base}/api/n64/build/${body.id}`)).json();
    process.stdout.write(`  ${elapsed()} ${status.state}\n`);
}
if (status.state !== "done") {
    fail(
        `${status.error?.code ?? "unknown"}: ${status.error?.message ?? ""}`,
        status.error?.detail ?? status.log
    );
}

const rom = Buffer.from(
    await (await fetch(`${base}/api/n64/build/${body.id}/rom`)).arrayBuffer()
);
await writeFile(outputPath, rom);
process.stdout.write(
    `RESULT PASS ${outputPath} ${rom.length} bytes md5 ${status.md5} in ${elapsed()}\n`
);
server.close();
