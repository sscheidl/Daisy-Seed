// Acid Seq panel pages of the browser editor (ARBEITSAUFTRAG Phase 5): pattern grid with
// note names and play position, transport and switch states, clock and tempo, parameter
// pages built from the device's descriptors (labels from sq_contract.js), calibration
// with the separate CV enable (stored per kit UID in this browser), product patch load and
// save with an explicit upload, error counters. Works with a live SqProtocol.Client or
// with OfflineClient (demo and patch editing without a device).
(function (root, factory) {
  const api = factory(root.SqProtocol, root.SqContract);
  root.SqPanel = api;
})(typeof self !== 'undefined' ? self : this, function (S, CONTRACT) {
  'use strict';

  const TABS = [['step-seq', 'Sequencer'], ['acid-303', 'Stimme'], ['drive', 'Drive'], ['stereo-delay', 'Delay']];
  const MIDI_KEYS = ['seq_channel', 'voice_channel', 'usb_out', 'din_out', 'velocity', 'accent_velocity', 'clock_out'];
  const STATS = ['Rahmenlänge', 'Prüfsumme', 'Version', 'unbekannter Typ', 'zu lang', 'Statusbyte im Rahmen', 'Wiederholung',
    'Echo', 'beschäftigt', 'Monitor verworfen', 'Note-On verworfen', 'MIDI-Queue übergelaufen', 'Clock ignoriert',
    'Callback-Frist überschritten', 'Callback-Spitze (Zyklen)', 'Queue-Hochwasser', 'Stack frei (Byte)'];
  const el = (tag, attrs = {}, ...children) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') e.className = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (v !== false && v !== undefined) e.setAttribute(k, v === true ? '' : v);
    }
    children.flat().forEach((c) => e.append(c instanceof Node ? c : document.createTextNode(String(c))));
    return e;
  };
  const contractParam = (component, key) => {
    const c = CONTRACT[component];
    return c && c.parameters.find((p) => p.key === key);
  };
  const label = (component, key) => (contractParam(component, key) || {}).label || key;
  const format = (p, v) => {
    if (p.type === 2) {
      const cp = contractParam(p.component, p.key);
      return (cp && cp.enum_labels && cp.enum_labels[v]) || (p.enumKeys && p.enumKeys[v]) || String(v);
    }
    if (p.type === 3) return v ? 'an' : 'aus';
    const digits = p.type === 1 ? 0 : Math.abs(p.max - p.min) > 100 ? 0 : 1;
    return v.toFixed(digits) + (S.UNIT_TEXT[p.unit] ? ' ' + S.UNIT_TEXT[p.unit] : '');
  };

  // A device stand-in for the demo and for offline patch editing: descriptors from the
  // contract, edits change only this model.
  class OfflineClient {
    constructor(product = 'acid-seq') {
      this.offline = true;
      this.identity = { product, version: 'offline', uid: [0, 0, 0], capabilities: 63, bootId: 0 };
      const types = ['float', 'int', 'enum', 'bool'], units = ['none', 'percent', 'hz', 'ms', 'db', 'semitones', 'cents', 'bpm', 'channel', 'velocity'];
      this.components = Object.entries(CONTRACT).map(([id, c]) => ({ id, version: '0.1.0', schema: c.schema,
        parameters: c.parameters.map((p) => ({ id: p.id, key: p.key, type: types.indexOf(p.type), unit: units.indexOf(p.unit),
          flags: ((p.flags || []).includes('readonly') ? 1 : 0) | ((p.flags || []).includes('log') ? 2 : 0), min: p.min, max: p.max,
          def: p.default, cc: p.cc === null ? 127 : p.cc, enumKeys: p.enum || [], value: p.default })) }));
      this.pattern = Array.from({ length: 16 }, (_, i) => ({ note: 24 + (i * 5) % 12, gateLength: 50, gate: i % 4 !== 3, accent: i % 3 === 0, slide: i % 5 === 2 }));
      this.state = { state: 0, mode: 0, sw17: 0, sw18: 1, source: 0, step: -1, gate: false, bpm: 120, accepted: 0, applied: 0, cvEnabled: false };
      this.cal = [{ valid: false }, { valid: false }];
      this.stats = [];
      this.onChange = () => {};
    }
    component(id) { return this.components.find((c) => c.id === id); }
    componentIndex(id) { return this.components.findIndex((c) => c.id === id); }
    async setParam(component, key, value) {
      const p = this.component(component).parameters.find((q) => q.key === key);
      const v = Math.min(p.max, Math.max(p.min, p.type === 0 ? value : Math.round(value)));
      p.value = v;
      this.onChange();
      return v;
    }
    async setStep(i, s) { this.pattern[i] = { ...s }; this.onChange(); }
    async setPattern(steps) { this.pattern = steps.map((s) => ({ ...s })); this.onChange(); }
    async transport() { throw new Error('offline'); }
    async setCal() { throw new Error('offline'); }
    async cvEnable() { throw new Error('offline'); }
    async readStats() { return []; }
  }

  function mount(container, { log = () => {}, onClose = () => {} } = {}) {
    let client = null, tab = 'step-seq', selected = 0, loadedPatch = null;
    const css = el('style', {}, `
      .sq { display: grid; gap: 16px; }
      .sq .bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
      .sq .steps { display: grid; grid-template-columns: repeat(8, minmax(64px, 1fr)); gap: 6px; }
      .sq .step { border: 1px solid var(--line); border-radius: 8px; padding: 6px; text-align: center; cursor: pointer; background: var(--bg); }
      .sq .step.off { opacity: .45; }
      .sq .step.sel { outline: 2px solid var(--sel); }
      .sq .step.play { background: rgba(var(--pos), .25); }
      .sq .step .n { font-weight: 600; font-size: 15px; }
      .sq .step .f { font-size: 11px; color: var(--muted); letter-spacing: .05em; }
      .sq .params { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 8px 16px; }
      .sq .param { display: grid; grid-template-columns: 120px 1fr 80px; gap: 6px; align-items: center; }
      .sq .param .v { font-variant-numeric: tabular-nums; text-align: right; font-size: 12px; }
      .sq .tabs button.on { background: var(--accent); color: #fff; border-color: var(--accent); }
      .sq table.stats td { padding: 1px 8px; font-size: 12px; }
      .sq .cal { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px; }
      .sq .cal label { display: grid; grid-template-columns: 150px 100px; gap: 6px; margin: 2px 0; }`);
    const status = el('div', { class: 'bar', id: 'sqStatus' });
    const steps = el('div', { class: 'steps', id: 'sqSteps' });
    const stepEditor = el('div', { class: 'bar', id: 'sqStepEditor' });
    const tabs = el('div', { class: 'bar tabs', id: 'sqTabs' });
    const params = el('div', { class: 'params', id: 'sqParams' });
    const cal = el('div', { class: 'cal', id: 'sqCal' });
    const patchInfo = el('div', { class: 'hint', id: 'sqPatchInfo' });
    const stats = el('table', { class: 'stats', id: 'sqStats' });
    const fileInput = el('input', { type: 'file', accept: '.acidpatch,.json', hidden: true, id: 'sqFile' });
    const wrap = (title, ...body) => el('section', { class: 'card' }, el('h2', {}, title), ...body);
    const run = (promise, ok) => Promise.resolve(promise).then(() => ok && log(ok, 'ok')).catch((e) => log(e.message, 'err'));

    const root = el('div', { class: 'sq' }, css,
      wrap('Acid Seq', status),
      wrap('Pattern', steps, stepEditor),
      wrap('Parameter', tabs, params),
      wrap('Kalibrierung und CV-Freigabe', cal),
      wrap('Patch', el('div', { class: 'bar' },
        el('button', { id: 'sqSave', onclick: savePatch }, 'Patch speichern …'),
        el('button', { id: 'sqLoad', onclick: () => fileInput.click() }, 'Patch laden …'), fileInput,
        el('button', { id: 'sqUpload', onclick: uploadPatch, disabled: true }, 'Auf Gerät übertragen')), patchInfo),
      wrap('Diagnose', el('div', { class: 'bar' }, el('button', { id: 'sqStatsBtn', onclick: readStats }, 'Zähler lesen')), stats));
    container.append(root);
    fileInput.addEventListener('change', async () => {
      const f = fileInput.files[0];
      fileInput.value = '';
      if (f) loadPatch(await f.text());
    });

    function renderStatus() {
      const s = client.state, id = client.identity;
      const seq = client.component('step-seq');
      const pill = (text, kind = '') => el('span', { class: 'pill ' + kind }, text);
      status.replaceChildren(
        pill(`${id.product} ${id.version}`, client.offline ? 'warn' : 'ok'),
        ...(seq ? [
          pill(S.STATE_NAMES[s.state] || '?', s.state === 2 ? 'ok' : ''),
          pill('SW17 ' + (s.sw17 ? 'Run' : 'Stop')),
          pill('Modus ' + (['Vorwärts', 'Rückwärts', 'Zufall'][s.mode] || '?')),
          pill('Quelle ' + (S.SOURCE_NAMES[s.source] || '?')),
          pill(s.bpm.toFixed(1) + ' BPM'),
          pill(`Revision ${s.applied}/${s.accepted}`, s.applied === s.accepted ? '' : 'warn'),
          pill('CV ' + (s.cvEnabled ? 'frei' : 'gesperrt'), s.cvEnabled ? 'warn' : ''),
          el('button', { id: 'sqStart', onclick: () => run(client.transport(true)), disabled: client.offline }, 'Start'),
          el('button', { id: 'sqStop', onclick: () => run(client.transport(false)), disabled: client.offline }, 'Stopp'),
        ] : []),
        el('button', { id: 'sqPull', onclick: () => run(client.readAll && client.readAll(), 'Vom Gerät gelesen'), disabled: client.offline }, 'Vom Gerät lesen'),
        ...(client.offline ? [el('button', { id: 'sqClose', onclick: onClose }, 'Offline-Ansicht schließen')] : []));
    }
    function renderSteps() {
      if (!client.component('step-seq')) { steps.replaceChildren(el('div', { class: 'hint' }, 'Dieses Gerät hat keinen Sequencer.')); stepEditor.replaceChildren(); return; }
      steps.replaceChildren(...client.pattern.map((s, i) => el('div', {
        class: 'step' + (s.gate ? '' : ' off') + (i === selected ? ' sel' : '') + (i === client.state.step ? ' play' : ''),
        'data-step': i, onclick: () => { selected = i; renderSteps(); },
      }, el('div', { class: 'f' }, (i < 8 ? 'A' : 'B') + (i % 8 + 1)), el('div', { class: 'n' }, S.noteName(s.note)),
      el('div', { class: 'f' }, `${s.gateLength} % ${s.accent ? 'A' : '·'}${s.slide ? 'S' : '·'}`))));
      const s = client.pattern[selected];
      const send = (change) => run(client.setStep(selected, { ...client.pattern[selected], ...change }));
      const note = el('select', { id: 'sqNote', onchange: (e) => send({ note: +e.target.value }) },
        ...Array.from({ length: 60 }, (_, n) => el('option', { value: n, selected: n === s.note }, S.noteName(n))));
      const length = el('input', { type: 'range', id: 'sqGate', min: 5, max: 100, value: s.gateLength, onchange: (e) => send({ gateLength: +e.target.value }) });
      const box = (id, text, key) => el('label', {}, el('input', { type: 'checkbox', id, checked: s[key], onchange: (e) => send({ [key]: e.target.checked }) }), ' ' + text);
      stepEditor.replaceChildren(el('b', {}, `Step ${selected < 8 ? 'A' : 'B'}${selected % 8 + 1}`), note,
        el('label', {}, 'Gate-Länge ', length, ` ${s.gateLength} %`), box('sqGateOn', 'Gate', 'gate'), box('sqAccent', 'Accent', 'accent'), box('sqSlide', 'Slide', 'slide'));
    }
    function renderParams() {
      const present = TABS.filter(([id]) => client.component(id));
      if (!present.some(([id]) => id === tab) && present.length) tab = present[0][0];
      tabs.replaceChildren(...present.map(([id, name]) => el('button', { class: id === tab ? 'on' : '', 'data-tab': id, onclick: () => { tab = id; renderParams(); } }, name)),
        ...(client.component('step-seq') ? [el('button', { class: tab === 'midi' ? 'on' : '', 'data-tab': 'midi', onclick: () => { tab = 'midi'; renderParams(); } }, 'MIDI')] : []));
      const component = tab === 'midi' ? 'step-seq' : tab;
      const comp = client.component(component);
      if (!comp) { params.replaceChildren(); return; }
      const list = comp.parameters.filter((p) => (tab === 'midi') === MIDI_KEYS.includes(p.key) || component !== 'step-seq');
      params.replaceChildren(...list.map((p) => {
        p.component = component;
        const value = el('span', { class: 'v' }, format(p, p.value));
        const set = (v) => run(client.setParam(component, p.key, v).then((applied) => { value.textContent = format(p, applied); }));
        let input;
        if (p.type === 2) {
          input = el('select', { 'data-key': p.key, disabled: !!(p.flags & 1), onchange: (e) => set(+e.target.value) },
            ...Array.from({ length: p.max + 1 }, (_, v) => el('option', { value: v, selected: v === Math.round(p.value) }, format(p, v))));
        } else if (p.type === 3) {
          input = el('input', { type: 'checkbox', 'data-key': p.key, checked: p.value >= 0.5, onchange: (e) => set(e.target.checked ? 1 : 0) });
        } else {
          const log2 = p.flags & 2;
          const toPos = (v) => (log2 ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min)) * 1000;
          const fromPos = (x) => (log2 ? p.min * Math.pow(p.max / p.min, x / 1000) : p.min + (p.max - p.min) * x / 1000);
          input = el('input', { type: 'range', 'data-key': p.key, min: 0, max: 1000, value: Math.round(toPos(p.value)),
            oninput: (e) => { value.textContent = format(p, fromPos(+e.target.value)); },
            onchange: (e) => set(p.type === 1 ? Math.round(fromPos(+e.target.value)) : fromPos(+e.target.value)) });
        }
        return el('div', { class: 'param', title: `${component}.${p.key}${p.cc !== 127 ? ' · CC ' + p.cc : ''}` }, el('span', {}, label(component, p.key)), input, value);
      }));
    }
    function storedCal() {
      try { return JSON.parse(localStorage.getItem('acid-seq-cal-' + client.identity.uid.join('-')) || 'null'); } catch (e) { return null; }
    }
    function renderCal() {
      if (!(client.identity.capabilities & 32)) { cal.replaceChildren(el('div', { class: 'hint' }, 'Dieses Gerät hat keine CV-Ausgänge.')); return; }
      const fields = [['offset', 'Code bei 0 V'], ['scale', 'Codes pro Volt'], ['gateLow', 'Gate-Low-Code'], ['gateHigh', 'Gate-High-Code'], ['clampMin', 'Klemme min'], ['clampMax', 'Klemme max']];
      const outputs = [0, 1].map((o) => {
        const c = client.cal[o] || {};
        const inputs = Object.fromEntries(fields.map(([k]) => [k, el('input', { type: 'number', step: k === 'scale' ? '0.1' : '1', id: `sqCal${o}${k}`,
          value: c[k] !== undefined ? c[k] : { offset: 2048, scale: -409.6, gateLow: 2048, gateHigh: 410, clampMin: 0, clampMax: 4095 }[k] })]));
        return el('div', {}, el('b', {}, o ? 'CV OUT 2 (Gate)' : 'CV OUT 1 (Tonhöhe)'), el('span', { class: 'hint' }, c.valid ? ' gültig' : ' ungültig'),
          ...fields.map(([k, t]) => el('label', {}, t, inputs[k])),
          el('button', { id: `sqCalSend${o}`, disabled: client.offline, onclick: () => {
            const v = Object.fromEntries(fields.map(([k]) => [k, +inputs[k].value]));
            // Redrawn once the device confirmed: the output then shows "gültig".
            run(client.setCal(o, v).then(renderCal), 'Kalibrierung übernommen, CV gesperrt');
          } }, 'Senden'));
      });
      const uid = client.identity.uid.map((u) => u.toString(16).toUpperCase().padStart(8, '0')).join('-');
      cal.replaceChildren(...outputs, el('div', {},
        el('div', { class: 'hint' }, `Kit-UID ${uid}. Die Freigabe wird nie gespeichert. Sie gilt nur bis zum nächsten Neustart. Vorher Diagnose H14 durchführen.`),
        el('div', { class: 'bar' },
          el('button', { id: 'sqCvOn', disabled: client.offline, onclick: () => run(client.cvEnable(true), 'CV freigegeben') }, 'CV freigeben'),
          el('button', { id: 'sqCvOff', disabled: client.offline, onclick: () => run(client.cvEnable(false), 'CV gesperrt') }, 'CV sperren'),
          el('button', { disabled: client.offline, onclick: () => { localStorage.setItem('acid-seq-cal-' + client.identity.uid.join('-'), JSON.stringify(client.cal)); log('Kalibrierung für dieses Kit gespeichert', 'ok'); } }, 'Für dieses Kit merken'),
          el('button', { disabled: client.offline || !storedCal(), onclick: async () => {
            const stored = storedCal(); // only ever sent to the kit with the same UID
            if (!stored) return;
            for (const o of [0, 1]) if (stored[o] && stored[o].valid) await client.setCal(o, stored[o]);
            log('Gespeicherte Kalibrierung gesendet, CV bleibt gesperrt', 'ok');
          } }, 'Gemerkte senden'))));
    }
    function render() {
      if (!client) return;
      renderStatus();
      renderSteps();
      if (document.activeElement && params.contains(document.activeElement)) return; // do not fight a drag
      renderParams();
    }
    function savePatch() {
      const text = S.serializePatch(S.patchFromClient(client));
      const a = el('a', { href: URL.createObjectURL(new Blob([text], { type: 'application/json' })), download: (client.identity.product || 'acid-seq') + '.acidpatch' });
      a.click();
      URL.revokeObjectURL(a.href);
      log('Patch gespeichert', 'ok');
    }
    function loadPatch(text) {
      try { loadedPatch = S.parsePatch(text); } catch (e) { return log(e.message, 'err'); }
      const compat = S.checkPatch(loadedPatch, client);
      const notes = [];
      if (compat.missing.length) notes.push('fehlt auf dem Gerät: ' + compat.missing.join(', '));
      if (compat.incompatible.length) notes.push('andere Schema-Version: ' + compat.incompatible.join(', '));
      if (compat.unknown.length) notes.push('unbekannte Parameter: ' + compat.unknown.join(', '));
      if (compat.patternSkipped) notes.push('Pattern ohne Sequencer übersprungen');
      patchInfo.textContent = `Geladen: ${loadedPatch.product}, ${loadedPatch.components.length} Komponenten. ` +
        (notes.length ? 'Übersprungen wird: ' + notes.join('; ') + '.' : 'Passt vollständig.') +
        ' Erst „Auf Gerät übertragen“ sendet ihn.';
      container.querySelector('#sqUpload').disabled = false;
      if (client.offline) uploadPatch(); // offline: the patch becomes the edited model at once
    }
    function uploadPatch() {
      if (!loadedPatch) return;
      run(S.uploadPatch(loadedPatch, client).then((n) => log(`${n} Werte übertragen`, 'ok')).then(render));
    }
    function readStats() {
      run(client.readStats().then((values) => {
        stats.replaceChildren(...values.map((v, i) => el('tr', {}, el('td', {}, STATS[i] || 'Zähler ' + i), el('td', {}, String(v)))));
      }));
    }
    return {
      root,
      attach(c) {
        client = c;
        client.onChange = render;
        selected = 0;
        render();
        renderCal();
        renderParams();
      },
      detach() { if (client) client.onChange = () => {}; client = null; },
      render,
      client: () => client,
    };
  }

  return { mount, OfflineClient, TABS };
});
