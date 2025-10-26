const CACHE = 'kolibri-cache-v2';
const OFFLINE_URL = '../offline.html';
const ASSETS = [
  '../index.html',
  '../app.js',
  '../style.css',
  '../kolibri.wasm',
  OFFLINE_URL,
  './manifest.json',
  './icons/icon-72.svg',
  './icons/icon-96.svg',
  './icons/icon-128.svg',
  './icons/icon-192.svg',
  './icons/icon-256.svg',
  './icons/icon-384.svg',
  './icons/icon-512.svg'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches
      .open(CACHE)
      .then((cache) => cache.addAll(ASSETS))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') {
    return;
  }

  event.respondWith(
    (async () => {
      try {
        const networkResponse = await fetch(event.request);
        if (networkResponse && networkResponse.status === 200) {
          const cache = await caches.open(CACHE);
          cache.put(event.request, networkResponse.clone());
        }
        return networkResponse;
      } catch (error) {
        const cachedResponse = await caches.match(event.request);
        if (cachedResponse) {
          return cachedResponse;
        }
        if (event.request.mode === 'navigate' || event.request.destination === 'document') {
          const offlinePage = await caches.match(OFFLINE_URL);
          if (offlinePage) {
            return offlinePage;
          }
          return new Response('<h1>Offline</h1>', {
            headers: { 'Content-Type': 'text/html; charset=utf-8' },
            status: 503
          });
        }
        return new Response('offline', { status: 503 });
      }
    })()
  );
});
