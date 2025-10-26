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

  const outEl = document.getElementById('out');
  const reasoningLogEl = document.getElementById('reasoning-log');
  const reasoningSourcesEl = document.getElementById('reasoning-sources');
  const tabButtons = document.querySelectorAll('.tab');
  const tabViews = document.querySelectorAll('.tab-view');

  tabButtons.forEach((button) => {
    button.addEventListener('click', () => {
      const target = button.dataset.tab;
      tabButtons.forEach((btn) => btn.classList.toggle('active', btn === button));
      tabViews.forEach((view) => {
        const match = view.dataset.view === target;
        view.classList.toggle('hidden', !match);
      });
    });
  });



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

    refreshAnswer();


    refreshInsights();

    refreshLanguageSummary();



  });

  document.getElementById('tick').addEventListener('click', () => {
    wasm.kol_tick();
    refreshHud();
    refreshTail();

    refreshAnswer();


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
    outEl.textContent = json;
    let data = null;
    try {
      data = JSON.parse(json);
    } catch (err) {
      data = null;
    }
    updateXai(data);
  }

  function updateXai(data) {
    reasoningLogEl.innerHTML = '';
    reasoningSourcesEl.innerHTML = '';
    const steps = extractSteps(data);
    if (steps.length === 0) {
      const li = document.createElement('li');
      li.textContent = 'Нет данных рассуждений.';
      reasoningLogEl.appendChild(li);
    } else {
      steps.forEach((step) => {
        const li = document.createElement('li');
        const confidence = typeof step.confidence === 'number' ? step.confidence.toFixed(2) : '—';
        li.textContent = `${step.name || 'step'}: ${step.message || ''} (c=${confidence})`;
        reasoningLogEl.appendChild(li);
      });
    }

    const sources = extractSources(data);
    if (sources.length === 0) {
      const li = document.createElement('li');
      li.textContent = 'Источники не найдены.';
      reasoningSourcesEl.appendChild(li);
    } else {
      sources.forEach((source) => {
        const li = document.createElement('li');
        if (/^https?:\/\//i.test(source)) {
          const link = document.createElement('a');
          link.href = source;
          link.textContent = source;
          link.target = '_blank';
          link.rel = 'noopener noreferrer';
          li.appendChild(link);
        } else {
          li.textContent = source;
        }
        reasoningSourcesEl.appendChild(li);
      });
    }
  }

  function extractSteps(data) {
    if (!data) return [];
    if (Array.isArray(data)) {
      return [];
    }
    if (data.reasoning && Array.isArray(data.reasoning.steps)) {
      return data.reasoning.steps;
    }
    if (Array.isArray(data.steps)) {
      return data.steps;
    }
    if (data.timeline && Array.isArray(data.timeline.steps)) {
      return data.timeline.steps;
    }
    return [];
  }

  function extractSources(data) {
    const bucket = new Set();
    if (!data) {
      return [];
    }
    const proofs = Array.isArray(data.proofs) ? data.proofs : [];
    proofs.forEach((proof) => {
      if (proof && Array.isArray(proof.sources)) {
        proof.sources.forEach((source) => {
          if (source) bucket.add(String(source));
        });
      }
    });
    const answerSupport = data.answer && Array.isArray(data.answer.support) ? data.answer.support : [];
    answerSupport.forEach((fact) => {
      if (fact && Array.isArray(fact.sources)) {
        fact.sources.forEach((source) => {
          if (source) bucket.add(String(source));
        });
      }
    });
    return Array.from(bucket.values()).sort();
  }

  refreshHud();
  refreshTail();
  refreshAnswer();

  refreshInsights();

  refreshLanguageSummary();


  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
