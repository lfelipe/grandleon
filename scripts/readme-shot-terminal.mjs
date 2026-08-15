// SPDX-License-Identifier: MIT
// Renders the terminal client's ANSI output to a PNG through Chromium.
//
// Only the escape codes the client actually emits are handled; anything else
// would be dead code pretending to be a terminal emulator.
import { createRequire } from "node:module";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const require = createRequire(path.join(root, "editor", "package.json"));
const { chromium } = require("@playwright/test");

const [, , inputPath, outPath] = process.argv;
const raw = fs.readFileSync(inputPath, "utf8");

const palette = {
  "1;34": "#5ba3d9",
  "1;31": "#e0685c",
  "1;33": "#f2c14e",
  "2": "#7d8a85"
};

function escapeHtml(text) {
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

let html = "";
let open = false;
for (const part of raw.split(/(\x1b\[[0-9;]*m)/)) {
  const code = part.match(/^\x1b\[([0-9;]*)m$/);
  if (!code) {
    html += escapeHtml(part);
    continue;
  }
  if (open) {
    html += "</span>";
    open = false;
  }
  const colour = palette[code[1]];
  if (colour) {
    html += `<span style="color:${colour};font-weight:bold">`;
    open = true;
  }
}
if (open) html += "</span>";

const page_html = `<!doctype html><meta charset="utf-8">
<body style="margin:0;background:#0d1a1b">
<pre style="margin:0;padding:26px;background:#0d1a1b;color:#d8e0da;
font:15px/1.35 'DejaVu Sans Mono',monospace;display:inline-block;
min-width:560px">${html}</pre>`;

const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 2 });
await page.setContent(page_html);
await page.locator("pre").screenshot({ path: outPath });
await browser.close();
