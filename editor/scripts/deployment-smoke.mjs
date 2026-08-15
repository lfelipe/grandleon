// SPDX-License-Identifier: MIT
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";

const editorDirectory = path.resolve(import.meta.dirname, "..");
const vite = path.join(editorDirectory, "node_modules/vite/bin/vite.js");

function build(base) {
  const result = spawnSync(
    process.execPath,
    [vite, "build", "--config", path.join(editorDirectory, "vite.config.ts")],
    {
      cwd: editorDirectory,
      encoding: "utf8",
      env: { ...process.env, GRANDLEON_EDITOR_BASE: base }
    }
  );
  if (result.status !== 0) {
    throw new Error(result.stderr || result.stdout);
  }
}

function assertBuild(base) {
  const output = path.join(editorDirectory, "dist");
  const html = fs.readFileSync(path.join(output, "index.html"), "utf8");
  if (!html.includes("Content-Security-Policy")) {
    throw new Error("production index is missing its baseline CSP");
  }
  // Chrome refuses WebAssembly.instantiate under a script-src that does not
  // permit it, so without this the simulation loads everywhere except a real
  // browser. happy-dom does not enforce CSP, so no other check would notice.
  if (!/script-src[^;"]*'wasm-unsafe-eval'/.test(html)) {
    throw new Error(
      "production CSP must allow 'wasm-unsafe-eval' or the simulation cannot load"
    );
  }
  if (/script-src[^;"]*'unsafe-eval'(?!\w)/.test(html.replace(/'wasm-unsafe-eval'/g, ""))) {
    throw new Error("production CSP must not broaden to full 'unsafe-eval'");
  }

  const assetUrls = [...html.matchAll(/(?:src|href)="([^"]*assets\/[^"]+)"/g)]
    .map((match) => match[1]);
  if (assetUrls.length < 2) {
    throw new Error("production index did not declare hashed JS and CSS assets");
  }

  for (const url of assetUrls) {
    if (!url.startsWith(base)) {
      throw new Error(`asset '${url}' does not use configured base '${base}'`);
    }
    if (!/\/assets\/[^/]+-[A-Za-z0-9_-]+\.(?:js|css)$/.test(url)) {
      throw new Error(`asset '${url}' is not content-hashed`);
    }
    const relative = url.slice(base.length);
    if (!fs.existsSync(path.join(output, relative))) {
      throw new Error(`asset '${url}' is absent from static output`);
    }
  }
}

function assertBrowserStartup() {
  const result = spawnSync(
    process.execPath,
    [path.join(editorDirectory, "scripts/browser-startup-smoke.mjs")],
    { cwd: editorDirectory, encoding: "utf8" }
  );
  if (result.status !== 0) {
    throw new Error(result.stderr || result.stdout);
  }
}

async function serveAndFetch(base) {
  const output = path.join(editorDirectory, "dist");
  const server = http.createServer((request, response) => {
    const requestUrl = request.url ?? "/";
    if (!requestUrl.startsWith(base)) {
      response.writeHead(404).end();
      return;
    }
    const relative = requestUrl.slice(base.length).split(/[?#]/, 1)[0] || "index.html";
    const filename = path.join(output, relative);
    if (!filename.startsWith(output) || !fs.existsSync(filename)) {
      response.writeHead(404).end();
      return;
    }
    response.setHeader(
      "Cache-Control",
      relative === "index.html" ? "no-cache" : "public, max-age=31536000, immutable"
    );
    response.writeHead(200).end(fs.readFileSync(filename));
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  try {
    const address = server.address();
    if (!address || typeof address === "string") {
      throw new Error("static test server did not expose a TCP address");
    }
    const response = await fetch(`http://127.0.0.1:${address.port}${base}`);
    if (!response.ok || !response.headers.get("cache-control")?.includes("no-cache")) {
      throw new Error(`static entry request failed for base '${base}'`);
    }
    const html = await response.text();
    for (const match of html.matchAll(/(?:src|href)="([^"]*assets\/[^"]+)"/g)) {
      const asset = await fetch(`http://127.0.0.1:${address.port}${match[1]}`);
      if (!asset.ok || !asset.headers.get("cache-control")?.includes("immutable")) {
        throw new Error(`static asset request failed for '${match[1]}'`);
      }
    }
  } finally {
    await new Promise((resolve, reject) =>
      server.close((error) => error ? reject(error) : resolve())
    );
  }
}

const bases = ["/", "/tools/grandleon/"];
for (const base of bases) {
  build(base);
  assertBuild(base);
  assertBrowserStartup();
  await serveAndFetch(base);
}

// Leave dist/ as the root-base production build. Without this, whichever base
// the loop tested last stayed on disk, and anything serving editor/dist
// afterwards, a long-lived preview or a reused Playwright server, quietly
// served a sub-path build whose assets resolve to the SPA fallback. Chromium
// then receives HTML where it expects a JS module and the app never boots.
build("/");
console.log("editor root/sub-path deployment smoke tests passed");
