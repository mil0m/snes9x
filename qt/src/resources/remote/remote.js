// Snes9x remote control (roadmap #53). Talks to the app's RemoteServer over one WebSocket.
// The key comes in the link's fragment (#...), which browsers never send to a server.
"use strict";

const $ = (id) => document.getElementById(id);
const MEMORIES = ["wram", "sram", "vram", "oam", "cgram", "aram", "rom"]; // the app's order
const MAX_WINDOW = 128 * 1024;

const state = {
  ws: null, colours: null, status: null, nextId: 1, retry: 1000, refused: false,
  memory: "wram", offset: 0, width: 64, length: 0, bytes: null, bytesOffset: 0,
  painting: false, pending: new Set(), lastPaint: 0, chaosTimer: null, ruleTimers: {},
};

function key() {
  const fromHash = location.hash.length > 1 ? decodeURIComponent(location.hash.slice(1)) : "";
  if (fromHash) {
    try { sessionStorage.setItem("s9x-remote-key", fromHash); } catch (e) { /* private mode */ }
    history.replaceState(null, "", location.pathname); // out of the address bar
    return fromHash;
  }
  try { return sessionStorage.getItem("s9x-remote-key") || ""; } catch (e) { return ""; }
}

function send(obj) {
  if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
  obj.id = state.nextId++;
  state.ws.send(JSON.stringify(obj));
}

function connect() {
  const token = key();
  if (!token) { $("nokey").hidden = false; setChip("no key", "off"); return; }
  const ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws");
  ws.binaryType = "arraybuffer";
  state.ws = ws;
  ws.onopen = () => ws.send(JSON.stringify({ hello: 1, token }));
  ws.onmessage = (ev) => (typeof ev.data === "string" ? onText(JSON.parse(ev.data)) : onBinary(ev.data));
  ws.onclose = () => {
    state.ws = null;
    setChip("offline", "off");
    if (state.refused) return;
    setTimeout(connect, state.retry);
    state.retry = Math.min(state.retry * 2, 15000);
  };
}

function onText(m) {
  if (m.welcome) {
    state.colours = m.colours.map(hexToRgb);
    state.retry = 1000;
    $("ui").hidden = false;
    $("nokey").hidden = true;
    watch();
  } else if (m.status) {
    state.status = m.status;
    render(m.status);
  } else if (m.error) {
    if (!state.colours && /token/.test(m.error)) {
      state.refused = true;
      setChip("refused", "off");
      $("recipe").textContent = m.error === "wrong token" ? "The key in this link is old: ask for a new link." : m.error;
    } else {
      $("message").textContent = "Not done: " + m.error;
    }
  }
}

function hexToRgb(h) {
  const n = parseInt(h.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function setChip(text, cls) {
  const chip = $("state");
  chip.textContent = text;
  chip.className = "chip " + cls;
}

function render(s) {
  $("game").textContent = s.game_loaded ? s.game : "No game loaded";
  $("recipe").textContent = s.recipe ? "Recipe: " + s.recipe : "No recipe";
  const on = s.active;
  setChip(s.starting ? "starting" : on ? "corrupting" : "off", on ? "on" : "off");
  const toggle = $("toggle");
  toggle.textContent = on ? "Stop" : "Corrupt";
  toggle.classList.toggle("on", on);
  toggle.disabled = !s.game_loaded || (!on && !s.has_recipe);
  toggle.title = s.has_recipe ? "" : "No recipe loaded: choose one in the panel, or paint something";
  if (!state.chaosTimer) {
    $("chaos").value = Math.round(s.chaos * 100);
    $("chaos-out").textContent = Math.round(s.chaos * 100) + "%";
  }
  $("message").textContent = s.message || (s.crashes ? s.crashes + " crashes caught" : "");
  $("who").textContent = s.clients.length ? "Connected: " + s.clients.join(", ") : "";
  renderRules(s.rules);
  renderHotkeys(s.rules);
  renderMemories(s.sizes);
}

function renderRules(rules) {
  const list = $("rules");
  $("no-rules").hidden = rules.length > 0;
  while (list.children.length > rules.length) list.lastChild.remove();
  rules.forEach((r, i) => {
    let li = list.children[i];
    if (!li) {
      li = document.createElement("li");
      li.innerHTML =
        '<div class="rule-head"><b></b><label class="check"><input type="checkbox"> On</label></div>' +
        '<p class="rule-what"></p><label class="field"><span>Strength <output></output></span>' +
        '<input type="range" min="0" max="100" step="1"></label>';
      li.querySelector("input[type=checkbox]").addEventListener("change", (e) =>
        send({ do: "rule_enabled", rule: i, on: e.target.checked }));
      const slider = li.querySelector("input[type=range]");
      slider.addEventListener("input", () => {
        li.querySelector("output").textContent = slider.value + "%";
        clearTimeout(state.ruleTimers[i]);
        state.ruleTimers[i] = setTimeout(() => {
          send({ do: "rule_strength", rule: i, value: slider.value / 100 });
          state.ruleTimers[i] = null;
        }, 120);
      });
      list.appendChild(li);
    }
    li.querySelector("b").textContent = r.name || "Rule " + (i + 1);
    li.querySelector(".rule-what").textContent = r.what;
    li.querySelector("input[type=checkbox]").checked = r.enabled;
    if (!state.ruleTimers[i]) {
      li.querySelector("input[type=range]").value = Math.round(r.strength * 100);
      li.querySelector("output").textContent = Math.round(r.strength * 100) + "%";
    }
  });
}

function renderHotkeys(rules) {
  const keys = new Map();
  for (const r of rules) if (r.hotkey && !keys.has(r.hotkey)) keys.set(r.hotkey, r.name);
  $("hotkeys-card").hidden = keys.size === 0;
  const box = $("hotkeys");
  const want = [...keys.entries()].sort((a, b) => a[0] - b[0]);
  const sig = JSON.stringify(want);
  if (box.dataset.sig === sig) return;
  box.dataset.sig = sig;
  box.textContent = "";
  for (const [k, name] of want) {
    const b = document.createElement("button");
    b.textContent = k + " · " + name;
    b.addEventListener("click", () => send({ do: "hotkey", key: k }));
    box.appendChild(b);
  }
}

function renderMemories(sizes) {
  const sel = $("memory");
  const have = MEMORIES.filter((m) => sizes[m] > 0);
  const sig = have.join(",");
  if (sel.dataset.sig !== sig) {
    sel.dataset.sig = sig;
    sel.textContent = "";
    for (const m of have) {
      const o = document.createElement("option");
      o.value = m;
      o.textContent = m.toUpperCase();
      sel.appendChild(o);
    }
    if (!have.includes(state.memory) && have.length) state.memory = have[0];
    sel.value = state.memory;
    watch();
  }
}

// The window: whole rows, at most 128 KB, inside the memory.
function watch() {
  const s = state.status;
  if (!s || !state.colours) return;
  const size = s.sizes[state.memory] || 0;
  if (!size) { send({ do: "watch", memory: "none" }); return; }
  const w = state.width;
  const rows = Math.max(1, Math.min(Math.floor(MAX_WINDOW / w), Math.ceil(size / w)));
  state.offset = Math.max(0, Math.min(state.offset - (state.offset % w), Math.max(0, size - w)));
  state.length = Math.min(rows * w, size - state.offset);
  $("offset").value = state.offset.toString(16).toUpperCase();
  const canvas = $("picture");
  canvas.width = w;
  canvas.height = Math.ceil(state.length / w);
  send({ do: "watch", memory: state.memory, offset: state.offset, length: state.length });
}

function onBinary(buf) {
  const v = new Uint8Array(buf);
  if (v.length < 9 || v[0] !== 0x53 || v[1] !== 0x39 || v[2] !== 0x58 || v[3] !== 0x4d) return;
  const memory = MEMORIES[v[4]];
  const offset = v[5] | (v[6] << 8) | (v[7] << 16) | (v[8] << 24);
  if (memory !== state.memory || offset !== state.offset) return; // an old window
  state.bytes = v.subarray(9);
  state.bytesOffset = offset;
  draw();
}

function draw() {
  const canvas = $("picture");
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(canvas.width, canvas.height);
  const b = state.bytes;
  for (let i = 0; i < b.length && i * 4 < img.data.length; i++) {
    const c = state.colours[b[i]];
    img.data[i * 4] = c[0];
    img.data[i * 4 + 1] = c[1];
    img.data[i * 4 + 2] = c[2];
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

function offsetAt(ev) {
  const canvas = $("picture");
  const r = canvas.getBoundingClientRect();
  const x = Math.floor(((ev.clientX - r.left) / r.width) * canvas.width);
  const y = Math.floor(((ev.clientY - r.top) / r.height) * canvas.height);
  if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return -1;
  const i = y * canvas.width + x;
  return i < state.length ? state.offset + i : -1;
}

function flushPaint() {
  if (!state.pending.size) return;
  const now = performance.now();
  if (now - state.lastPaint < 60) { setTimeout(flushPaint, 60); return; } // under the app's 20 a second
  state.lastPaint = now;
  const value = parseInt($("value").value, 16);
  const offsets = [...state.pending].slice(0, 4096);
  offsets.forEach((o) => state.pending.delete(o));
  if (Number.isInteger(value) && value >= 0 && value <= 255)
    send({ do: "paint", memory: state.memory, offsets, value });
  if (state.pending.size) setTimeout(flushPaint, 60);
}

function setup() {
  $("toggle").addEventListener("click", () => send({ do: "active", on: !(state.status && state.status.active) }));
  $("reroll").addEventListener("click", () => send({ do: "reroll" }));
  $("crashed").addEventListener("click", () => send({ do: "mark_crash" }));
  $("normal").addEventListener("click", () => send({ do: "back_to_normal" }));
  $("chaos").addEventListener("input", (e) => {
    $("chaos-out").textContent = e.target.value + "%";
    clearTimeout(state.chaosTimer);
    state.chaosTimer = setTimeout(() => {
      send({ do: "chaos", value: e.target.value / 100 });
      state.chaosTimer = null;
    }, 120);
  });
  $("memory").addEventListener("change", (e) => { state.memory = e.target.value; state.offset = 0; watch(); });
  $("width").addEventListener("change", (e) => { state.width = parseInt(e.target.value, 10); watch(); });
  $("offset").addEventListener("change", (e) => {
    const n = parseInt(e.target.value.replace(/^\$|^0x/i, ""), 16);
    if (Number.isInteger(n)) { state.offset = n; watch(); }
  });
  $("prev").addEventListener("click", () => { state.offset = Math.max(0, state.offset - state.length); watch(); });
  $("next").addEventListener("click", () => {
    const size = (state.status && state.status.sizes[state.memory]) || 0;
    if (state.offset + state.length < size) { state.offset += state.length; watch(); }
  });
  $("paint").addEventListener("change", (e) => {
    state.painting = e.target.checked;
    document.body.classList.toggle("paint", state.painting);
  });
  const canvas = $("picture");
  let down = false;
  const paintAt = (ev) => {
    const o = offsetAt(ev);
    if (o >= 0) { state.pending.add(o); flushPaint(); }
  };
  canvas.addEventListener("pointerdown", (ev) => {
    if (!state.painting) return;
    down = true;
    canvas.setPointerCapture(ev.pointerId);
    paintAt(ev);
  });
  canvas.addEventListener("pointermove", (ev) => {
    const o = offsetAt(ev);
    if (o >= 0 && state.bytes) {
      const b = state.bytes[o - state.bytesOffset];
      $("hover").textContent = state.memory.toUpperCase() + " $" + o.toString(16).toUpperCase().padStart(5, "0") +
        (b === undefined ? "" : " = " + b.toString(16).toUpperCase().padStart(2, "0"));
    }
    if (down && state.painting) paintAt(ev);
  });
  const up = () => { down = false; };
  canvas.addEventListener("pointerup", up);
  canvas.addEventListener("pointercancel", up);
  connect();
}

document.addEventListener("DOMContentLoaded", setup);
