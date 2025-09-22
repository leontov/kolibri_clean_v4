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

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./pwa/sw.js').catch(() => {});
  }
}

boot();
