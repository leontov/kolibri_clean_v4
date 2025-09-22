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

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

async function boot() {
  const instance = await loadWasm();
  const wasm = instance.exports;
  wasm.kol_init(3, 12345);

  const msgEl = document.getElementById('msg');
  const effEl = document.getElementById('eff');
  const complEl = document.getElementById('compl');
  const outEl = document.ge
  const chartCanvas = document.getElementById('metric-chart');
  const chartCtx = chartCanvas ? chartCanvas.getContext('2d') : null;

  const effHistory = [];
  const complHistory = [];
  const MAX_HISTORY = 100;

  const storyEl = document.getElementById('story');
  const memoryEl = document.getElementById('memory');


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

    refreshInsights();

    refreshLanguageSummary();


  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();

    refreshInsights();

    refreshLanguageSummary();

  });

  function refreshHud() {
    const eff = wasm.kol_eff();
    const compl = wasm.kol_compl();

    effEl.textContent = eff.toFixed(4);
    complEl.textContent = compl.toFixed(2);

    effHistory.push(eff);
    complHistory.push(compl);
    if (effHistory.length > MAX_HISTORY) {
      effHistory.shift();
      complHistory.shift();
    }

    renderHistoryChart();
  }

  function renderHistoryChart() {
    if (!chartCtx) {
      return;
    }

    const len = effHistory.length;
    chartCtx.clearRect(0, 0, chartCanvas.width, chartCanvas.height);
    if (!len) {
      return;
    }

    const padding = 10;
    const usableWidth = chartCanvas.width - padding * 2;
    const usableHeight = chartCanvas.height - padding * 2;
    const points = effHistory.map((value, index) => ({
      x: padding + (len === 1 ? usableWidth / 2 : (usableWidth * index) / (len - 1)),
      value,
      compl: complHistory[index],
    }));

    const combined = effHistory.concat(complHistory);
    const maxValue = combined.length ? Math.max(...combined) : 1;
    const minValue = combined.length ? Math.min(...combined) : 0;
    const range = Math.max(maxValue - minValue, 1e-6);

    chartCtx.lineWidth = 2;

    const drawLine = (key, color) => {
      chartCtx.beginPath();
      chartCtx.strokeStyle = color;
      points.forEach((pt, idx) => {
        const value = key === 'eff' ? pt.value : pt.compl;
        const y =
          padding + usableHeight - ((value - minValue) / range) * usableHeight;
        if (idx === 0) {
          chartCtx.moveTo(pt.x, y);
        } else {
          chartCtx.lineTo(pt.x, y);
        }
      });
      chartCtx.stroke();
    };

    drawLine('eff', '#3b82f6');
    drawLine('compl', '#10b981');
  }

  function refreshTail() {
    const cap = 8192;
    const ptr = wasm.kol_alloc(cap);
    const len = wasm.kol_tail_json(ptr, cap, 10);
    const json = readString(instance, ptr, len > 0 ? len : 0);
    wasm.kol_free(ptr);
    let blocks;
    try {
      const parsed = json ? JSON.parse(json) : [];
      if (!Array.isArray(parsed)) {
        throw new Error('Unexpected format');
      }
      blocks = parsed.filter((item) => item && typeof item === 'object');
    } catch (error) {
      console.error('Failed to parse kol_tail_json output', error);
      outEl.innerHTML =
        '<div class="chain-message error">Не удалось прочитать цепочку данных.</div>';
      return;
    }

    if (blocks.length === 0) {
      outEl.innerHTML =
        '<div class="chain-message">Цепочка пуста — выполните Tick или отправьте сообщение.</div>';
      return;
    }

    const markup = blocks
      .map((block) => {
        const step =
          typeof block.step === 'number' && Number.isFinite(block.step)
            ? block.step
            : '-';
        const digit =
          typeof block.digit === 'number' && Number.isFinite(block.digit)
            ? block.digit
            : '-';
        const eff =
          typeof block.eff === 'number' && Number.isFinite(block.eff)
            ? block.eff.toFixed(4)
            : block.eff ?? '-';
        const compl =
          typeof block.compl === 'number' && Number.isFinite(block.compl)
            ? block.compl.toFixed(2)
            : block.compl ?? '-';
        const tsNumeric = Number(block.ts);
        const tsHuman = Number.isFinite(tsNumeric)
          ? new Date(tsNumeric * 1000).toLocaleString()
          : '-';
        const formulaValue = block.formula ?? '';
        const formula =
          typeof formulaValue === 'string'
            ? formulaValue
            : String(formulaValue);
        const hashValue = block.hash ?? '';
        const hash =
          typeof hashValue === 'string' ? hashValue : String(hashValue);
        const prevValue = block.prev ?? '';
        const prev =
          typeof prevValue === 'string' ? prevValue : String(prevValue);
        const hashShort = hash ? `${hash.slice(0, 8)}…` : '—';
        const prevShort = prev ? `${prev.slice(0, 8)}…` : '—';
        const tsRaw =
          typeof block.ts === 'number' || typeof block.ts === 'string'
            ? block.ts
            : '-';

        return `
          <article class="chain-card">
            <header>
              <span class="chain-badge">Шаг ${escapeHtml(step)}</span>
              <span class="chain-badge digit">Цифра ${escapeHtml(digit)}</span>
            </header>
            <div class="chain-body">
              <div class="chain-field">
                <span class="chain-label">Формула</span>
                <span class="chain-value">${escapeHtml(formula)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">Эффективность</span>
                <span class="chain-value metric">${escapeHtml(eff)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">Сложность</span>
                <span class="chain-value metric">${escapeHtml(compl)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">Время</span>
                <span class="chain-value">${escapeHtml(tsHuman)}</span>
              </div>
            </div>
            <div class="chain-footer">
              <div class="chain-field">
                <span class="chain-label">Хэш</span>
                <span class="chain-value">${escapeHtml(hash)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">Предыдущий</span>
                <span class="chain-value">${escapeHtml(prev)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">Кратко</span>
                <span class="chain-value">${escapeHtml(hashShort)} · ${escapeHtml(prevShort)}</span>
              </div>
              <div class="chain-field">
                <span class="chain-label">TS (raw)</span>
                <span class="chain-value">${escapeHtml(tsRaw)}</span>
              </div>
            </div>
          </article>
        `;
      })
      .join('');

    outEl.innerHTML = markup;
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

  refreshLanguageSummary();


  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
