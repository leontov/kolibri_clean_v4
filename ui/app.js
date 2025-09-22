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
  const answerEl = document.getElementById('answer');
  const sendBtn = document.getElementById('send');
  const tickBtn = document.getElementById('tick');
  const autoCountEl = document.getElementById('autoCount');
  const autoRunBtn = document.getElementById('autoRun');
  const autoStopBtn = document.getElementById('autoStop');

  let autoAbort = false;
  let autoPromise = null;

  sendBtn.addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshAll();
  });

  tickBtn.addEventListener('click', () => {
    wasm.kol_tick();
    refreshAll();
  });

  autoRunBtn.addEventListener('click', async () => {
    if (autoPromise) {
      return;
    }
    const desired = parseInt(autoCountEl.value, 10);
    if (!Number.isFinite(desired) || desired <= 0) {
      return;
    }
    autoAbort = false;
    setAutoRunning(true);
    autoPromise = runAutoTicks(desired);
    try {
      await autoPromise;
    } finally {
      autoPromise = null;
      setAutoRunning(false);
      autoAbort = false;
      refreshAll();
    }
  });

  autoStopBtn.addEventListener('click', () => {
    if (autoPromise) {
      autoAbort = true;
    }
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

  function refreshAnswer() {
    if (!answerEl || typeof wasm.kol_emit_text !== 'function') {
      return;
    }
    const cap = 4096;
    const ptr = wasm.kol_alloc(cap);
    if (!ptr) {
      answerEl.textContent = '';
      return;
    }
    const len = wasm.kol_emit_text(ptr, cap);
    const text = len > 0 ? readString(instance, ptr, len) : '';
    wasm.kol_free(ptr);
    answerEl.textContent = text;
  }

  function refreshAll() {
    refreshHud();
    refreshTail();
    refreshAnswer();
  }

  function setAutoRunning(running) {
    sendBtn.disabled = running;
    tickBtn.disabled = running;
    autoRunBtn.disabled = running;
    autoCountEl.disabled = running;
    autoStopBtn.disabled = !running;
  }

  async function runAutoTicks(count) {
    for (let i = 0; i < count; ++i) {
      if (autoAbort) {
        break;
      }
      wasm.kol_tick();
      refreshAll();
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
  }

  setAutoRunning(false);
  refreshAll();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
