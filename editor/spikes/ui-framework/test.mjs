// SPDX-License-Identifier: MIT
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

const sources = {
  react: fs.readFileSync("react/src/main.tsx", "utf8"),
  vue: fs.readFileSync("vue/src/App.vue", "utf8"),
  svelte: fs.readFileSync("svelte/src/App.svelte", "utf8")
};

const requiredPatterns = [
  [/skip-link/, "skip link"],
  [/aria-label=["']Project["']/, "project navigation label"],
  [/aria-live=["']polite["']/, "live save status"],
  [/aria-invalid/, "invalid-field state"],
  [/<dialog|<dialog /, "native modal"],
  [/Project name is required/, "linked validation explanation"],
  [/focus\(\)/, "focus restoration"]
];

for (const [candidate, source] of Object.entries(sources)) {
  for (const [pattern, feature] of requiredPatterns) {
    assert.match(source, pattern, `${candidate} must implement ${feature}`);
  }

  const index = fs.readFileSync(path.join("dist", candidate, "index.html"), "utf8");
  assert.match(index, /(?:src|href)="\.\/assets\//, `${candidate} build must use a relative base`);

  const assetDirectory = path.join("dist", candidate, "assets");
  const initialBytes = fs.readdirSync(assetDirectory)
    .filter((name) => /\.(js|css)$/.test(name))
    .reduce(
      (total, name) => total + fs.statSync(path.join(assetDirectory, name)).size,
      0
    );
  assert.ok(initialBytes < 250_000, `${candidate} initial raw assets exceed spike guardrail`);
}

console.log("framework spike contract checks passed");
