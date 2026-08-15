// SPDX-License-Identifier: MIT
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { Window } from "happy-dom";

const editorDirectory = path.resolve(import.meta.dirname, "..");
const output = path.join(editorDirectory, "dist");
const html = fs.readFileSync(path.join(output, "index.html"), "utf8");
const entryMatch = html.match(
  /<script\b[^>]*type="module"[^>]*src="([^"]+)"[^>]*><\/script>/
);
assert.ok(entryMatch, "production index must declare a module entry script");

const entryUrl = entryMatch[1];
const assetMarker = "/assets/";
const assetOffset = entryUrl.indexOf(assetMarker);
assert.notEqual(assetOffset, -1, `module entry '${entryUrl}' is not a built asset`);
const entryPath = path.join(output, entryUrl.slice(assetOffset + 1));
assert.ok(fs.existsSync(entryPath), `module entry '${entryUrl}' is absent`);

// A generated validator can leave CommonJS/eval-based runtime code in the
// browser path. Those constructs violate the editor's production CSP and can
// fail only after an action first invokes the validator.
const browserScripts = fs.readdirSync(path.join(output, "assets"))
  .filter((filename) => filename.endsWith(".js"));
for (const filename of browserScripts) {
  const source = fs.readFileSync(path.join(output, "assets", filename), "utf8");
  for (const [name, pattern] of [
    ["eval", /\beval\s*\(/],
    ["Function constructor", /\bnew\s+Function\s*\(/],
    ["CommonJS require", /\brequire\s*\(/]
  ]) {
    assert.doesNotMatch(
      source,
      pattern,
      `production script '${filename}' contains CSP-incompatible ${name}`
    );
  }
}

const window = new Window({
  url: `https://editor.example.test${entryUrl.slice(0, assetOffset) || "/"}`
});
window.document.write(html);

for (const name of [
  "window",
  "document",
  "navigator",
  "location",
  "history",
  "Node",
  "Element",
  "HTMLElement",
  "SVGElement",
  "DocumentFragment",
  "Event",
  "CustomEvent",
  "MouseEvent",
  "DOMException",
  "Blob",
  "File",
  "FileReader"
]) {
  Object.defineProperty(globalThis, name, {
    configurable: true,
    writable: true,
    value: name === "window" ? window : window[name]
  });
}

try {
  await import(`${pathToFileURL(entryPath)}?startup-smoke=${Date.now()}`);
  await window.happyDOM.waitUntilComplete();

  assert.equal(
    window.document.querySelector("h1")?.textContent?.trim(),
    "Grandleon Editor",
    "browser entry did not mount the editor"
  );
  // Loading a sample over an open project asks for confirmation; happy-dom has
  // no dialog, so answer yes the way a person would.
  window.confirm = () => true;
  // The editor opens on the way in, which is where the examples are offered.
  assert.ok(
    window.document.querySelector("#start"),
    "mounted editor did not open on the start screen"
  );
  // Select the maintained demo explicitly. It is not the default sample, and it
  // is the one with a native golden hash behind it.
  const picker = window.document.querySelector(
    'input[name="start-sample"][value="demo"]'
  );
  assert.ok(picker, "start screen is missing the bundled examples");
  picker.click();
  const loadSample = [...window.document.querySelectorAll("button")]
    .find((button) => button.textContent?.trim() === "Open this example");
  assert.ok(loadSample, "start screen is missing the example action");
  loadSample.click();
  await window.happyDOM.waitUntilComplete();
  // What the game is called is a game-wide setting, and the game is the section
  // an author lands on, so it is the first thing readable.
  assert.equal(
    window.document.querySelector("#content-title")?.textContent?.trim(),
    "Game",
    "opening an example did not land on the game's own section"
  );
  assert.equal(
    window.document.querySelector("#field-title")?.value,
    "The Bridge at Dawn",
    "opening the example did not replace the project with the bundled demo"
  );
  // The browser playtest reports on the game rather than configuring it, so it
  // lives under Diagnostics; the page an author lands on holds the settings and
  // nothing else.
  const diagnostics = [...window.document.querySelectorAll(
    'nav[aria-label="Project"] button'
  )].find((button) => button.textContent?.trim().startsWith("Diagnostics"));
  assert.ok(diagnostics, "the rail is missing its diagnostics section");
  diagnostics.click();
  await window.happyDOM.waitUntilComplete();
  const runStage = [...window.document.querySelectorAll("button")]
    .find((button) => button.textContent?.trim() === "Run the Stage");
  assert.ok(runStage, "loaded demo is missing the browser playtest action");
  runStage.click();
  await window.happyDOM.waitUntilComplete();
  const board = window.document.querySelector("svg[aria-label='The board']");
  assert.ok(board, "browser playtest did not render its tactical map");
  const terrainSheets = [...board.querySelectorAll(".board-cell .terrain-image")]
    .map((cell) => cell.getAttribute("href"));
  assert.equal(terrainSheets.length, 24, "demo tactical map has the wrong tile count");
  assert.ok(
    terrainSheets.every((href) => href && /board\/terrain\/[a-z]+_blob\.png$/.test(href)),
    "every tactical terrain tile must blit a generated terrain sheet"
  );
  assert.ok(
    new Set(terrainSheets).size >= 3,
    "demo tactical map must visibly distinguish its authored terrain"
  );
  const unitSprites = [...board.querySelectorAll(".unit .unit-sprite")]
    .map((unit) => unit.getAttribute("href"));
  assert.ok(
    unitSprites.some((href) => href?.endsWith("_blue.png")) &&
      unitSprites.some((href) => href?.endsWith("_red.png")),
    "the two demo sides must render generated sprites in their faction colours"
  );

  // The board only renders if the WebAssembly simulation instantiated and
  // accepted the demo Stage, so reaching here already proves the engine
  // loaded from the production bundle. Read its canonical hash to prove it is
  // also producing authoritative state rather than an empty shell.
  const hash = window.document.querySelector("[data-canonical-hash]");
  assert.ok(hash, "the playtest must surface the engine's canonical state hash");
  assert.match(
    hash.textContent?.trim() ?? "",
    /^[0-9a-f]{16}$/,
    "the canonical state hash must be a 64-bit value"
  );

  // Play is the product's primary command and must reach a playable board
  // without any editor verb in between.
  const play = [...window.document.querySelectorAll("button")]
    .find((button) => button.textContent?.trim() === "▶ Play");
  assert.ok(play, "the application shell is missing the primary Play command");
  play.click();
  await window.happyDOM.waitUntilComplete();

  const playMode = window.document.querySelector('[role="dialog"][aria-modal="true"]');
  assert.ok(playMode, "Play did not open the full-screen play surface");

  // A kept campaign stands on the company before every board, so the surface
  // opens on the screen between Stages and the board is one press away. The
  // press is part of the smoke rather than skipped: it proves the campaign
  // session's management stage instantiated in the production bundle too.
  const toTheStage = [...playMode.querySelectorAll("button")]
    .find((button) => button.textContent?.trim().startsWith("To the Stage"));
  assert.ok(toTheStage, "Play did not open on the company between Stages");
  toTheStage.click();
  await window.happyDOM.waitUntilComplete();

  assert.equal(
    playMode.querySelectorAll('[role="gridcell"]').length,
    24,
    "Play mode did not start the demo Stage on its own"
  );
  assert.ok(
    [...playMode.querySelectorAll("button")]
      .some((button) => button.textContent?.trim().startsWith("← Back to editing")),
    "Play mode must offer a visible way back to editing"
  );

  // Play until someone wins. The player steers the first side only; the second
  // side now acts on its own from the behaviour authored on each placement, so
  // this drives blue and lets red answer.
  const playCell = (x, y) => {
    const found = playMode.querySelector(`[aria-label^="Position ${x}, ${y},"]`);
    assert.ok(found, `play surface is missing cell ${x},${y}`);
    found.click();
    return window.happyDOM.waitUntilComplete();
  };

  await playCell(0, 1);
  await playCell(1, 1);
  // The walk does not end the turn. Two action points is what a turn means
  // here, so the rider arrives still owing the board a strike, and the
  // surface says so rather than leaving the player to discover it by being
  // refused.
  assert.match(
    playMode.textContent ?? "",
    /has moved\. Strike, or say you are done with them/,
    "a rider that walked was treated as finished by its own step"
  );
  await playCell(1, 1);
  await playCell(2, 1);
  // And now the turn is closed, so red answers on its own: the rider has taken
  // the free counter to its own blow and then the picket's swing, which is six
  // off seven and reachable no other way.
  assert.match(
    playMode.textContent ?? "",
    /HP 1\/7/,
    "the opposing side did not take its own turn"
  );
  for (let round = 0; round < 6; round += 1) {
    if (/wins!/.test(playMode.textContent ?? "")) break;
    await playCell(1, 1);
    await playCell(2, 1);
  }
  assert.match(
    playMode.textContent ?? "",
    /wins!/,
    "the demo Stage did not reach a decided outcome in Play mode"
  );

} finally {
  await window.happyDOM.abort();
  window.close();
}

console.log("editor production browser startup smoke test passed");
