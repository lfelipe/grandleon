// SPDX-License-Identifier: MIT
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { defineConfig, devices } from "@playwright/test";

const editorDirectory = path.dirname(fileURLToPath(import.meta.url));

// Browsers live inside the repository rather than in a user-level cache, so
// that verifying the editor in a real browser depends on nothing outside the
// checkout. See CODING.md for the one-time install command.
process.env.PLAYWRIGHT_BROWSERS_PATH ??= path.resolve(
  editorDirectory,
  "../.playwright-browsers"
);

// A test-dedicated port, NOT 4173: that is where a person's long-lived
// preview tends to live, and reusing a server this run did not start means
// testing whatever that server happens to serve. Parallel workstreams set
// GRANDLEON_EDITOR_PREVIEW_PORT to distinct values.
const port = Number(process.env.GRANDLEON_EDITOR_PREVIEW_PORT ?? "4517");
const origin = `http://127.0.0.1:${port}`;

// Where the editor's ROM proxy is pointed for this suite, and the same argument
// one more time. `vite.config.ts` defaults it to 4699, which is where a
// person's ROM build service lives — and it is often up, because running it is
// how the editor is used. A suite that reads 4699 is asking whatever the
// machine happens to have running, so "offers the Nintendo 64 ROM, and says why
// it cannot build one here" goes red on exactly the machines where a ROM *can*
// be built here. That is a failure of this suite and not of the editor.
//
// Pointing it somewhere nothing answers is only half an answer, though: a port
// nobody expects to be busy is still a port somebody can be on. `global-setup`
// beside this proves it is free before any test runs, the way `--strictPort`
// above refuses to reuse a preview server this run did not start.
const romServicePort = Number(process.env.GRANDLEON_ROM_SERVICE_PORT ?? "4518");
process.env.GRANDLEON_ROM_SERVICE_PORT = String(romServicePort);

export default defineConfig({
  testDir: "tests/browser",
  globalSetup: path.resolve(editorDirectory, "tests/browser/global-setup.ts"),
  fullyParallel: true,
  // Unconditional, not `Boolean(process.env.CI)`. A committed `test.only` is
  // never right, and gated on CI alone it is caught by nothing a person runs:
  // one `.only` cuts this suite from every test to one and exits 0.
  forbidOnly: true,
  retries: 0,
  // Omitted rather than set to undefined outside CI, because
  // `exactOptionalPropertyTypes` distinguishes the two and only the omission
  // means "Playwright's own default".
  ...(process.env.CI ? { workers: 1 } : {}),
  reporter: process.env.CI ? "line" : "list",
  use: {
    baseURL: origin,
    trace: "retain-on-failure"
  },
  projects: [
    // Chromium specifically: it is the engine that enforces a Content Security
    // Policy against WebAssembly compilation, which is the failure mode that
    // the happy-dom suites structurally cannot observe.
    { name: "chromium", use: { ...devices["Desktop Chrome"] } }
  ],
  webServer: {
    // Serves the production build, so the browser sees the same CSP, the same
    // bundling, and the same embedded WebAssembly module that a deployment does.
    command: `npx vite preview --port ${port} --strictPort`,
    url: origin,
    // Never reuse: the server must serve the build this run just produced.
    reuseExistingServer: false,
    timeout: 60_000
  }
});
