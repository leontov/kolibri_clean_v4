const STATIC_CACHE = "kolibri-static-v1";
const RUNTIME_CACHE = "kolibri-runtime-v1";
const OFFLINE_FALLBACK = "/offline.html";

self.addEventListener("install", (event: ExtendableEvent) => {
  event.waitUntil(
    caches.open(STATIC_CACHE).then((cache) => cache.addAll(["/", OFFLINE_FALLBACK, "/manifest.webmanifest"]))
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event: ExtendableEvent) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((key) => ![STATIC_CACHE, RUNTIME_CACHE].includes(key)).map((key) => caches.delete(key))))
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event: FetchEvent) => {
  const request = event.request;
  const url = new URL(request.url);

  if (request.method !== "GET") {
    return;
  }

  if (url.pathname.startsWith("/api/v1/chain/stream")) {
    return;
  }

  if (url.pathname.startsWith("/api/v1/status")) {
    event.respondWith(staleWhileRevalidate(request));
    return;
  }

  if (url.pathname.startsWith("/api/")) {
    return;
  }

  if (request.mode === "navigate") {
    event.respondWith(
      fetch(request)
        .then((response) => {
          const copy = response.clone();
          caches.open(STATIC_CACHE).then((cache) => cache.put(request, copy));
          return response;
        })
        .catch(() => caches.match(OFFLINE_FALLBACK))
    );
    return;
  }

  if (url.pathname.startsWith("/assets") || ["style", "script", "font", "image"].includes(request.destination)) {
    event.respondWith(cacheFirst(request));
    return;
  }
});

function cacheFirst(request: Request) {
  return caches.match(request).then((cached) => {
    if (cached) {
      return cached;
    }
    return fetch(request).then((response) => {
      const copy = response.clone();
      caches.open(STATIC_CACHE).then((cache) => cache.put(request, copy));
      return response;
    });
  });
}

function staleWhileRevalidate(request: Request) {
  return caches.match(request).then((cached) => {
    const fetchPromise = fetch(request)
      .then((response) => {
        const copy = response.clone();
        caches.open(RUNTIME_CACHE).then((cache) => cache.put(request, copy));
        return response;
      })
      .catch(() => cached);
    return cached || fetchPromise;
  });
}
