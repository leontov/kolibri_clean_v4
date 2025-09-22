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
    updateSlo(json);
  }

  refreshHud();
  refreshTail();

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();

function updateSlo(json) {
  let events = [];
  try {
    events = JSON.parse(json);
  } catch (_) {
    // ignore malformed json from wasm tail
  }
  const sloEl = document.getElementById('slo');
  if (!sloEl) return;
  if (!Array.isArray(events)) {
    sloEl.textContent = 'нет данных';
    return;
  }
  const latest = [...events].reverse().find((entry) => entry && entry.event === 'slo_report');
  if (!latest || !latest.payload) {
    sloEl.textContent = 'нет данных';
    return;
  }
  let report = latest.payload;
  if (typeof report === 'string') {
    try {
      report = JSON.parse(report);
    } catch (_) {
      // keep as string
    }
  }
  if (report && report.json && typeof report.json === 'string') {
    try {
      report = JSON.parse(report.json);
    } catch (_) {
      // fallback to payload
    }
  }
  if (!report || typeof report !== 'object') {
    sloEl.textContent = 'нет данных';
    return;
  }
  const stages = report.stages || {};
  const lines = [];
  Object.keys(stages)
    .sort()
    .forEach((stage) => {
      const snapshot = stages[stage] || {};
      const p95 = Number(snapshot.p95 || 0).toFixed(1);
      const count = Math.trunc(Number(snapshot.count || 0));
      lines.push(`${stage}: p95=${p95}мс, n=${count}`);
    });
  const breaches = report.breaches || {};
  const breachKeys = Object.keys(breaches);
  if (breachKeys.length) {
    lines.push('⚠️ Нарушения SLA:');
    breachKeys
      .sort()
      .forEach((stage) => {
        const info = breaches[stage] || {};
        const actual = Number(info.p95 || 0).toFixed(1);
        const limit = Number(info.limit || 0).toFixed(1);
        lines.push(`- ${stage}: ${actual}мс > ${limit}мс`);
      });
  }
  sloEl.textContent = lines.length ? lines.join('\n') : 'нет данных';
}
