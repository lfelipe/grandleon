// SPDX-License-Identifier: MIT
// Captures the map editor over a production build, for the README.
//
// Driven rather than posed: it loads the bundled Tarnholt sample and opens the
// Fordlight in the map editor, the same clicks a person would make.
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const require = createRequire(path.join(root, "editor", "package.json"));
const { chromium } = require("@playwright/test");

const outPath = process.argv[2];
const port = 4181;

// Refuse to photograph a server this script did not start. `--strictPort`
// makes the spawn below fail when the port is taken, and without this check
// the failure is silent: the wait loop then finds the stranger answering,
// and the screenshot is of whatever that process happens to be serving. That
// is how a leftover preview from another day nearly ended up standing in for
// the current build.
try {
  const already = await fetch(`http://127.0.0.1:${port}/`);
  if (already.ok) {
    throw new Error(
      `port ${port} is already serving something. This script starts its own ` +
      `preview and will not photograph a server it did not start; stop that ` +
      `process and re-run.`
    );
  }
} catch (error) {
  if (error instanceof Error && error.message.includes("already serving")) {
    throw error;
  }
  // Connection refused is what we want: nothing is there.
}

// Its own process group, so the kill below reaches the server rather than a
// wrapper in front of it. `npx` runs the real `vite` as a *child* of itself, so
// killing what `spawn` returned left the listener orphaned and holding the
// port, which the check above then read as a stranger, and the script refused
// to run a second time. Signalling the group closes both halves of that.
const server = spawn(
  "npx", ["vite", "preview", "--port", String(port), "--strictPort"],
  { cwd: path.join(root, "editor"), stdio: "ignore", detached: true }
);
server.on("exit", (code) => {
  if (code) console.error(`preview server exited with code ${code}`);
});

// Whatever happens after this point, a thrown assertion, a failed launch or
// Ctrl-C, the server must not outlive the script.
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
try {
  // Wait for the preview server rather than sleeping.
  let ready = false;
  for (let attempt = 0; attempt < 50; attempt += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/`);
      if (response.ok) { ready = true; break; }
    } catch {}
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  if (!ready) throw new Error(`preview server never answered on port ${port}`);

  const browser = await chromium.launch();
  const page = await browser.newPage({
    viewport: { width: 1500, height: 1500 },
    deviceScaleFactor: 2
  });
  // Loading a sample over the unsaved initial draft asks for confirmation,
  // and Playwright dismisses dialogs unless told otherwise, which cancels
  // the load and leaves no records to photograph.
  page.on("dialog", (dialog) => void dialog.accept());
  await page.goto(`http://127.0.0.1:${port}/`);
  // The editor opens on the way in, and the bundled examples are offered there.
  await page.getByRole("button", { name: "Open this example" }).click();
  // Two surfaces answer to "Maps": the rail entry and the collection tab the
  // section reveals. The rail click also selects the collection.
  await page.locator(".project-sections button", { hasText: "Maps" }).click();
  await page.locator(".record-list button", { hasText: "Fordlight Crossing" })
    .click();
  await page.locator(".map-editor").evaluate(
    (element) => element.scrollIntoView({ block: "start" })
  );
  // Fix the cell size for the capture so every column fits the clip; on a
  // live screen the grid stretches to its container instead. The columns are
  // sized per row, since each row is its own ARIA row element.
  await page.locator(".terrain-grid").evaluate((element) => {
    for (const row of element.querySelectorAll(".terrain-row")) {
      const columns = row.style.gridTemplateColumns.match(/repeat\((\d+)/);
      if (columns) row.style.gridTemplateColumns = `repeat(${columns[1]}, 3rem)`;
      row.style.width = "max-content";
    }
    element.style.width = "max-content";
    // Let the ancestors grow with it; this styles the capture, not the app.
    for (let node = element.parentElement; node; node = node.parentElement) {
      node.style.overflow = "visible";
      node.style.maxWidth = "none";
    }
  });
  await page.waitForTimeout(400);
  // The map surface itself: the brush palette through the painted grid,
  // without the resize form below or unrelated panels beside it.
  const palette = await page.locator(".terrain-palette").boundingBox();
  const grid = await page.locator(".terrain-grid").boundingBox();
  const left = Math.max(0, Math.min(palette.x, grid.x) - 12);
  const top = Math.max(0, palette.y - 12);
  await page.screenshot({
    path: outPath,
    clip: {
      x: left,
      y: top,
      width: Math.max(palette.width, grid.width) + 24,
      height: grid.y + grid.height + 12 - top
    }
  });
  await browser.close();
} finally {
  stopServer();
}
