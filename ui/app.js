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
  const chartCanvas = document.getElementById('metric-chart');
  const chartCtx = chartCanvas ? chartCanvas.getContext('2d') : null;

  const effHistory = [];
  const complHistory = [];
  const MAX_HISTORY = 100;

  document.getElementById('send').addEventListener('click', () => {
    const txt = msgEl.value.trim();
    if (!txt) return;
    const ptr = writeString(instance, txt);
    wasm.kol_chat_push(ptr);
    wasm.kol_free(ptr);
    msgEl.value = '';
    refreshHud();
  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();
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
    outEl.textContent = json;
  }

  refreshHud();
  refreshTail();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
