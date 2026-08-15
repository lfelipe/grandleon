// SPDX-License-Identifier: MIT
import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const listeners = new Map();
const cachedUrls = [];
const shellResponse = new Response(
  '<link rel="manifest" href="./manifest.webmanifest">' +
  '<script src="/tools/grandleon/assets/app-hash.js"></script>',
  { status: 200 }
);
let networkAvailable = true;
const fallback = new Response("offline editor shell", { status: 200 });

const cache = {
  async addAll(urls) {
    cachedUrls.push(...urls);
  },
  async put() {}
};
const caches = {
  async open() { return cache; },
  async keys() { return ["old-shell", "grandleon-editor-shell-v1"]; },
  async delete() { return true; },
  async match(request) {
    const url = typeof request === "string" ? request : request.url;
    return url === "https://example.test/tools/grandleon/" ? fallback : undefined;
  }
};

const context = {
  URL,
  Promise,
  Response,
  caches,
  fetch: async (request) => {
    if (!networkAvailable) {
      throw new Error("offline");
    }
    const url = typeof request === "string" ? request : request.url;
    if (url === "https://example.test/tools/grandleon/") {
      return shellResponse;
    }
    return new Response("asset", { status: 200 });
  },
  self: {
    location: {
      href: "https://example.test/tools/grandleon/sw.js",
      origin: "https://example.test"
    },
    clients: { async claim() {} },
    async skipWaiting() {},
    addEventListener(type, listener) {
      listeners.set(type, listener);
    }
  }
};

vm.runInNewContext(fs.readFileSync("public/sw.js", "utf8"), context);

let installPromise;
listeners.get("install")({
  waitUntil(promise) { installPromise = promise; }
});
await installPromise;
assert.ok(cachedUrls.includes("https://example.test/tools/grandleon/"));
assert.ok(cachedUrls.includes(
  "https://example.test/tools/grandleon/assets/app-hash.js"
));
assert.ok(cachedUrls.includes(
  "https://example.test/tools/grandleon/manifest.webmanifest"
));

networkAvailable = false;
let navigationPromise;
listeners.get("fetch")({
  request: {
    method: "GET",
    mode: "navigate",
    url: "https://example.test/tools/grandleon/"
  },
  respondWith(promise) { navigationPromise = promise; }
});
const response = await navigationPromise;
assert.equal(await response.text(), "offline editor shell");

console.log("editor offline startup policy smoke test passed");
