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
  const memoryEl = document.getElementById('memory');

  const LANGUAGE_PREFIX = 'Kolibri запомнил:';
  const LANGUAGE_DEFAULT_MESSAGE = 'Колибри пока молчит...';

  const sendBtn = document.getElementById('send');
  let isSending = false;

  async function handleSend() {
    if (isSending) return;
    const txt = msgEl.value.trim();
    if (!txt) return;


    isSending = true;
    const originalText = sendBtn.textContent;
    sendBtn.disabled = true;
    sendBtn.textContent = 'Отправка…';

    try {
      const ptr = writeString(instance, txt);
      wasm.kol_chat_push(ptr);
      wasm.kol_free(ptr);
      msgEl.value = '';
      refreshHud();
      refreshTail();
    } finally {
      sendBtn.disabled = false;
      sendBtn.textContent = originalText;
      isSending = false;
    }
  }

  sendBtn.addEventListener('click', () => {
    handleSend();
  });

  msgEl.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      handleSend();
    }

    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshHud();
    refreshTail();
    refreshLanguageSummary();

  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();
    refreshLanguageSummary();
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

  function refreshLanguageSummary() {
    const cap = 512;
    const ptr = wasm.kol_alloc(cap);
    if (ptr === 0) {
      memoryEl.textContent = LANGUAGE_DEFAULT_MESSAGE;
      return;
    }
    const len = wasm.kol_language_generate(ptr, cap);
    const text = len > 0 ? readString(instance, ptr, len) : '';
    wasm.kol_free(ptr);

    let display = text.trim();
    if (len <= 0 || display.length === 0) {
      memoryEl.textContent = LANGUAGE_DEFAULT_MESSAGE;
      return;
    }
    if (display === LANGUAGE_DEFAULT_MESSAGE) {
      memoryEl.textContent = LANGUAGE_DEFAULT_MESSAGE;
      return;
    }
    if (display.startsWith(LANGUAGE_PREFIX)) {
      display = display.slice(LANGUAGE_PREFIX.length).trim();
    }
    memoryEl.textContent = display.length > 0 ? display : LANGUAGE_DEFAULT_MESSAGE;
  }

  refreshHud();
  refreshTail();
  refreshLanguageSummary();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
