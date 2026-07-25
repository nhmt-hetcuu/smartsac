const CACHE_NAME = 'smartcharger-pwa-v5';
const OFFLINE_URL = '/offline';

const CORE_ASSETS = [
  '/offline',
  '/manifest.webmanifest',
  '/static/css/index.css',
  '/static/img/logo.png'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(CORE_ASSETS)).catch(() => Promise.resolve())
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => Promise.all(
      keys.map((key) => {
        if (key !== CACHE_NAME) {
          return caches.delete(key);
        }
        return Promise.resolve();
      })
    ))
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  const { request } = event;
  const url = new URL(request.url);

  if (request.method !== 'GET') {
    return;
  }

  // Never cache realtime API responses.
  if (url.pathname.startsWith('/api/')) {
    event.respondWith(fetch(request).catch(() => new Response('{"status":"error","message":"offline"}', {
      status: 503,
      headers: { 'Content-Type': 'application/json' }
    })));
    return;
  }

  // Do not cache cross-origin resources (CDN libs), always ask network.
  if (url.origin !== self.location.origin) {
    event.respondWith(fetch(request));
    return;
  }

  if (request.mode === 'navigate') {
    event.respondWith(
      fetch(request)
        .then((response) => {
          // Avoid storing dynamic HTML to reduce stale dashboard pages.
          const contentType = response.headers.get('Content-Type') || '';
          if (!contentType.includes('text/html')) {
            const clone = response.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(request, clone));
          }
          return response;
        })
        .catch(() => caches.match(request).then((cached) => cached || caches.match(OFFLINE_URL)))
    );
    return;
  }

  event.respondWith(
    caches.match(request).then((cached) => {
      if (cached) {
        return cached;
      }
      return fetch(request)
        .then((response) => {
          const clone = response.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(request, clone));
          return response;
        })
        .catch(() => caches.match(OFFLINE_URL));
    })
  );
});

