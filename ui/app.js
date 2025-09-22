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
  const digitsEl = document.getElementById('digits');

  document.getElementById('send').addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshAll();
  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshAll();
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

  function refreshDigits() {
    const maxDigits = 128;
    const digitsPtr = wasm.kol_alloc(maxDigits);
    const lenPtr = wasm.kol_alloc(4);
    let count = 0;

    try {
      const result = wasm.kol_emit_digits(digitsPtr, maxDigits, lenPtr);
      if (result !== 0) {
        digitsEl.textContent = '—';
        return;
      }

      const lenView = new Uint32Array(wasm.memory.buffer, lenPtr, 1);
      count = Math.min(lenView[0], maxDigits);
      const digitsView = new Uint8Array(wasm.memory.buffer, digitsPtr, maxDigits);
      const digits = Array.from(digitsView.subarray(0, count));

      if (digits.length === 0) {
        digitsEl.textContent = '—';
        return;
      }

      digitsEl.innerHTML = digits
        .map(
          (digit, idx) =>
            `<div class="digit-cell" style="--digit:${digit}"><span class="digit-value">${digit}</span><span class="digit-index">${idx}</span></div>`
        )
        .join('');
    } finally {
      wasm.kol_free(digitsPtr);
      wasm.kol_free(lenPtr);
    }
  }

  function refreshAll() {
    refreshHud();
    refreshTail();
    refreshDigits();
  }

  refreshAll();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
