const encoder = new TextEncoder();
const decoder = new TextDecoder();

async function loadWasm() {
  let response;
  try {
    response = await fetch('./kolibri.wasm');
  } catch (error) {
    throw new Error(`Не удалось запросить kolibri.wasm: ${error instanceof Error ? error.message : String(error)}`);
  }

  if (!response.ok) {
    throw new Error(`Не удалось загрузить kolibri.wasm: статус ${response.status}`);
  }

  let buffer;
  try {
    buffer = await response.arrayBuffer();
  } catch (error) {
    throw new Error(`Не удалось прочитать kolibri.wasm: ${error instanceof Error ? error.message : String(error)}`);
  }

  try {
    const module = await WebAssembly.instantiate(buffer, {});
    return module.instance;
  } catch (error) {
    throw new Error(`Не удалось инициализировать WebAssembly: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function writeString(wasm, text) {
  const bytes = encoder.encode(text + '\0');
  const ptr = wasm.exports.kol_alloc(bytes.length);
  const mem = new Uint8Array(wasm.exports.memory.buffer, ptr, bytes.length);
  mem.set(bytes);
  return ptr;
}

function readString(wasm, ptr, len) {
  const mem = new Uint8Array(wasm.exports.memory.buffer, ptr, len);
  return decoder.decode(mem.subarray(0, len));
}

async function boot() {
  const msgEl = document.getElementById('msg');
  const effEl = document.getElementById('eff');
  const complEl = document.getElementById('compl');
  const outEl = document.getElementById('out');
  const sendEl = document.getElementById('send');
  const tickEl = document.getElementById('tick');
  const loadingEl = document.getElementById('loading');
  const errorEl = document.getElementById('error');

  if (!msgEl || !effEl || !complEl || !outEl || !sendEl || !tickEl) {
    const message = 'Не удалось инициализировать интерфейс: отсутствуют элементы управления.';
    if (errorEl) {
      errorEl.textContent = message;
    }
    throw new Error(message);
  }

  const toggleControls = (disabled) => {
    if (msgEl) msgEl.disabled = disabled;
    if (sendEl) sendEl.disabled = disabled;
    if (tickEl) tickEl.disabled = disabled;
  };

  toggleControls(true);
  if (loadingEl) loadingEl.hidden = false;

  let instance;
  try {
    instance = await loadWasm();
  } catch (error) {
    if (loadingEl) loadingEl.textContent = 'Ошибка загрузки Kolibri';
    if (errorEl) {
      errorEl.textContent = error instanceof Error ? error.message : String(error);
    }
    throw error;
  }

  const wasm = instance.exports;
  wasm.kol_init(3, 12345);

  toggleControls(false);
  if (loadingEl) loadingEl.hidden = true;

  sendEl.addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshHud();
  });

  tickEl.addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();
  });

  function refreshHud() {
    effEl.textContent = wasm.kol_eff().toFixed(4);
    complEl.textContent = wasm.kol_compl().toFixed(2);
  }

  function refreshTail() {
    const cap = 8192;
    const ptr = wasm.kol_alloc(cap);
    const len = wasm.kol_tail_json(ptr, cap, 10);
    const json = readString(instance, ptr, len > 0 ? len : 0);
    wasm.kol_free(ptr);
    outEl.textContent = json;
  }

  refreshHud();
  refreshTail();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

async function start() {
  try {
    await boot();
  } catch (error) {
    const errorEl = document.getElementById('error');
    if (errorEl && !errorEl.textContent) {
      errorEl.textContent = error instanceof Error ? error.message : String(error);
    }
  }
}

start();
