const encoder = new TextEncoder();
const decoder = new TextDecoder();

async function loadWasm() {
  const response = await fetch('./kolibri.wasm');
  const buffer = await response.arrayBuffer();
  const module = await WebAssembly.instantiate(buffer, {});
  return module.instance;
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
  const instance = await loadWasm();
  const wasm = instance.exports;
  wasm.kol_init(3, 12345);

  const msgEl = document.getElementById('msg');
  const effEl = document.getElementById('eff');
  const complEl = document.getElementById('compl');
  const outEl = document.getElementById('out');
  const storyEl = document.getElementById('story');
  const memoryEl = document.getElementById('memory');

  document.getElementById('send').addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshHud();
    refreshTail();
    refreshInsights();
  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();
    refreshInsights();
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

  function readWasmText(invoker) {
    const cap = 2048;
    const ptr = wasm.kol_alloc(cap);
    try {
      const len = invoker(ptr, cap);
      const usable = len > 0 ? len : 0;
      const raw = readString(instance, ptr, usable);
      return raw.trim();
    } finally {
      wasm.kol_free(ptr);
    }
  }

  function refreshNarrative() {
    const text = readWasmText((ptr, cap) => wasm.kol_emit_text(ptr, cap));
    storyEl.textContent = text || '—';
  }

  function refreshMemory() {
    const text = readWasmText((ptr, cap) => wasm.kol_language_generate(ptr, cap));
    memoryEl.textContent = text || '—';
  }

  function refreshInsights() {
    refreshNarrative();
    refreshMemory();
  }

  refreshHud();
  refreshTail();
  refreshInsights();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
