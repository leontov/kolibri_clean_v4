const STATIC_CACHE = "kolibri-static-v1";
const OFFLINE_URL = "/offline.html";
const STATIC_ASSETS = [
  OFFLINE_URL,
  "/kolibri.svg",
  "/manifest.webmanifest"
];

self.addEventListener("install", event => {
  event.waitUntil(
    caches.open(STATIC_CACHE).then(cache => cache.addAll(STATIC_ASSETS)).then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", event => {
  event.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(key => key !== STATIC_CACHE).map(key => caches.delete(key)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", event => {
  const { request } = event;
  if (request.method !== "GET") {
    return;
  }
  if (request.headers.get("accept") === "text/event-stream") {
    return;
  }
  if (request.url.includes("/api/")) {
    event.respondWith(
      fetch(request).catch(() => caches.match(OFFLINE_URL))
    );
    return;
  }
  event.respondWith(
    caches.match(request).then(cacheResponse =>
      cacheResponse || fetch(request).then(networkResponse => {
        const copy = networkResponse.clone();
        caches.open(STATIC_CACHE).then(cache => cache.put(request, copy));
        return networkResponse;
      }).catch(() => caches.match(OFFLINE_URL))
    )
  );
});
