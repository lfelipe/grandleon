// SPDX-License-Identifier: MIT

// The development server, in a real browser.
//
// `npm run dev` is the first command a newcomer types and every other browser
// check in this repository runs against `vite preview` over a production
// build. The two servers differ in the ways that decide whether the page is
// usable: dev injects every stylesheet as an inline `<style>` element, serves
// unbundled modules, and constructs the analysis worker from an origin it
// derives itself. A production build can be perfect while all three are
// broken.
//
// So this asks the three questions no other check can:
//
//   * is the page styled, measured as a computed colour rather than as the
//     presence of a stylesheet, because a blocked `<style>` element is still
//     in the document;
//   * did the browser refuse anything on grounds of the Content Security
//     Policy;
//   * did the analysis worker construct, which is what fails when the server's
//     advertised origin and its listening port disagree.
//
// It starts the server on a port of its own with `--port`, so the same run
// also covers the flag a person reaches for first.

import assert from "node:assert/strict";
import path from "node:path";
import process from "node:process";
import { spawn } from "node:child_process";
import { chromium } from "@playwright/test";

const editorDirectory = path.resolve(import.meta.dirname, "..");
process.env.PLAYWRIGHT_BROWSERS_PATH ??= path.resolve(
  editorDirectory,
  "../.playwright-browsers"
);

const port = Number(process.env.GRANDLEON_EDITOR_DEV_PORT ?? "4188");
assert.ok(
  Number.isInteger(port) && port > 0 && port < 65536,
  `GRANDLEON_EDITOR_DEV_PORT must be a port number, not '${
    process.env.GRANDLEON_EDITOR_DEV_PORT
  }'`
);
const origin = `http://127.0.0.1:${port}`;

// Refuse to measure a server this script did not start. `--strictPort` makes
// the spawn below fail when the port is taken, and without this the wait loop
// finds the stranger answering and reports on whatever it happens to serve.
try {
  const already = await fetch(`${origin}/`);
  if (already.ok) {
    throw new Error(
      `port ${port} is already serving something. This check starts its own ` +
      `development server and will not measure one it did not start; stop ` +
      `that process, or set GRANDLEON_EDITOR_DEV_PORT to a free port.`
    );
  }
} catch (error) {
  if (error instanceof Error && error.message.includes("already serving")) {
    throw error;
  }
  // Connection refused is what this wanted: nothing is there.
}

// Its own process group, so the kill below reaches the server rather than the
// `npx` wrapper in front of it. An orphaned listener holds the port, which the
// check above then reads as a stranger and refuses to run against.
//
// The advertised hostname is pinned to the loopback address the browser will
// use. Left at the machine's own name, this would be measuring whether mDNS
// resolves on the machine running the check, which is not the question and is
// not true everywhere.
const server = spawn(
  "npx", ["vite", "--port", String(port), "--strictPort"],
  {
    cwd: editorDirectory,
    stdio: ["ignore", "pipe", "pipe"],
    detached: true,
    env: { ...process.env, GRANDLEON_EDITOR_HOSTNAME: "127.0.0.1" }
  }
);
let banner = "";
server.stdout.setEncoding("utf8");
server.stderr.setEncoding("utf8");
server.stdout.on("data", (chunk) => { banner += chunk; });
server.stderr.on("data", (chunk) => { banner += chunk; });

const stopServer = () => {
  try {
    process.kill(-server.pid, "SIGTERM");
  } catch {
    // Already gone, which is the outcome this wanted.
  }
};
process.on("exit", stopServer);
process.on("SIGINT", () => { stopServer(); process.exit(130); });
process.on("SIGTERM", () => { stopServer(); process.exit(143); });

let ready = false;
for (let attempt = 0; attempt < 100; attempt += 1) {
  if (server.exitCode !== null) break;
  try {
    const response = await fetch(`${origin}/`);
    if (response.ok) { ready = true; break; }
  } catch {
    // Not listening yet.
  }
  await new Promise((resolve) => setTimeout(resolve, 200));
}
if (!ready) {
  throw new Error(
    `the development server never answered on port ${port}\n${banner}`
  );
}

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });

// Everything the browser said, kept whole. A Content Security Policy refusal
// is written to the console and nowhere else: no request fails, no exception
// is thrown, and the document goes on rendering without the thing it refused.
const consoleLines = [];
page.on("console", (message) => {
  consoleLines.push(`${message.type()}: ${message.text()}`);
});
page.on("pageerror", (error) => {
  consoleLines.push(`pageerror: ${error.message}`);
});

// The analysis worker is constructed when the application mounts, so the wait
// has to be armed before the navigation that mounts it.
const workerAppeared = page.waitForEvent("worker", { timeout: 30_000 })
  .then(() => true, () => false);

let failure = null;
try {
  await page.goto(`${origin}/`, { waitUntil: "load" });
  await page.getByRole("heading", { name: "Grandleon Editor" })
    .waitFor({ state: "visible", timeout: 30_000 });

  // Styled, measured on the one element whose colour is unmistakable. An
  // unstyled page leaves the header transparent, so this is the difference
  // between the editor and a stack of default HTML controls.
  const headerBackground = await page.locator("header.app-header").evaluate(
    (element) => getComputedStyle(element).backgroundColor
  );
  assert.equal(
    headerBackground,
    "rgb(23, 32, 51)",
    "the development server serves an unstyled page: the header background " +
    `computed to '${headerBackground}' rather than the stylesheet's colour`
  );

  // A second, independent reading of the same fact, on a rule the browser has
  // a non-zero default for.
  const bodyMargin = await page.evaluate(
    () => getComputedStyle(document.body).marginTop
  );
  assert.equal(
    bodyMargin,
    "0px",
    `the development server serves an unstyled page: body margin is '${bodyMargin}'`
  );

  const refusals = consoleLines.filter(
    (line) => /content security policy/i.test(line)
  );
  assert.deepEqual(
    refusals,
    [],
    `the development server's page violates its Content Security Policy:\n` +
    refusals.join("\n")
  );

  // The worker's origin comes from the server's configuration rather than from
  // the address the browser used, so a server listening somewhere other than
  // where it says it is fails here and nowhere else.
  assert.ok(
    await workerAppeared,
    "the analysis worker did not construct against the development server"
  );

  // What the server printed has to be the address that works. A banner naming
  // a port nothing is listening on sends every newcomer to a dead page.
  assert.match(
    banner,
    new RegExp(`Editor:\\s+http://\\S+:${port}/`),
    `the development server's banner does not name port ${port}:\n${banner}`
  );

  const errors = consoleLines.filter((line) => line.startsWith("error:"));
  assert.deepEqual(
    errors,
    [],
    `the development server's page logged errors:\n${errors.join("\n")}`
  );
} catch (error) {
  failure = error;
} finally {
  await browser.close();
  stopServer();
}

if (failure) throw failure;

process.stdout.write(`development server styled and error-free on ${origin}\n`);
