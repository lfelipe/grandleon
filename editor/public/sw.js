// SPDX-License-Identifier: MIT
// Bump this whenever the worker's behaviour changes, not only when the shell
// does. Activation deletes every cache whose name is not this one, so a worker
// that starts answering differently while keeping the old name inherits the
// entries the old rules wrote, which is a new worker serving an old decision.
const cacheName = "grandleon-editor-shell-v2";
const scopeUrl = new URL("./", self.location.href).href;

async function shellUrls() {
  const response = await fetch(scopeUrl, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`editor shell returned ${response.status}`);
  }
  const html = await response.text();
  const urls = [scopeUrl, new URL("manifest.webmanifest", scopeUrl).href];
  for (const match of html.matchAll(/(?:src|href)="([^"]+)"/g)) {
    const url = new URL(match[1], scopeUrl);
    if (url.origin === self.location.origin) {
      urls.push(url.href);
    }
  }
  return [...new Set(urls)];
}

self.addEventListener("install", (event) => {
  event.waitUntil(
    shellUrls()
      .then((urls) => caches.open(cacheName).then((cache) => cache.addAll(urls)))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(
        keys.filter((key) => key !== cacheName).map((key) => caches.delete(key))
      ))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") {
    return;
  }
  if (event.request.mode === "navigate") {
    event.respondWith(fetch(event.request).catch(() => caches.match(scopeUrl)));
    return;
  }
  const requestUrl = new URL(event.request.url);
  if (requestUrl.origin !== self.location.origin) {
    return;
  }
  // This worker caches the shell so the editor opens without a network. `/api`
  // is not the shell. It is live state, and answering it from a cache is
  // answering it with the past.
  //
  // A ROM build is the case that proves it. The editor asks
  // `/api/n64/build/<id>` every two seconds until the answer stops being
  // "building"; cache the first of those and every later poll is served the
  // same "building" for ever, while the finished ROM sits on disk. The health
  // check has the same shape, so a cached one can leave the ROM button
  // disabled after the service is up, or enabled after it has gone.
  //
  // Invisible to the browser suite, which starts from a clean profile every
  // run and so never has a stale cache to be wrong from. It is the long-lived
  // browser, somebody's editor tab open all day, that this hurts.
  if (requestUrl.pathname.startsWith("/api/")) {
    return;
  }
  event.respondWith(
    caches.match(event.request).then((cached) =>
      cached ?? fetch(event.request).then((response) => {
        if (response.ok) {
          const copy = response.clone();
          void caches.open(cacheName).then((cache) => cache.put(event.request, copy));
        }
        return response;
      })
    )
  );
});
