// SPDX-License-Identifier: MIT
// The pictures in docs/CREATING_A_GAME.md, driven rather than posed.
//
//   scripts/guide-shots.mjs           write docs/screenshots/guide/
//   scripts/guide-shots.mjs --check   regenerate elsewhere and compare bytes
//
// One Chromium, one production build, one sitting: a game is made from nothing
// and photographed at each step, as it is named, given a character, a map, a
// Stage with people on it, a way to win, a road between two Stages. Every
// image in the guide comes out of this walk, so a picture cannot show a
// surface the editor does not have: the click that would take it fails first.
//
// `scripts/readme-shot-editor.mjs` is the shape this follows, including its
// refusal to photograph a server it did not start.
import { createRequire } from "node:module";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// This checkout's pinned Chromium, unless the caller named one. `scripts/setup.sh`
// puts it here, as a symlink to the primary checkout in a worktree, and
// Playwright looks in its own cache instead unless told, so without this the
// command this script's own documentation gives fails on a set-up tree.
const browsers = path.join(root, ".playwright-browsers");
if (!process.env.PLAYWRIGHT_BROWSERS_PATH && fs.existsSync(browsers)) {
  process.env.PLAYWRIGHT_BROWSERS_PATH = browsers;
}

const require = createRequire(path.join(root, "editor", "package.json"));
const { chromium } = require("@playwright/test");

const checking = process.argv.includes("--check");
const committed = path.join(root, "docs", "screenshots", "guide");
const outDir = checking
  ? fs.mkdtempSync(path.join(os.tmpdir(), "grandleon-guide-"))
  : committed;
fs.mkdirSync(outDir, { recursive: true });

// Its own port, not the one the browser suite or the README shot use, so a
// gate and a refresh can run at the same time without either photographing the
// other's build.
const port = Number(process.env.GRANDLEON_GUIDE_SHOT_PORT ?? "4182");

// Refuse to photograph a server this script did not start. `--strictPort` makes
// the spawn below fail when the port is taken, and without this check the
// failure is silent: the wait loop then finds the stranger answering, and every
// picture in the guide is of whatever that process happens to be serving.
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

// Its own process group, so the kill below reaches the server rather than the
// `npx` wrapper in front of it. An orphaned listener holds the port, which the
// check above then reads as a stranger and refuses to run against.
const server = spawn(
  "npx", ["vite", "preview", "--port", String(port), "--strictPort"],
  { cwd: path.join(root, "editor"), stdio: "ignore", detached: true }
);
server.on("exit", (code) => {
  if (code) console.error(`preview server exited with code ${code}`);
});
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

const written = [];

try {
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
  // Wide enough that the record list, the record editor and the board it holds
  // each get the room the layout means them to have: on a narrow window the
  // stage editor folds into a column and a picture of it says more about the
  // window than about the editor.
  const page = await browser.newPage({
    viewport: { width: 1800, height: 1200 },
    deviceScaleFactor: 2
  });
  page.on("dialog", (dialog) => void dialog.accept());

  // The file is named by the step it belongs to, and the clip is the union of
  // whatever surfaces that step is about. It is several where the interesting
  // thing is one surface answering another and a box round only one would hide
  // it.
  const shoot = async (name, targets, extra = {}) => {
    const pad = 14;
    const boxes = [];
    for (const target of [targets].flat()) {
      // Document coordinates, not viewport ones. `boundingBox()` answers in
      // the viewport's frame while a full-page clip is measured from the top
      // of the document, and on a scrolled page the two differ by exactly the
      // scroll, which photographs a rectangle of the wrong part of the page
      // rather than failing.
      const box = await target.evaluate((element) => {
        const rect = element.getBoundingClientRect();
        return {
          x: rect.x + window.scrollX,
          y: rect.y + window.scrollY,
          width: rect.width,
          height: rect.height
        };
      });
      if (!box || box.width === 0) throw new Error(`${name}: nothing to photograph`);
      boxes.push(box);
    }
    // `extra.top` and `extra.bottom` adjust the padding for a surface whose
    // box is not where its picture ends: positive takes more room, negative
    // takes less. Negative is the common one: a form field's box stops above
    // the paragraph explaining it and below the tail of the one before.
    const left = Math.max(0, Math.min(...boxes.map((box) => box.x)) - pad);
    const top = Math.max(0, Math.min(...boxes.map((box) => box.y))
      - pad - (extra.top ?? 0));
    const right = Math.max(...boxes.map((box) => box.x + box.width)) + pad;
    const bottom = Math.max(...boxes.map((box) => box.y + box.height))
      + pad + (extra.bottom ?? 0);
    await page.screenshot({
      path: path.join(outDir, `${name}.png`),
      // Past the fold as well: several of these surfaces are taller than any
      // window, and a clip without this is silently cut at the viewport rather
      // than refused.
      fullPage: true,
      clip: { x: left, y: top, width: right - left, height: bottom - top }
    });
    written.push(`${name}.png`);
  };

  // Nothing below restyles the page to make a surface photograph better. Where
  // the editor clips its own content, as the flow graph does when it cuts the
  // last row of way out buttons off at its scroll box, the picture shows that,
  // because it is what an author sees.
  await page.goto(`http://127.0.0.1:${port}/`);

  // 1. The way in.
  await page.locator("#start").waitFor();
  await shoot("01-start", page.locator("#start"));

  // 2. A new game, named. The rest of the settings are already answered.
  await page.getByRole("button", { name: "Start a new game" }).click();
  await page.locator("#field-title").fill("The Salt Road");
  await page.locator("#field-characterStyleId").selectOption({ label: "Medieval" });
  await page.locator("#field-themeId").selectOption({ label: "Autumn" });
  await page.getByRole("button", { name: "Save game settings" }).click();
  // The whole of what a new game is asked, which is short enough to photograph
  // whole: five questions and the closed fold holding the two names that are
  // for a machine. The clip ends on the fold rather than before it, because a
  // picture that stopped above it would show a page with no way to the id and
  // the revision at all.
  await shoot("02-game", [
    page.locator('label[for="field-title"]'),
    page.locator(".game-settings > .schema-form details.advanced-fields")
  ], { top: -14 });

  const rail = page.locator('nav[aria-label="Project"]');

  // 3. One character, made by the wizard: whose side, what kind, their name.
  await rail.getByRole("button", { name: "Characters" }).click();
  await page.getByRole("button", { name: "New character" }).click();
  await page.locator('input[name="character-wizard-side"][value="your_side"]')
    .check();
  await page.getByRole("button", { name: "Next" }).click();
  await page.getByRole("radio", { name: /Commander/ }).click();
  await shoot("03-wizard-kind", page.locator(".character-wizard"));

  // 4. What one press writes, in the wizard's own words.
  await page.getByRole("button", { name: "Next" }).click();
  await page.locator("#character-wizard-name").fill("Wren");
  await page.locator(".wizard-panel .field-help").waitFor();
  await shoot("04-wizard-name", page.locator(".character-wizard"));
  await page.getByRole("button", { name: "Make them" }).click();
  await page.locator(".character-card").waitFor();

  // 5. Ground. A brush and a grid, and nothing about a fight.
  await rail.getByRole("button", { name: "Maps" }).click();
  await page.getByRole("button", { name: "Create map" }).click();
  await page.locator(".record-list").getByRole("button", { name: /New Map/ })
    .click();
  await page.locator(".terrain-grid").waitFor();
  await page.locator("#field-name").fill("The Ford");
  // The record form's submit is named for the record, so it reads "Save New
  // Map" until this is saved and "Save The Ford" after.
  await page.locator(".record-editor button[type=submit]").click();
  // A river down the middle with a bridge over it, and trees on the far bank:
  // painted by the same pointer presses a person makes, one terrain at a time.
  const paint = async (terrain, cells) => {
    await page.locator(".terrain-palette button", { hasText: terrain }).first()
      .click();
    for (const cell of cells) {
      await page.locator(`.terrain-grid [data-cell="${cell}"]`).click();
    }
  };
  await paint("water", [3, 11, 27, 35, 43]);
  await paint("bridge", [19]);
  await paint("forest", [5, 6, 13, 22, 30, 45]);
  await paint("rock", [0, 40]);
  // The history through the painted ground: the brush, the strokes, and what
  // can still be taken back. The undo row is in frame because the page says
  // how much it holds, and a count is the kind of claim a reader should be
  // able to check against the picture rather than take on trust.
  await shoot("05-map", [
    page.locator(".map-history"),
    page.locator(".terrain-palette"),
    page.locator(".terrain-grid")
  ]);

  // 6. The Stage. The ground was chosen on the map, so it is not asked again.
  await page.getByRole("button", { name: "Make a Stage on this ground" }).click();
  await page.getByRole("button", { name: "Make the Stage" }).click();
  const stage = page.locator(".stage-editor");
  await stage.waitFor();
  await page.locator("#stage-name").fill("The crossing");
  await page.locator("#stage-name").blur();

  // 7. Find-or-make. Nobody has authored a bandit; the palette offers one
  // anyway, and says what putting it down will write.
  await stage.locator('[data-palette="new:medieval_rogue"]').click();
  await stage.locator("#palette-new-name").fill("Bandit");
  await stage.getByRole("button", { name: "The enemy", exact: true }).click();
  await shoot("06-find-or-make", page.locator(".placement-palette"));

  // 8. Four presses: three of the one Bandit the first press minted, and Wren.
  await stage.locator('[data-cell="21"]').click();
  await stage.locator('[data-palette="unit:bandit"]')
    .waitFor({ state: "attached" });
  await stage.locator('[data-cell="22"]').click();
  await stage.locator('[data-cell="30"]').click();
  await stage.locator('[data-unit-type="wren"]').click();
  await stage.locator('[data-cell="8"]').click();
  await page.locator(".placed-unit").nth(3).waitFor();
  await shoot("07-board", page.locator(".placement-grid"));

  // 9. Who those presses turned out to be. Nobody answered a question about
  // it: the page reads standing off what the game references.
  await rail.getByRole("button", { name: "Characters" }).click();
  await page.locator(".character-cards").waitFor();
  await shoot("08-standing", page.locator(".character-roster"));

  // 10. How it is won.
  await rail.getByRole("button", { name: "Stages" }).click();
  await page.locator(".record-list").getByRole("button", { name: /The crossing/ })
    .click();
  await stage.waitFor();
  await stage.locator('[data-way-to-win="defeat_all_opponents"]').click();
  await page.locator(".condition-list").waitFor();
  await shoot("09-winning", page.locator(".win-conditions"));

  // 11. A second Stage, so the road has somewhere to go.
  await rail.getByRole("button", { name: "Maps" }).click();
  await page.getByRole("button", { name: "Create map" }).click();
  await page.locator(".record-list").getByRole("button", { name: /New Map/ })
    .click();
  await page.locator(".terrain-grid").waitFor();
  await page.locator("#field-name").fill("The salt flats");
  await page.locator(".record-editor button[type=submit]").click();
  await page.getByRole("button", { name: "Make a Stage on this ground" }).click();
  await page.getByRole("button", { name: "Make the Stage" }).click();
  await stage.waitFor();

  // 12. Flow: the road, and the way out dragged onto the next stop. The graph
  // is laid out left to right and scrolls on a normal window, so the window is
  // widened for this one picture rather than the graph being cut in half.
  await rail.getByRole("button", { name: "Flow" }).click();
  const graph = page.locator(".flow-graph");
  await graph.waitFor();
  await page.setViewportSize({ width: 2400, height: 1200 });
  // A drag that changes something. Making each Stage also made the stop that
  // follows it, so the road is already joined up end to end and re-pointing a
  // way out at the target it already has would photograph nothing. This sends
  // the first Stage straight at the second, past the story beat between them.
  // The graph then says that nothing leads to that beat any more, which is
  // the other half of what the picture is for.
  const wayOut = graph.locator("[data-way-out]").first();
  const source = (await wayOut.getAttribute("data-way-out")).split("/")[0];
  const secondStage = graph.locator("button.flow-stop-encounter").nth(1);
  if (await secondStage.getAttribute("data-stop") === source) {
    throw new Error("the two Stages came back in an order this did not expect");
  }
  await wayOut.dragTo(secondStage);
  await graph.locator(".flow-stop-warning").first().waitFor();
  await page.locator(".save-status").waitFor();
  await page.waitForTimeout(400);
  // The instruction above the graph is in the clip because it is the whole of
  // how the gesture is discovered.
  await shoot("10-flow", [
    page.locator(".flow-graph-panel > .field-help"),
    graph,
    page.locator(".flow-graph-warning")
  ]);
  await page.setViewportSize({ width: 1800, height: 1200 });

  // 13. Playing it, in the same page. The engine the consoles run, compiled to
  // WebAssembly.
  await page.getByRole("button", { name: "▶ Play" }).click();
  const play = page.locator(".play-mode");
  const toTheStage = play.getByRole("button", { name: "To the Stage" });
  await toTheStage.waitFor({ timeout: 60_000 });
  await toTheStage.click();
  await play.locator(".tactical-board").waitFor();
  // Pick Wren up, so the board is showing what she can reach rather than
  // sitting still: it is the thing a still picture of a tactics game most
  // wants to say.
  await play.locator('[aria-label^="Position 0, 1,"]').click();
  await play.locator(".play-actions button").first().waitFor();
  await page.waitForTimeout(600);
  await shoot("11-play", play.locator(".play-stage"));

  await browser.close();
} finally {
  stopServer();
}

written.sort();
if (!checking) {
  console.log(`Wrote ${written.length} pictures to ${outDir}`);
  for (const name of written) console.log(`  ${name}`);
} else {
  const problems = [];
  const before = fs.existsSync(committed)
    ? fs.readdirSync(committed).filter((name) => name.endsWith(".png")).sort()
    : [];
  for (const name of written) {
    const there = path.join(committed, name);
    if (!fs.existsSync(there)) { problems.push(`${name}: not committed`); continue; }
    if (!fs.readFileSync(there).equals(fs.readFileSync(path.join(outDir, name)))) {
      problems.push(`${name}: differs from what this walk produces`);
    }
  }
  for (const name of before) {
    if (!written.includes(name)) problems.push(`${name}: this walk produces no such picture`);
  }
  // And the third side of it. Matching bytes only say the pictures are current;
  // they say nothing about whether the page shows them. The names are parsed
  // out and compared as sets rather than searched for one substring at a time,
  // because `07-board.png` is a substring of `07-board.png.disabled` and a
  // containment test called that a reference.
  const guide = fs.readFileSync(
    path.join(root, "docs", "CREATING_A_GAME.md"), "utf8"
  );
  const shown = new Set(
    [...guide.matchAll(/screenshots\/guide\/([\w.-]+?\.png)\)/g)]
      .map((match) => match[1])
  );
  for (const name of written) {
    if (!shown.has(name)) {
      problems.push(`${name}: docs/CREATING_A_GAME.md never shows it`);
    }
  }
  for (const name of shown) {
    if (!written.includes(name)) {
      problems.push(`${name}: the guide shows it and this walk never takes it`);
    }
  }
  fs.rmSync(outDir, { recursive: true, force: true });
  if (problems.length) {
    for (const problem of problems) console.error(`  ${problem}`);
    console.error("\nRe-run scripts/guide-shots.mjs and commit the result.");
    process.exit(1);
  }
  console.log(`${written.length} pictures match the editor they came from`);
}
