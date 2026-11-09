/* MicroSIP Modern - puente con el nucleo C++ (WebView2) */

const api = window.chrome && window.chrome.webview ? window.chrome.webview : null;
const post = (payload) => { if (api) api.postMessage(payload); else console.log('cmd', JSON.stringify(payload)); };

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => [...document.querySelectorAll(sel)];

const ui = {
  snapshot: { state: {}, history: [], contacts: [] },
  number: '',
  theme: localStorage.getItem('msip.theme') || 'system',
  callStart: 0,
  timer: null,
  lastCallKey: '',
  msgPeer: '',
  pendingAccount: 0,
  prevView: 'dialer',
};

// Idioma: preferencia guardada; si no hay, el del sistema.
const savedLang = localStorage.getItem('msip.lang');
i18n.start(savedLang || i18n.detectSystem());

/* ---------------------------------------------------------------- */
/* Tema                                                             */
/* ---------------------------------------------------------------- */
const media = window.matchMedia('(prefers-color-scheme: dark)');

function effectiveTheme() {
  return ui.theme === 'system' ? (media.matches ? 'dark' : 'light') : ui.theme;
}

function renderTheme() {
  document.documentElement.setAttribute('data-theme', effectiveTheme());
  $$('[data-theme-set]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.themeSet === ui.theme);
  });
}

function setTheme(mode) {
  ui.theme = mode;
  localStorage.setItem('msip.theme', mode);
  renderTheme();
  post({ cmd: 'theme', mode });
}

$$('[data-theme-set]').forEach((btn) => {
  btn.addEventListener('click', () => setTheme(btn.dataset.themeSet));
});
media.addEventListener('change', () => { if (ui.theme === 'system') renderTheme(); });
renderTheme();

/* ---------------------------------------------------------------- */
/* Acento de color                                                  */
/* ---------------------------------------------------------------- */
ui.accent = localStorage.getItem('msip.accent') || 'red';

const accentNames = { red: 'Rojo MicroSIP', green: 'Verde', blue: 'Azul' };

function renderAccent() {
  document.documentElement.setAttribute('data-accent', ui.accent);
  $$('.swatch[data-accent]').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.accent === ui.accent);
  });
  const label = $('#accentLabel');
  if (label) label.textContent = accentNames[ui.accent] || accentNames.red;
}

function setAccent(name) {
  if (!accentNames[name]) return;
  ui.accent = name;
  localStorage.setItem('msip.accent', name);
  renderAccent();
  post({ cmd: 'accent', name });
}

$$('.swatch[data-accent]').forEach((btn) => {
  btn.addEventListener('click', () => setAccent(btn.dataset.accent));
});
renderAccent();

/* ---------------------------------------------------------------- */
/* Navegacion                                                       */
/* ---------------------------------------------------------------- */
const views = {
  dialer: $('#view-dialer'),
  calls: $('#view-calls'),
  contacts: $('#view-contacts'),
  messages: $('#view-messages'),
  search: $('#view-search'),
  shortcuts: $('#view-shortcuts'),
  settings: $('#view-settings'),
  about: $('#view-about'),
};

function showView(name) {
  Object.entries(views).forEach(([key, el]) => el && el.classList.toggle('active', key === name));
  $$('.nav-btn').forEach((btn) => btn.classList.toggle('active', btn.dataset.view === name));
}

$$('.nav-btn').forEach((btn) => btn.addEventListener('click', () => showView(btn.dataset.view)));

/* ---------------------------------------------------------------- */
/* Cabecera: buscar y menu                                          */
/* ---------------------------------------------------------------- */
function currentView() {
  return Object.entries(views).find(([, el]) => el && el.classList.contains('active'))?.[0] || 'dialer';
}function showSearch() {
  ui.prevView = currentView() === 'search' ? ui.prevView : currentView();
  showView('search');
  const input = $('#searchInput');
  if (input) {
    input.value = '';
    input.focus();
  }
  renderSearch('');
}

function renderSearch(query) {
  const results = $('#searchResults');
  if (!results) return;
  results.innerHTML = '';

  const q = String(query || '').trim();
  const lower = q.toLowerCase();
  const matches = (value) => String(value || '').toLowerCase().includes(lower);

  if (!q) {
    results.innerHTML = '<div class="empty">Escribe un nombre, número o mensaje</div>';
    return;
  }

  const rows = [];
  const seen = new Set();

  (ui.snapshot.contacts || []).forEach((contact) => {
    if (matches(contact.name) || matches(contact.number)) {
      const key = 'c:' + contact.number;
      if (!seen.has(key)) {
        seen.add(key);
        rows.push({ kind: 'contact', name: contact.name || contact.number, sub: contact.number, number: contact.number });
      }
    }
  });

  (ui.snapshot.history || []).forEach((item) => {
    if (matches(item.name) || matches(item.number)) {
      const key = 'h:' + item.number;
      if (!seen.has(key)) {
        seen.add(key);
        rows.push({ kind: 'call', name: displayName(item.name || item.number), sub: item.number, number: item.number });
      }
    }
  });

  (ui.snapshot.messages || []).forEach((message) => {
    if (matches(message.peer) || matches(message.body)) {
      const key = 'm:' + peerKey(message.peer);
      if (!seen.has(key)) {
        seen.add(key);
        rows.push({ kind: 'message', name: displayName(message.peer), sub: message.body, peer: message.peer });
      }
    }
  });

  if (!rows.length) {
    const number = q.replace(/[^\d+*#]/g, '');
    if (number) {
      rows.push({ kind: 'dial', name: 'Llamar a ' + q, sub: 'Número no guardado', number: q });
      rows.push({ kind: 'message', name: 'Mensaje a ' + q, sub: 'Abrir conversación', peer: q });
    }
  }

  rows.slice(0, 30).forEach((item) => {
    const row = document.createElement('div');
    row.className = 'row';
    const icon = item.kind === 'message' ? '💬' : item.kind === 'call' ? '↗' : '👤';
    row.innerHTML = `
      <div class="avatar sm">${initials(item.name)}</div>
      <div class="row-main">
        <div class="row-title">${escapeHtml(item.name)}</div>
        <div class="row-sub">${escapeHtml(item.sub || '')}</div>
      </div>
      <div class="row-meta"><span class="msg-preview">${icon}</span></div>`;
    row.addEventListener('click', () => {
      if (item.kind === 'message') {
        openChat(item.peer);
      }
      else {
        post({ cmd: 'dial', number: item.number });
        showView('dialer');
      }
    });
    results.appendChild(row);
  });
}

$('#btnSearch').addEventListener('click', showSearch);
$('#searchClose').addEventListener('click', () => showView(ui.prevView || 'dialer'));
$('#searchInput').addEventListener('input', (event) => renderSearch(event.target.value));

$('#btnMenu').addEventListener('click', (event) => {
  event.stopPropagation();
  const menu = $('#headMenu');
  if (menu) menu.hidden = !menu.hidden;
});

document.addEventListener('click', (event) => {
  const menu = $('#headMenu');
  if (menu && !menu.hidden && !event.target.closest('#headMenu') && !event.target.closest('#btnMenu')) {
    menu.hidden = true;
  }
});

$$('#headMenu [data-menu]').forEach((btn) => {
  btn.addEventListener('click', () => {
    const action = btn.dataset.menu;
    const menu = $('#headMenu');
    if (menu) menu.hidden = true;
    if (action === 'quit') {
      post({ cmd: 'quit' });
      return;
    }
    if (action === 'about') {
      openAbout();
      return;
    }
    if (action === 'alwaysOnTop') {
      const settings = (ui.snapshot.state || {}).settings || {};
      post({ cmd: 'alwaysOnTop', on: !settings.alwaysOnTop });
      return;
    }
    if (action === 'openLog') {
      post({ cmd: 'openLog' });
      return;
    }
    if (action === 'website') {
      post({ cmd: 'website' });
      return;
    }
    if (action === 'exportConfig' || action === 'importConfig') {
      post({ cmd: action });
      return;
    }
    showView(action);
  });
});

/* ---------------------------------------------------------------- */
/* Acerca de                                                        */
/* ---------------------------------------------------------------- */
let aboutReturn = 'dialer';

function openAbout() {
  const from = currentView();
  aboutReturn = from === 'about' ? 'dialer' : from;
  showView('about');
}

const aboutCloseBtn = $('#aboutClose');
if (aboutCloseBtn) {
  aboutCloseBtn.addEventListener('click', () => showView(aboutReturn));
}

/* ---------------------------------------------------------------- */
/* Dialer                                                           */
/* ---------------------------------------------------------------- */
const dialNumber = $('#dialNumber');
const dialInput = $('#dialInput');
const dialHint = $('#dialHint');
const backspace = $('#backspace');

function updateDialState() {
  backspace.classList.toggle('show', !!ui.number);
  const match = findContact(ui.number);
  dialHint.textContent = match ? match.name : '';
}

function renderNumber() {
  if (dialInput && dialInput.value !== ui.number) {
    dialInput.value = ui.number;
  }
  updateDialState();
}

function findContact(number) {
  if (!number) return null;
  const clean = (v) => String(v || '').replace(/[^\d+]/g, '');
  return (ui.snapshot.contacts || []).find((c) => clean(c.number) === clean(number)) || null;
}

$('#keypad').addEventListener('click', (e) => {
  const key = e.target.closest('.key');
  if (!key) return;
  ui.number += key.dataset.key;
  renderNumber();
  post({ cmd: 'dtmf', digit: key.dataset.key });
  if (dialInput) dialInput.focus();
});

backspace.addEventListener('click', (e) => {
  e.stopPropagation();
  ui.number = ui.number.slice(0, -1);
  renderNumber();
  if (dialInput) dialInput.focus();
});

function callNumber() {
  const target = (ui.number || (dialInput ? dialInput.value : '')).trim();
  if (!target) return;
  post({ cmd: 'dial', number: target });
  ui.number = '';
  renderNumber();
}

$('#btnCall').addEventListener('click', callNumber);

if (dialInput) {
  dialInput.addEventListener('input', () => {
    const cleaned = dialInput.value.replace(/[^\d+*#]/g, '');
    if (dialInput.value !== cleaned) {
      dialInput.value = cleaned;
    }
    // Tono local por cada tecla nueva, como la app original.
    if (cleaned.length > ui.number.length && cleaned.startsWith(ui.number)) {
      for (const ch of cleaned.slice(ui.number.length)) {
        if (/^[0-9*#]$/.test(ch)) post({ cmd: 'dtmf', digit: ch });
      }
    }
    ui.number = cleaned;
    updateDialState();
  });

  dialInput.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      callNumber();
    }
    else if (event.key === 'Escape') {
      event.preventDefault();
      ui.number = '';
      renderNumber();
    }
  });
}

/* Teclado fisico: numeros, *, #, borrar, Enter para llamar y Escape para limpiar. */
document.addEventListener('keydown', (event) => {
  // Atajos globales de la app (como en MicroSIP: Ctrl+P/S/Q/W).
  if (event.ctrlKey && !event.altKey && !event.metaKey) {
    const key = event.key.toLowerCase();
    if (key === 'p') { event.preventDefault(); showView('settings'); return; }
    if (key === 's') { event.preventDefault(); showView('shortcuts'); return; }
    if (key === 'q') { event.preventDefault(); post({ cmd: 'quit' }); return; }
    if (key === 'w') { event.preventDefault(); post({ cmd: 'website' }); return; }
  }

  const target = event.target;
  const typing = target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA'
    || target.tagName === 'SELECT' || target.isContentEditable);
  if (typing) return;

  // Durante una llamada, los digitos envian DTMF.
  if ($('#view-incall').classList.contains('active')) {
    if (/^[0-9*#]$/.test(event.key)) {
      event.preventDefault();
      post({ cmd: 'dtmf', digit: event.key });
    }
    return;
  }

  if (currentView() !== 'dialer') return;

  if (/^[0-9*#+]$/.test(event.key)) {
    event.preventDefault();
    ui.number += event.key;
    renderNumber();
    if (/^[0-9*#]$/.test(event.key)) post({ cmd: 'dtmf', digit: event.key });
    if (dialInput) dialInput.focus();
    return;
  }
  if (event.key === 'Backspace') {
    event.preventDefault();
    ui.number = ui.number.slice(0, -1);
    renderNumber();
    if (dialInput) dialInput.focus();
    return;
  }
  if (event.key === 'Enter') {
    event.preventDefault();
    callNumber();
    return;
  }
  if (event.key === 'Escape') {
    ui.number = '';
    renderNumber();
    if (dialInput) dialInput.focus();
  }
});

/* Menu contextual del numero: copiar, cortar y pegar. */
const dialMenu = $('#dialMenu');

function hideDialMenu() {
  if (dialMenu) dialMenu.hidden = true;
}

function showDialMenu(x, y) {
  if (!dialMenu) return;
  dialMenu.hidden = false;
  const rect = dialMenu.getBoundingClientRect();
  dialMenu.style.left = Math.min(x, window.innerWidth - rect.width - 8) + 'px';
  dialMenu.style.top = Math.min(y, window.innerHeight - rect.height - 8) + 'px';
}

if (dialNumber) {
  dialNumber.addEventListener('contextmenu', (event) => {
    event.preventDefault();
    showDialMenu(event.clientX, event.clientY);
  });
}

document.addEventListener('contextmenu', (event) => {
  if (!event.target.closest('#dialNumber')) {
    event.preventDefault();
    hideDialMenu();
  }
});

document.addEventListener('click', (event) => {
  if (!event.target.closest('#dialMenu') && !event.target.closest('#dialNumber')) {
    hideDialMenu();
  }
});

if (dialMenu) {
  dialMenu.addEventListener('click', (event) => {
    const button = event.target.closest('[data-clip]');
    if (!button) return;
    const action = button.dataset.clip;
    hideDialMenu();

    if (action === 'copy' || action === 'cut') {
      post({ cmd: 'copy', text: dialInput ? dialInput.value : '' });
      if (action === 'cut') {
        ui.number = '';
        renderNumber();
      }
      return;
    }

    if (action === 'paste') {
      post({ cmd: 'paste' });
    }
  });
}

/* ---------------------------------------------------------------- */
/* Acciones rapidas                                                  */
/* ---------------------------------------------------------------- */
function renderQuick() {
  const state = ui.snapshot.state || {};
  $('#chipDnd').classList.toggle('on', !!state.dnd);
  $('#chipRec').classList.toggle('on', !!state.record);
  $('#chipMute').classList.toggle('on', !!state.muted);
  $('#chipMuteLabel').textContent = state.muted ? t('Silenciado') : t('Micrófono');
  $('#micSlash').style.display = state.muted ? '' : 'none';
  $('#btnMute').classList.toggle('on', !!state.muted);
  $('#btnMuteIn').classList.toggle('muted', !!state.muted);
  $('#btnCallMuteOut').classList.toggle('on', !!state.mutedOut);
  $('#btnCallMuteOutLabel').textContent = state.mutedOut ? t('Sin audio') : t('Audio');
  $('#switchDnd').classList.toggle('on', !!state.dnd);
  const vm = $('#chipVoicemail');
  if (vm) vm.hidden = !state.voicemail;
  $('#recPill').classList.toggle('show', !!state.record);
}

$('#chipDnd').addEventListener('click', () => {
  post({ cmd: 'dnd', on: !ui.snapshot.state.dnd });
});
$('#chipVoicemail').addEventListener('click', () => post({ cmd: 'voicemail' }));
$('#switchDnd').addEventListener('click', () => {
  post({ cmd: 'dnd', on: !ui.snapshot.state.dnd });
});
$('#chipRec').addEventListener('click', () => {
  post({ cmd: 'record', on: !ui.snapshot.state.record });
});
$('#chipMute').addEventListener('click', () => {
  post({ cmd: 'mute', on: !ui.snapshot.state.muted });
});
$('#btnMute').addEventListener('click', () => {
  post({ cmd: 'mute', on: !ui.snapshot.state.muted });
});
$('#btnMuteIn').addEventListener('click', () => {
  post({ cmd: 'mute', on: !ui.snapshot.state.muted });
});
$('#btnMuteOut').addEventListener('click', () => {
  post({ cmd: 'muteOut', on: !ui.snapshot.state.mutedOut });
});
$('#btnCallMuteOut').addEventListener('click', () => {
  post({ cmd: 'muteOut', on: !ui.snapshot.state.mutedOut });
});

/* ---------------------------------------------------------------- */
/* Cabecera                                                         */
/* ---------------------------------------------------------------- */
function renderHeader() {
  const state = ui.snapshot.state || {};
  const nameEl = $('.head-meta .name');
  const statusEl = $('#headStatus');

  const account = state.account || '';
  const server = state.server || '';
  nameEl.textContent = account ? `${account}${server ? ' · ' + server : ''}` : t('Sin cuenta configurada');

  if (state.dnd) {
    statusEl.innerHTML = '<span class="presence" style="background:var(--red);box-shadow:0 0 0 3px var(--red-soft)"></span> ' + t('No molestar');
    return;
  }
  const active = (state.accounts || []).find((item) => item.index === state.activeAccount);
  if (active && active.needsPassword) {
    statusEl.innerHTML = '<span class="presence" style="background:var(--amber);box-shadow:0 0 0 3px rgba(217,119,6,.16)"></span> ' + t('Falta la contraseña');
    return;
  }
  if (state.registered) {
    statusEl.innerHTML = '<span class="presence"></span> ' + t('Registrado') + ' · ' + (state.transport || 'udp').toUpperCase();
    return;
  }
  const reason = state.regReason ? ' · ' + state.regReason : '';
  statusEl.innerHTML = '<span class="presence" style="background:var(--amber);box-shadow:0 0 0 3px rgba(217,119,6,.16)"></span> ' + t('Sin registrar') + reason;
}

/* ---------------------------------------------------------------- */
/* Llamadas e historial                                             */
/* ---------------------------------------------------------------- */
function displayName(value) {
  const raw = String(value || "");
  const match = /sip:([^@;>]+)/i.exec(raw);
  if (match) return match[1];

  const clean = raw.replace(/^</, "").replace(/>$/, "");
  if (/^[+\d][^@\s]*@/.test(clean)) {
    return clean.slice(0, clean.indexOf("@"));
  }
  return raw;
}

function initials(name) {
  const parts = String(name || '').trim().split(/\s+/).filter(Boolean);
  if (!parts.length) return '??';
  return parts.slice(0, 2).map((w) => w[0]).join('').toUpperCase();
}

function formatTime(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function formatDuration(sec) {
  if (!sec) return '';
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return m ? `${m} min` : `${s} s`;
}

function dayLabel(ts) {
  const d = new Date(ts * 1000);
  const today = new Date();
  const yesterday = new Date();
  yesterday.setDate(today.getDate() - 1);
  const same = (a, b) => a.toDateString() === b.toDateString();
  if (same(d, today)) return t('Hoy');
  if (same(d, yesterday)) return t('Ayer');
  return d.toLocaleDateString(i18n.current);
}

function historyRow(item) {
  const row = document.createElement('div');
  row.className = 'row';
  const missed = item.type === 'missed';
  const label = displayName(item.name || item.number);
  const dir = missed ? t('Perdida') : item.type === 'in' ? t('Entrante') : t('Saliente');
  row.innerHTML = `
    <div class="avatar sm">${initials(label)}</div>
    <div class="row-main">
      <div class="row-title">${escapeHtml(label)}</div>
      <div class="row-sub">${missed ? '<span class="badge-missed">' + dir + '</span>' : dir} · ${formatTime(item.time)}${item.duration ? ' · ' + formatDuration(item.duration) : ''}</div>
    </div>
    <div class="row-meta">
      <button class="row-call" title="Llamar">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="currentColor"><path d="M6.6 10.8c1.4 2.8 3.8 5.2 6.6 6.6l2.2-2.2c.3-.3.7-.4 1.1-.2 1.2.4 2.5.6 3.8.6.6 0 1 .4 1 1V20c0 .6-.4 1-1 1C10.6 21 3 13.4 3 4c0-.6.4-1 1-1h3.4c.6 0 1 .4 1 1 0 1.3.2 2.6.6 3.8.1.4 0 .8-.2 1.1L6.6 10.8Z"/></svg>
      </button>
      <button class="row-del" data-del="1" title="Eliminar del historial">×</button>
    </div>`;
  row.addEventListener('click', (event) => {
    if (event.target.closest('[data-del]')) {
      event.stopPropagation();
      if (typeof item.index === 'number') post({ cmd: 'deleteHistory', index: item.index });
      return;
    }
    post({ cmd: 'dial', number: item.number });
  });
  return row;
}

function renderHistory() {
  const today = $('#callsToday');
  const yesterday = $('#callsYesterday');
  if (!today || !yesterday) return;
  today.innerHTML = '';
  yesterday.innerHTML = '';

  const items = (ui.snapshot.history || []).slice(0, 60);
  if (!items.length) {
    today.innerHTML = '<div class="empty">' + t('Todavía no hay llamadas') + '</div>';
    return;
  }

  const groups = new Map();
  items.forEach((item) => {
    const label = dayLabel(item.time);
    if (!groups.has(label)) groups.set(label, []);
    groups.get(label).push(item);
  });

  let first = true;
  groups.forEach((list, label) => {
    const container = document.createElement('div');
    container.className = 'history-group';
    const title = document.createElement('div');
    title.className = 'list-label';
    title.textContent = label;
    container.appendChild(title);
    list.forEach((item) => container.appendChild(historyRow(item)));
    if (first) {
      today.appendChild(container);
      first = false;
    } else {
      yesterday.appendChild(container);
    }
  });
}

function renderContacts() {
  const list = $('#contactsList');
  if (!list) return;
  list.innerHTML = '';
  const contacts = ui.snapshot.contacts || [];
  if (!contacts.length) {
    list.innerHTML = '<div class="empty">' + t('Sin contactos. Usa “Nuevo contacto”.') + '</div>';
    return;
  }
  contacts.forEach((contact) => {
    const row = document.createElement('div');
    row.className = 'row';
    const status = contact.presenceStatus || 'unknown';
    const color = status === 'online' ? 'var(--green)'
      : status === 'offline' ? 'var(--text-3)' : 'var(--amber)';
    const dot = contact.presence
      ? `<span class="presence" style="background:${color};box-shadow:none"></span> ` : '';
    row.innerHTML = `
      <div class="avatar sm">${initials(contact.name || contact.number)}</div>
      <div class="row-main">
        <div class="row-title">${escapeHtml(contact.name || contact.number)}</div>
        <div class="row-sub">${dot}${escapeHtml(contact.number)}</div>
      </div>
      <div class="row-meta">
        <button class="row-call" data-msg="1" title="Enviar mensaje">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 11.5a8.4 8.4 0 0 1-9 8.4 9 9 0 0 1-4-.9L3 21l1.9-4.6a8.4 8.4 0 0 1-1-4A8.4 8.4 0 0 1 12 4a8.4 8.4 0 0 1 9 7.5Z"/></svg>
        </button>
        <button class="row-call" data-edit="1" title="Editar">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20h4l10-10-4-4L4 16v4Z"/><path d="m13 6 4 4"/></svg>
        </button>
        <button class="row-call" data-pickup="1" title="Recoger llamada">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14h4l3 3"/><path d="M20 10h-4l-3-3"/><path d="M4 10h4l3 3"/><path d="M20 14h-4l-3 3"/></svg>
        </button>
        <button class="row-call" title="Llamar">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="currentColor"><path d="M6.6 10.8c1.4 2.8 3.8 5.2 6.6 6.6l2.2-2.2c.3-.3.7-.4 1.1-.2 1.2.4 2.5.6 3.8.6.6 0 1 .4 1 1V20c0 .6-.4 1-1 1C10.6 21 3 13.4 3 4c0-.6.4-1 1-1h3.4c.6 0 1 .4 1 1 0 1.3.2 2.6.6 3.8.1.4 0 .8-.2 1.1L6.6 10.8Z"/></svg>
        </button>
      </div>`;
    row.addEventListener('click', (event) => {
      if (event.target.closest('[data-msg]')) {
        event.stopPropagation();
        openChat(contact.number);
        return;
      }
      if (event.target.closest('[data-edit]')) {
        event.stopPropagation();
        editContact(contact);
        return;
      }
      if (event.target.closest('[data-pickup]')) {
        event.stopPropagation();
        post({ cmd: 'pickup', number: contact.number });
        return;
      }
      post({ cmd: 'dial', number: contact.number });
    });
    list.appendChild(row);
  });
}

let contactEditId = 0;

function editContact(contact) {
  contactEditId = contact ? contact.id : 0;
  $('#contactName').value = contact ? contact.name : '';
  $('#contactNumber').value = contact ? contact.number : '';
  $('#contactInfo').value = contact ? contact.info : '';
  $('#contactPresence').checked = contact ? !!contact.presence : false;
  $('#contactDelete').hidden = !contact;
  $('#contactForm').hidden = false;
  $('#contactName').focus();
}

$('#contactNew').addEventListener('click', () => editContact(null));
$('#contactSave').addEventListener('click', () => {
  const number = $('#contactNumber').value.trim();
  if (!number) return;
  post({
    cmd: 'saveContact',
    id: contactEditId,
    name: $('#contactName').value.trim(),
    number,
    info: $('#contactInfo').value.trim(),
    presence: $('#contactPresence').checked,
  });
  $('#contactForm').hidden = true;
});
$('#contactDelete').addEventListener('click', () => {
  if (contactEditId > 0) post({ cmd: 'deleteContact', id: contactEditId });
  $('#contactForm').hidden = true;
});

$('#callsExport').addEventListener('click', () => post({ cmd: 'exportHistory' }));
$('#callsImport').addEventListener('click', () => post({ cmd: 'importHistory' }));
$('#callsClear').addEventListener('click', () => post({ cmd: 'clearHistory' }));
$('#contactsExport').addEventListener('click', () => post({ cmd: 'exportContacts' }));
$('#contactsImport').addEventListener('click', () => post({ cmd: 'importContacts' }));

/* ---------------------------------------------------------------- */
/* Mensajes                                                         */
/* ---------------------------------------------------------------- */
function peerKey(value) {
  const raw = String(value || '');
  const match = /sip:([^@;>]+)/i.exec(raw);
  let user = match ? match[1] : raw.replace(/^</, '').replace(/>$/, '');
  const at = user.indexOf('@');
  if (at > 0) user = user.slice(0, at);
  return user.replace(/[^\d+]/g, '');
}

function openChat(peer) {
  if (!peer) return;
  ui.msgPeer = peer;
  const input = $('#msgPeer');
  if (input) input.value = displayName(peer);
  showView('messages');
  renderMessages();
  const body = $('#msgBody');
  if (body) body.focus();
}

/* ---------------------------------------------------------------- */
/* Atajos de marcacion rapida                                       */
/* ---------------------------------------------------------------- */
function shortcutsData() {
  const state = ui.snapshot.state || {};
  return Array.isArray(state.shortcuts) ? state.shortcuts : [];
}

let shortcutEditIndex = -1;

function resetShortcutForm() {
  shortcutEditIndex = -1;
  const label = $('#shortcutLabel');
  const number = $('#shortcutNumber');
  if (label) label.value = '';
  if (number) number.value = '';
  const save = $('#shortcutSave');
  if (save) save.textContent = 'Añadir atajo';
  const cancel = $('#shortcutCancel');
  if (cancel) cancel.hidden = true;
}

function renderShortcuts() {
  const shortcuts = shortcutsData();

  const dialRow = $('#dialShortcuts');
  if (dialRow) {
    dialRow.innerHTML = '';
    dialRow.hidden = shortcuts.length === 0;
    shortcuts.forEach((shortcut) => {
      const btn = document.createElement('button');
      btn.className = 'shortcut-btn';
      btn.textContent = shortcut.label || shortcut.number;
      btn.title = shortcut.number;
      btn.addEventListener('click', () => {
        ui.number = shortcut.number;
        renderNumber();
        post({ cmd: 'dial', number: shortcut.number });
      });
      dialRow.appendChild(btn);
    });
  }

  const list = $('#shortcutsList');
  if (!list) return;
  list.innerHTML = '';
  if (!shortcuts.length) {
    list.innerHTML = '<div class="empty">' + t('Sin atajos. Añade uno arriba.') + '</div>';
    return;
  }

  shortcuts.forEach((shortcut, index) => {
    const row = document.createElement('div');
    row.className = 'row';

    const avatar = document.createElement('div');
    avatar.className = 'avatar sm';
    avatar.textContent = String(shortcut.label || shortcut.number || '?').slice(0, 2).toUpperCase();

    const main = document.createElement('div');
    main.className = 'row-main';
    const title = document.createElement('div');
    title.className = 'row-title';
    title.textContent = shortcut.label || shortcut.number;
    const sub = document.createElement('div');
    sub.className = 'row-sub';
    sub.textContent = shortcut.number;
    main.appendChild(title);
    main.appendChild(sub);

    const meta = document.createElement('div');
    meta.className = 'row-meta';

    const call = document.createElement('button');
    call.className = 'row-call';
    call.title = 'Llamar';
    call.textContent = 'Llamar';
    call.addEventListener('click', () => post({ cmd: 'dial', number: shortcut.number }));

    const edit = document.createElement('button');
    edit.className = 'row-del';
    edit.title = 'Editar';
    edit.textContent = 'Editar';
    edit.addEventListener('click', () => {
      shortcutEditIndex = index;
      $('#shortcutLabel').value = shortcut.label || '';
      $('#shortcutNumber').value = shortcut.number || '';
      $('#shortcutSave').textContent = 'Guardar atajo';
      $('#shortcutCancel').hidden = false;
      $('#shortcutLabel').focus();
    });

    const del = document.createElement('button');
    del.className = 'row-del';
    del.title = 'Eliminar';
    del.textContent = 'Eliminar';
    del.addEventListener('click', () => post({ cmd: 'deleteShortcut', index }));

    meta.appendChild(call);
    meta.appendChild(edit);
    meta.appendChild(del);

    row.appendChild(avatar);
    row.appendChild(main);
    row.appendChild(meta);
    list.appendChild(row);
  });
}

if ($('#shortcutSave')) {
  $('#shortcutSave').addEventListener('click', () => {
    const label = $('#shortcutLabel').value.trim();
    const number = $('#shortcutNumber').value.trim();
    if (!number) {
      $('#shortcutNumber').focus();
      return;
    }
    post({ cmd: 'saveShortcut', index: shortcutEditIndex, label, number });
    resetShortcutForm();
  });
}

if ($('#shortcutCancel')) {
  $('#shortcutCancel').addEventListener('click', resetShortcutForm);
}

function renderMenuState() {
  const settings = (ui.snapshot.state || {}).settings || {};
  const item = $('#menuAlwaysOnTop');
  if (item) item.classList.toggle('on', !!settings.alwaysOnTop);
}

/* ---------------------------------------------------------------- */
/* Mensajes                                                         */
/* ---------------------------------------------------------------- */
function renderMessages() {
  const list = $('#msgList');
  const chat = $('#msgChat');
  const back = $('#msgBack');
  if (!list || !chat) return;

  const messages = ui.snapshot.messages || [];
  const peer = ui.msgPeer;

  if (!peer) {
    chat.hidden = true;
    list.hidden = false;
    if (back) back.hidden = true;

    const groups = new Map();
    messages.forEach((message) => {
      const key = peerKey(message.peer) || message.peer;
      if (!groups.has(key)) {
        groups.set(key, { peer: message.peer, last: message });
      }
      groups.get(key).last = message;
    });

    list.innerHTML = '';
    if (!groups.size) {
      list.innerHTML = '<div class="empty">' + t('Todavía no hay conversaciones') + '</div>';
      return;
    }

    [...groups.values()]
      .sort((a, b) => (b.last.time || 0) - (a.last.time || 0))
      .forEach((group) => {
        const row = document.createElement('div');
        row.className = 'row';
        row.innerHTML = `
          <div class="avatar sm">${initials(group.peer)}</div>
          <div class="row-main">
            <div class="row-title">${escapeHtml(displayName(group.peer))}</div>
            <div class="msg-preview">${escapeHtml((group.last.outgoing ? t('Tú: ') : '') + group.last.body)}</div>
          </div>
          <div class="row-meta"><span class="msg-preview">${formatTime(group.last.time)}</span></div>`;
        row.addEventListener('click', () => openChat(group.peer));
        list.appendChild(row);
      });
    return;
  }

  list.hidden = true;
  chat.hidden = false;
  if (back) back.hidden = false;

  const thread = $('#msgThread');
  if (!thread) return;
  thread.innerHTML = '';

  const items = messages.filter((message) => peerKey(message.peer) === peerKey(peer));
  if (!items.length) {
    thread.innerHTML = '<div class="empty">' + t('Sin mensajes con este número') + '</div>';
  }

  items.forEach((message) => {
    const bubble = document.createElement('div');
    bubble.className = 'bubble'
      + (message.outgoing ? ' out' : '')
      + (message.status === 'failed' ? ' failed' : '');
    const status = message.outgoing
      ? ' · ' + (message.status === 'failed' ? t('no enviado') : t('enviado'))
      : '';
    bubble.innerHTML = `${escapeHtml(message.body)}<span class="meta">${formatTime(message.time)}${status}</span>`;
    thread.appendChild(bubble);
  });
  thread.scrollTop = thread.scrollHeight;
}

function sendMessage() {
  const input = $('#msgPeer');
  const bodyInput = $('#msgBody');
  if (!input || !bodyInput) return;

  const peer = ui.msgPeer || input.value.trim();
  const body = bodyInput.value.trim();
  if (!peer || !body) return;

  ui.msgPeer = peer;
  post({ cmd: 'sendMessage', to: peer, body });
  bodyInput.value = '';
  bodyInput.focus();
}

$('#msgSend').addEventListener('click', sendMessage);
$('#msgBody').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    event.preventDefault();
    sendMessage();
  }
});
$('#msgOpen').addEventListener('click', () => openChat($('#msgPeer').value.trim()));
$('#msgPeer').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    event.preventDefault();
    openChat($('#msgPeer').value.trim());
  }
});
$('#msgBack').addEventListener('click', () => {
  ui.msgPeer = '';
  $('#msgPeer').value = '';
  renderMessages();
});

/* ---------------------------------------------------------------- */
/* Llamada activa / entrante                                        */
/* ---------------------------------------------------------------- */
function currentCalls() {
  const calls = (ui.snapshot.state && ui.snapshot.state.calls) || [];
  return calls.filter((c) => c.state !== "disconnected" && c.state !== "null");
}

function activeCall() {
  const calls = currentCalls();
  return calls.find((c) => c.state !== 'incoming' && !c.held)
    || calls.find((c) => c.state !== 'incoming');
}

function renderOverlay() {
  const calls = currentCalls();
  const incoming = calls.find((c) => c.state === 'incoming');
  const active = calls.find((c) => c.state !== 'incoming' && !c.held)
    || calls.find((c) => c.state !== 'incoming');

  if (incoming) {
    showOverlay('incoming', incoming);
    return;
  }
  if (active) {
    showOverlay('incall', active);
    return;
  }
  hideOverlays();
}

function renderCallSwitch(calls, activeId) {
  const box = $('#callSwitch');
  if (!box) return;
  const confirmed = calls.filter((c) => c.state !== 'incoming');
  box.innerHTML = '';
  if (confirmed.length < 2) {
    box.hidden = true;
    return;
  }
  box.hidden = false;
  const merge = $('#btnMerge');
  if (merge) merge.hidden = confirmed.length < 2;
  confirmed.forEach((c) => {
    const btn = document.createElement('button');
    btn.className = 'call-chip' + (c.id === activeId ? ' active' : '');
    btn.textContent = displayName(c.name || c.remoteNumber || 'Llamada')
      + (c.held ? ' · ' + t('En espera') : '');
    btn.addEventListener('click', () => post({ cmd: 'swap', id: c.id }));
    box.appendChild(btn);
  });
}

function showOverlay(kind, call) {
  const incall = $('#view-incall');
  const incoming = $('#view-incoming');
  incall.classList.toggle('active', kind === 'incall');
  incoming.classList.toggle('active', kind === 'incoming');

  const name = displayName(call.name || call.remoteNumber || call.remote || "Desconocido");
  const sub = displayName(call.remoteNumber || call.remote || '');

  if (kind === 'incall') {
    $('#incallName').textContent = name;
    $('#incallNum').textContent = sub;
    $('#incallAvatar').textContent = initials(name);
    const state = ui.snapshot.state || {};
    const ringing = call.state === 'calling' || call.state === 'early' || call.state === 'connecting';
    $('#view-incall .state').textContent = ringing
      ? t('Llamando…')
      : (call.held ? t('En espera') : (state.conference ? t('Conferencia') : t('En llamada')));
    renderCallSwitch(currentCalls(), call.id);
    const pending = $('#transferPending');
    if (pending) pending.hidden = !state.transferPending;

    if (!ringing) {
      const key = call.id + ':' + call.state;
      if (!ui.callStart || ui.lastCallKey !== key) {
        ui.callStart = Date.now() - (call.duration || 0) * 1000;
        ui.lastCallKey = key;
      }
      startTimer();
    } else {
      stopTimer();
    }
  } else {
    $('#view-incoming .name').textContent = name;
    $('#view-incoming .num').textContent = sub;
    $('#view-incoming .avatar').textContent = initials(name);
  }
}

function hideOverlays() {
  $('#view-incall').classList.remove('active');
  $('#view-incoming').classList.remove('active');
  const sw = $('#callSwitch');
  if (sw) sw.hidden = true;
  const merge = $('#btnMerge');
  if (merge) merge.hidden = true;
  const pending = $('#transferPending');
  if (pending) pending.hidden = true;
  const sheet = document.getElementById('callSheet');
  if (sheet) sheet.classList.remove('open');
  stopTimer();
  ui.callStart = 0;
  ui.lastCallKey = '';
}

function startTimer() {
  if (ui.timer) return;
  const tick = () => {
    const secs = ui.callStart ? Math.max(0, Math.floor((Date.now() - ui.callStart) / 1000)) : 0;
    const m = String(Math.floor(secs / 60)).padStart(2, '0');
    const s = String(secs % 60).padStart(2, '0');
    $('#callTimer').textContent = `${m}:${s}`;
  };
  tick();
  ui.timer = setInterval(tick, 1000);
}

function stopTimer() {
  if (ui.timer) clearInterval(ui.timer);
  ui.timer = null;
  $('#callTimer').textContent = '00:00';
}

$('#btnHangup').addEventListener('click', () => {
  const call = activeCall();
  if (call) post({ cmd: 'hangup', id: call.id });
});
$('#btnAnswer').addEventListener('click', () => {
  const call = currentCalls().find((c) => c.state === 'incoming');
  if (call) post({ cmd: 'answer', id: call.id });
});
$('#btnDecline').addEventListener('click', () => {
  const call = currentCalls().find((c) => c.state === 'incoming');
  if (call) post({ cmd: 'hangup', id: call.id });
});

$('#btnHold').addEventListener('click', () => {
  const call = activeCall();
  if (call) post({ cmd: 'hold', id: call.id, on: !call.held });
});

/* Hoja de teclado DTMF / transferencia */
const callSheet = $('#callSheet');
const sheetKeypad = $('#sheetKeypad');
const sheetTransfer = $('#sheetTransfer');
const sheetTitle = $('#sheetTitle');

function openSheet(kind) {
  if (!callSheet) return;
  callSheet.classList.add('open');
  sheetKeypad.hidden = kind !== 'keypad';
  sheetTransfer.hidden = kind !== 'transfer';
  const sheetAdd = $('#sheetAdd');
  if (sheetAdd) sheetAdd.hidden = kind !== 'add';
  sheetTitle.textContent = kind === 'keypad' ? t('Teclado') : (kind === 'add' ? t('Añadir a conferencia') : t('Transferir'));
}

$('#btnKeypad').addEventListener('click', () => openSheet('keypad'));
$('#btnTransfer').addEventListener('click', () => openSheet('transfer'));
$('#btnAddCall').addEventListener('click', () => openSheet('add'));
$('#btnMerge').addEventListener('click', () => post({ cmd: 'merge' }));
$('#sheetClose').addEventListener('click', () => callSheet.classList.remove('open'));

document.querySelectorAll('[data-dtmf]').forEach((btn) => {
  btn.addEventListener('click', () => {
    post({ cmd: 'dtmf', digit: btn.dataset.dtmf });
    btn.style.background = 'var(--accent-soft)';
    setTimeout(() => { btn.style.background = ''; }, 140);
  });
});

$('#transferBlind').addEventListener('click', () => {
  const call = activeCall();
  const number = $('#transferNumber').value.trim();
  if (!call || !number) return;
  post({ cmd: 'transfer', id: call.id, number });
  $('#transferNumber').value = '';
  callSheet.classList.remove('open');
});

$('#transferAttended').addEventListener('click', () => {
  const call = activeCall();
  const number = $('#transferNumber').value.trim();
  if (!call || !number) return;
  post({ cmd: 'attendedTransfer', id: call.id, number });
  $('#transferNumber').value = '';
  callSheet.classList.remove('open');
});

$('#transferComplete').addEventListener('click', () => post({ cmd: 'completeTransfer' }));
$('#transferCancel').addEventListener('click', () => post({ cmd: 'cancelTransfer' }));

$('#addCallGo').addEventListener('click', () => {
  const number = $('#addCallNumber').value.trim();
  if (!number) return;
  post({ cmd: 'addCall', number });
  $('#addCallNumber').value = '';
  callSheet.classList.remove('open');
});

$('#btnTestRing').addEventListener('click', () => post({ cmd: 'ringTest' }));

/* ---------------------------------------------------------------- */
/* Ajustes: cuenta SIP                                              */
/* ---------------------------------------------------------------- */
function renderAccountSelect() {
  const state = ui.snapshot.state || {};
  const accounts = state.accounts || [];
  const select = $('#accountSelect');
  if (!select) return;

  const previous = select.value;
  select.innerHTML = '';

  accounts.forEach((account) => {
    const opt = document.createElement('option');
    opt.value = String(account.index);
    opt.textContent = `Account${account.index} · ${account.username || account.label || 'sin usuario'}${account.registered ? ' ✓' : ''}`;
    select.appendChild(opt);
  });

  if (ui.pendingAccount && !accounts.some((a) => a.index === ui.pendingAccount)) {
    const opt = document.createElement('option');
    opt.value = String(ui.pendingAccount);
    opt.textContent = `Account${ui.pendingAccount} · nueva`;
    select.appendChild(opt);
  }

  if (!accounts.length && !ui.pendingAccount) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = 'Sin cuentas · usa + para crear';
    select.appendChild(opt);
    select.value = '';
    return;
  }

  let target = ui.pendingAccount ? String(ui.pendingAccount) : previous;
  if (!target || ![...select.options].some((o) => o.value === target)) {
    target = String(state.activeAccount || (accounts[0] ? accounts[0].index : 1));
  }
  select.value = target;
}

function selectedAccount() {
  const state = ui.snapshot.state || {};
  const index = Number($('#accountSelect').value || state.activeAccount || 0);
  return (state.accounts || []).find((account) => account.index === index) || null;
}

function renderAccountForm() {
  renderAccountSelect();
  const account = selectedAccount();
  const set = (id, value) => {
    const el = $(id);
    if (el && document.activeElement !== el) el.value = value || '';
  };
  const setSelect = (id, value) => {
    const el = $(id);
    if (el && document.activeElement !== el) el.value = value;
  };
  const setSwitch = (id, on) => {
    const el = $(id);
    if (el) el.classList.toggle('on', !!on);
  };

  if (account) {
    set('#accServer', account.server);
    set('#accUser', account.username);
    set('#accName', account.displayName || account.label);
    set('#accDomain', account.domain);
    set('#accProxy', account.proxy);
    set('#accAuthId', account.authId);
    set('#accPrefix', account.dialingPrefix);
    set('#accDialPlan', account.dialPlan);
    set('#accVoicemail', account.voicemail);
    set('#accPublicAddr', account.publicAddr);
    set('#accRegisterRefresh', account.registerRefresh);
    set('#accKeepAlive', account.keepAlive);
    setSelect('#accHideCid', account.hideCid || '');
    setSelect('#accTransport', account.transport || 'udp');
    setSelect('#accSrtp', account.srtp || '');
    setSwitch('#accIce', account.ice);
    setSwitch('#accRewrite', account.allowRewrite);
    setSwitch('#accSessionTimer', account.disableSessionTimer);
    setSwitch('#accPublish', account.publish);
  }
  else if (!ui.pendingAccount) {
    set('#accServer', '');
    set('#accUser', '');
    set('#accName', '');
    set('#accDomain', '');
    set('#accProxy', '');
    set('#accAuthId', '');
    set('#accPrefix', '');
    set('#accDialPlan', '');
    set('#accVoicemail', '');
    set('#accPublicAddr', '');
    set('#accRegisterRefresh', 300);
    set('#accKeepAlive', 15);
    setSelect('#accHideCid', '');
    setSelect('#accTransport', 'udp');
    setSelect('#accSrtp', '');
    setSwitch('#accIce', false);
    setSwitch('#accRewrite', false);
    setSwitch('#accSessionTimer', false);
    setSwitch('#accPublish', false);
  }

  const pathEl = $('#configPath');
  if (pathEl) {
    const state = ui.snapshot.state || {};
    const path = state.configPath || '';
    const imported = state.importedFrom || '';
    pathEl.textContent = path
      ? `Config: ${path}${imported ? ' · importado de ' + imported : ''}`
      : '';
  }

  const passNote = $('#accPassNote');
  if (passNote) {
    passNote.hidden = !(account && account.needsPassword);
  }
}

$('#accountSelect').addEventListener('change', () => {
  ui.pendingAccount = 0;
  post({ cmd: 'setAccount', index: Number($('#accountSelect').value) });
});

$('#accNew').addEventListener('click', () => {
  const accounts = (ui.snapshot.state || {}).accounts || [];
  const max = accounts.reduce((value, account) => Math.max(value, account.index), 0);
  ui.pendingAccount = Math.max(max + 1, 1);
  renderAccountForm();
  ['#accServer', '#accUser', '#accPass', '#accName'].forEach((id) => {
    const el = $(id);
    if (el) el.value = '';
  });
  $('#accServer').focus();
});

$('#accImport').addEventListener('click', () => {
  post({ cmd: 'importAccounts' });
  $('#accImport').textContent = 'Importando…';
  setTimeout(() => { $('#accImport').textContent = 'Importar cuentas de MicroSIP'; }, 1800);
});

$('#accSave').addEventListener('click', () => {
  const server = $('#accServer').value.trim();
  const username = $('#accUser').value.trim();
  const password = $('#accPass').value;
  const displayName = $('#accName').value.trim();
  const index = ui.pendingAccount || Number($('#accountSelect').value) || 0;
  if (!server || !username) {
    $('#accSave').textContent = 'Faltan servidor o usuario';
    setTimeout(() => { $('#accSave').textContent = 'Guardar y registrar'; }, 1800);
    return;
  }

  const on = (id) => $(id).classList.contains('on');
  post({
    cmd: 'saveAccount',
    index,
    label: displayName || username,
    server,
    domain: $('#accDomain').value.trim(),
    username,
    password,
    authId: $('#accAuthId').value.trim(),
    displayName,
    proxy: $('#accProxy').value.trim(),
    voicemail: $('#accVoicemail').value.trim(),
    dialingPrefix: $('#accPrefix').value.trim(),
    dialPlan: $('#accDialPlan').value.trim(),
    hideCid: $('#accHideCid').value,
    srtp: $('#accSrtp').value,
    transport: $('#accTransport').value,
    publicAddr: $('#accPublicAddr').value.trim(),
    registerRefresh: Number($('#accRegisterRefresh').value) || 300,
    keepAlive: Number($('#accKeepAlive').value) || 0,
    publish: on('#accPublish'),
    ice: on('#accIce'),
    allowRewrite: on('#accRewrite'),
    disableSessionTimer: on('#accSessionTimer'),
  });
  ui.pendingAccount = 0;
  $('#accSave').textContent = 'Guardando…';
  setTimeout(() => { $('#accSave').textContent = 'Guardar y registrar'; }, 1500);
});

/* Interruptores de cuenta: se guardan con el formulario. */
$$('[data-account-switch]').forEach((btn) => {
  btn.addEventListener('click', () => btn.classList.toggle('on'));
});

/* ---------------------------------------------------------------- */
/* Ajustes: red, audio y general                                    */
/* ---------------------------------------------------------------- */
function renderAudioDevices(devices, settings) {
  const fill = (id, list, current) => {
    const el = $(id);
    if (!el) return;
    el.innerHTML = '';
    const def = document.createElement('option');
    def.value = '-1';
    def.textContent = 'Predeterminado';
    el.appendChild(def);
    list.forEach((device) => {
      const option = document.createElement('option');
      option.value = String(device.id);
      option.textContent = device.name;
      el.appendChild(option);
    });
    el.value = String(current >= 0 ? current : -1);
  };

  fill('#audIn', (devices || []).filter((d) => d.inputs > 0), settings.inputDevice);
  fill('#audOut', (devices || []).filter((d) => d.outputs > 0), settings.outputDevice);
}

/* ---------------------------------------------------------------- */
/* Ajustes: bloques con acceso rapido                               */
/* ---------------------------------------------------------------- */
const settingsBlocks = ['apariencia', 'cuenta', 'audio', 'red', 'general', 'llamadas'];

function showSettingsBlock(name) {
  if (!settingsBlocks.includes(name)) name = 'apariencia';
  $$('#settingsTabs button').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.block === name);
  });
  $$('.settings-block').forEach((el) => {
    el.classList.toggle('active', el.dataset.block === name);
  });
  const view = $('#view-settings');
  if (view) view.scrollTop = 0;
  localStorage.setItem('msip.settingsBlock', name);
}

$$('#settingsTabs button').forEach((btn) => {
  btn.addEventListener('click', () => showSettingsBlock(btn.dataset.block));
});

showSettingsBlock(localStorage.getItem('msip.settingsBlock') || 'apariencia');

function renderRingDeviceSelect(devices, current) {
  const select = $('#setRingDevice');
  if (!select) return;
  const previous = select.value;
  select.innerHTML = '<option value="">Predeterminado</option>';
  devices.forEach((name) => {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name;
    select.appendChild(opt);
  });
  select.value = current || previous || '';
}

function renderCodecSelect(codecs, force) {
  const select = $('#audForce');
  if (!select) return;
  const previous = select.value;
  select.innerHTML = '<option value="">Automático</option>';
  codecs.forEach((codec) => {
    if (!codec.id || codec.id.startsWith('video')) return;
    const opt = document.createElement('option');
    opt.value = codec.id;
    opt.textContent = codec.id;
    select.appendChild(opt);
  });
  select.value = force || previous || '';
}

function renderSettingsForm() {
  const state = ui.snapshot.state || {};
  const settings = state.settings || {};
  const set = (id, value) => {
    const el = $(id);
    if (el && document.activeElement !== el) el.value = value;
  };
  const toggle = (id, on) => {
    const el = $(id);
    if (el) el.classList.toggle('on', !!on);
  };

  set('#netSourcePort', settings.sourcePort);
  set('#netStun', settings.stun);
  set('#netRtpMin', settings.rtpPortMin);
  set('#netRtpMax', settings.rtpPortMax);
  set('#netDnsSrv', settings.dnsSrv);
  set('#netLogLevel', String(settings.logLevel));
  toggle('#netEnableStun', settings.enableStun);
  toggle('#audEc', settings.ec);
  toggle('#audVad', settings.vad);
  toggle('#audLocalDtmf', settings.localDTMF);
  toggle('#genMinimized', settings.startMinimized);
  toggle('#genSingle', settings.singleInstance);
  toggle('#genAlwaysOnTop', settings.alwaysOnTop);
  toggle('#setAutoAnswer', settings.autoAnswer);
  toggle('#setCallWaiting', settings.callWaiting);
  toggle('#setJabra', settings.jabraIntegration);
  toggle('#setHidHeadset', settings.hidHeadset);
  toggle('#setTeamsPresence', settings.teamsPresence);
  toggle('#setCallToast', settings.callToast);
  set('#setToastPosition', settings.callToastPosition || 'topright');
  set('#setToastScreen', settings.callToastScreen || 'primary');
  toggle('#setForwarding', settings.forwarding);
  set('#setForwardNumber', settings.forwardingNumber || '');
  set('#setFeatureCodeCP', settings.featureCodeCP || '**');
  set('#audCodecs', settings.audioCodecs || '');
  set('#audDtmf', settings.dtmfMethod || 'rfc2833');
  renderCodecSelect(state.codecs || [], settings.forceCodec || '');
  renderRingDeviceSelect(state.ringDevices || [], settings.ringDevice || '');
  const langSelect = $('#langSelect');
  if (langSelect) langSelect.value = settings.language || 'system';
  const ringVolume = $('#ringVol');
  if (ringVolume) {
    ringVolume.value = String(settings.volumeRing ?? 100);
    $('#ringVolVal').textContent = ringVolume.value + '%';
  }

  const genName = $('#genAccName');
  const genMeta = $('#genAccMeta');
  if (genName) genName.textContent = state.displayName || state.account || t('Cuenta');
  if (genMeta) {
    genMeta.textContent = [
      state.account,
      state.server,
      (state.transport || 'udp').toUpperCase(),
      state.registered ? t('Registrado') : t('Sin registrar'),
    ].filter(Boolean).join(' · ');
  }

  renderAudioDevices(state.audioDevices, settings);
}

[
  ['#netSourcePort', 'sourcePort'],
  ['#netStun', 'STUN'],
  ['#netRtpMin', 'rtpPortMin'],
  ['#netRtpMax', 'rtpPortMax'],
  ['#netDnsSrv', 'dnsSrvNs'],
  ['#netLogLevel', 'logLevel'],
  ['#audIn', 'inputDevice'],
  ['#audOut', 'outputDevice'],
  ['#audCodecs', 'audioCodecs'],
  ['#audForce', 'forceCodec'],
  ['#audDtmf', 'DTMFMethod'],
  ['#setForwardNumber', 'forwardingNumber'],
  ['#setFeatureCodeCP', 'featureCodeCP'],
  ['#setRingDevice', 'ringDevice'],
  ['#setToastPosition', 'callToastPosition'],
  ['#setToastScreen', 'callToastScreen'],
].forEach(([id, key]) => {
  const el = $(id);
  if (!el) return;
  el.addEventListener('change', () => post({ cmd: 'setting', key, value: el.value }));
});

$$('[data-setting]').forEach((btn) => {
  btn.addEventListener('click', () => {
    const on = !btn.classList.contains('on');
    btn.classList.toggle('on', on);
    post({ cmd: 'setting', key: btn.dataset.setting, value: on ? '1' : '0' });
  });
});

const langSelect = $('#langSelect');
if (langSelect) {
  langSelect.addEventListener('change', () => {
    const value = langSelect.value;
    if (value === 'system') {
      localStorage.removeItem('msip.lang');
      post({ cmd: 'setting', key: 'language', value: '' });
    }
    else {
      localStorage.setItem('msip.lang', value);
      post({ cmd: 'setting', key: 'language', value });
    }
    localStorage.setItem('msip.viewAfterReload', 'settings');
    setTimeout(() => location.reload(), 150);
  });
}

/* ---------------------------------------------------------------- */
/* Ajustes: volumen                                                 */
/* ---------------------------------------------------------------- */
function wireSlider(inputId, labelId, output) {
  const input = $(inputId);
  const label = $(labelId);
  if (!input || !label) return;
  const update = () => {
    label.textContent = input.value + '%';
    post({ cmd: 'volume', output, value: Number(input.value) });
  };
  input.addEventListener('input', () => {
    label.textContent = input.value + '%';
  });
  input.addEventListener('change', update);
  label.textContent = input.value + '%';
}

wireSlider('#volOut', '#volOutVal', true);
wireSlider('#volIn', '#volInVal', false);

const ringVol = $('#ringVol');
if (ringVol) {
  ringVol.addEventListener('input', () => {
    $('#ringVolVal').textContent = ringVol.value + '%';
  });
  ringVol.addEventListener('change', () => {
    post({ cmd: 'setting', key: 'volumeRing', value: ringVol.value });
  });
}

/* ---------------------------------------------------------------- */
/* Buscadores                                                       */
/* ---------------------------------------------------------------- */
function wireSearch(inputId, scope) {
  const input = $(inputId);
  if (!input) return;
  input.addEventListener('input', () => {
    const q = input.value.trim().toLowerCase();
    $$(scope + ' .row').forEach((row) => {
      row.style.display = row.textContent.toLowerCase().includes(q) ? '' : 'none';
    });
  });
}

wireSearch('#callsSearch', '#view-calls');
wireSearch('#contactsSearch', '#view-contacts');

/* ---------------------------------------------------------------- */
/* Snapshot desde C++                                               */
/* ---------------------------------------------------------------- */
function applySnapshot(snapshot) {
  ui.snapshot = snapshot || { state: {}, history: [], contacts: [] };
  const state = ui.snapshot.state || {};

  const lang = (state.settings || {}).language || '';
  if (lang && lang !== i18n.current) {
    localStorage.setItem('msip.lang', lang);
    location.reload();
    return;
  }

  if (!localStorage.getItem('msip.accent') && state.accent
      && accentNames[state.accent] && state.accent !== ui.accent) {
    ui.accent = state.accent;
    renderAccent();
  }

  if (state.volumeOut !== undefined) $('#volOut').value = state.volumeOut;
  if (state.volumeIn !== undefined) $('#volIn').value = state.volumeIn;
  $('#volOutVal').textContent = $('#volOut').value + '%';
  $('#volInVal').textContent = $('#volIn').value + '%';

  renderHeader();
  renderQuick();
  renderShortcuts();
  renderMenuState();
  renderHistory();
  renderContacts();
  renderMessages();
  renderAccountForm();
  renderSettingsForm();
  renderOverlay();
}

function escapeHtml(value) {
  return String(value || '').replace(/[&<>"']/g, (ch) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[ch]));
}

window.addEventListener('error', (event) => post({ cmd: 'jserror', message: String(event.message) }));

if (api) {
  api.addEventListener('message', (event) => {
    const data = event.data;
    if (data && data.ev === 'snapshot') {
      applySnapshot(data);
    }
    else if (data && data.ev === 'clipboard') {
      const cleaned = String(data.text || '').replace(/[^\d+*#]/g, '');
      if (cleaned) {
        ui.number = cleaned;
        renderNumber();
        if (dialInput) dialInput.focus();
      }
    }
  });
  post({ cmd: 'ready' });
} else {
  // Vista previa en navegador: datos de ejemplo para poder maquetar.
  applySnapshot({
    state: {
      account: 'demo', server: 'sip.miempresa.com', registered: true, transport: 'udp',
      dnd: false, record: false, muted: false, volumeOut: 80, volumeIn: 60, calls: [],
      activeAccount: 1,
      accounts: [
        { index: 1, label: 'Demo', username: 'demo', server: 'sip.miempresa.com', displayName: 'Demo', transport: 'udp', registered: true, status: 200, reason: 'OK', active: true },
      ],
    },
    history: [
      { time: Math.floor(Date.now() / 1000) - 600, number: '204', name: 'Ana Torres', type: 'missed', duration: 0 },
      { time: Math.floor(Date.now() / 1000) - 3600, number: '210', name: 'Soporte Nivel 2', type: 'out', duration: 240 },
    ],
    contacts: [
      { name: 'Ana Torres', number: '204', info: '' },
      { name: 'Luis Martín', number: '215', info: '' },
    ],
    messages: [
      { time: Math.floor(Date.now() / 1000) - 420, peer: '204', from: 'sip:204@demo', body: '¿Me pasas el informe?', outgoing: false, status: 'received' },
      { time: Math.floor(Date.now() / 1000) - 300, peer: '204', from: 'sip:demo@demo', body: 'Claro, te lo envío ahora.', outgoing: true, status: 'sent' },
    ],
  });
}

renderNumber();

if (localStorage.getItem('msip.viewAfterReload') === 'settings') {
  localStorage.removeItem('msip.viewAfterReload');
  showView('settings');
  showSettingsBlock(localStorage.getItem('msip.settingsBlock') || 'general');
}

