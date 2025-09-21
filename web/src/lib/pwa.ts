let deferredPrompt: any = null;
const listeners = new Set<(available: boolean) => void>();

function notify() {
  const available = Boolean(deferredPrompt);
  listeners.forEach((listener) => listener(available));
}

export function onInstallPrompt(listener: (available: boolean) => void) {
  listeners.add(listener);
  listener(Boolean(deferredPrompt));
  return () => listeners.delete(listener);
}

export async function showInstallPrompt() {
  if (!deferredPrompt) return;
  deferredPrompt.prompt();
  await deferredPrompt.userChoice;
  deferredPrompt = null;
  notify();
}

export function registerKolibriServiceWorker() {
  if (typeof window === "undefined" || !("serviceWorker" in navigator)) {
    return;
  }
  window.addEventListener("beforeinstallprompt", (event) => {
    event.preventDefault();
    deferredPrompt = event;
    notify();
  });
  window.addEventListener("load", () => {
    const swUrl = new URL("../service-worker.ts", import.meta.url);
    navigator.serviceWorker
      .register(swUrl, { type: "module" })
      .then(() => console.info("Kolibri service worker registered"))
      .catch((err) => console.error("SW registration failed", err));
  });
}
