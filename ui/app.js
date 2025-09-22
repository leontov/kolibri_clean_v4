const encoder = new TextEncoder();
const decoder = new TextDecoder();

const STORAGE_KEY = 'kolibri_ui_state_v1';
const MAX_MESSAGES = 50;

function createDefaultState() {
  return {
    messages: [],
    tailJSON: '',
    responseText: '',
  };
}

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

function storageAvailable(type) {
  try {
    const storage = window[type];
    const testKey = '__kolibri_test__';
    storage.setItem(testKey, testKey);
    storage.removeItem(testKey);
    return true;
  } catch (err) {
    console.warn('Хранилище недоступно или отключено', err);
    return false;
  }
}

async function boot() {
  const instance = await loadWasm();
  const wasm = instance.exports;
  wasm.kol_init(3, 12345);

  const msgEl = document.getElementById('msg');
  const effEl = document.getElementById('eff');
  const complEl = document.getElementById('compl');
  const outEl = document.getElementById('out');
  const responseEl = document.getElementById('response');
  const historyEl = document.getElementById('history');
  const sendBtn = document.getElementById('send');
  const tickBtn = document.getElementById('tick');
  const clearBtn = document.getElementById('clear');

  let canPersist = storageAvailable('localStorage');
  let state = createDefaultState();

  if (canPersist) {
    state = loadState();
  }
  applyStateToDOM();

  sendBtn.addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    state.messages.push(txt);
    if (state.messages.length > MAX_MESSAGES) {
      state.messages.splice(0, state.messages.length - MAX_MESSAGES);
    }
    renderMessages();
    persistState();
    refreshHud();
    refreshTail();
    refreshResponse();
  });

  tickBtn.addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();
    refreshResponse();
  });

  clearBtn.addEventListener('click', () => {
    state = createDefaultState();
    applyStateToDOM();
    if (canPersist) {
      try {
        localStorage.removeItem(STORAGE_KEY);
      } catch (err) {
        console.warn('Не удалось очистить localStorage', err);
        canPersist = false;
      }
    }
  });

  function loadState() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        return createDefaultState();
      }
      const parsed = JSON.parse(raw);
      return {
        messages: Array.isArray(parsed.messages)
          ? parsed.messages.slice(-MAX_MESSAGES)
          : [],
        tailJSON: typeof parsed.tailJSON === 'string' ? parsed.tailJSON : '',
        responseText:
          typeof parsed.responseText === 'string' ? parsed.responseText : '',
      };
    } catch (err) {
      console.warn('Не удалось восстановить состояние UI', err);
      return createDefaultState();
    }
  }

  function persistState() {
    if (!canPersist) {
      return;
    }
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch (err) {
      console.warn('Не удалось сохранить состояние UI', err);
      canPersist = false;
    }
  }

  function renderMessages() {
    historyEl.innerHTML = '';
    if (!state.messages.length) {
      return;
    }
    const fragment = document.createDocumentFragment();
    state.messages.forEach((message) => {
      const li = document.createElement('li');
      li.textContent = message;
      fragment.appendChild(li);
    });
    historyEl.appendChild(fragment);
  }

  function applyStateToDOM() {
    outEl.textContent = state.tailJSON || '';
    responseEl.textContent = state.responseText || '';
    renderMessages();
  }

  function refreshHud() {
    effEl.textContent = wasm.kol_eff().toFixed(4);
    complEl.textContent = wasm.kol_compl().toFixed(2);
  }

  function refreshTail() {
    const cap = 8192;
    const ptr = wasm.kol_alloc(cap);
    if (!ptr) {
      return;
    }
    try {
      const len = wasm.kol_tail_json(ptr, cap, 10);
      if (len >= 0) {
        const json = readString(instance, ptr, len > 0 ? len : 0);
        outEl.textContent = json;
        if (state.tailJSON !== json) {
          state.tailJSON = json;
          persistState();
        }
      }
    } finally {
      wasm.kol_free(ptr);
    }
  }

  function refreshResponse() {
    const cap = 4096;
    const ptr = wasm.kol_alloc(cap);
    if (!ptr) {
      return;
    }
    try {
      const len = wasm.kol_emit_text(ptr, cap);
      if (len >= 0) {
        const text = readString(instance, ptr, len > 0 ? len : 0);
        responseEl.textContent = text;
        if (state.responseText !== text) {
          state.responseText = text;
          persistState();
        }
      }
    } finally {
      wasm.kol_free(ptr);
    }
  }

  refreshHud();
  refreshTail();
  refreshResponse();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
