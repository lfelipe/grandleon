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

export default defineConfig({
  testDir: "tests/browser",
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
