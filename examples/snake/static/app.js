import * as icons from "/icons.js";

const $ = (id) => document.getElementById(id);
const canvas = $("board");
const ctx = canvas.getContext("2d");
const dialog = $("restart-dialog");
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
const storageKey = "lm0-snake-v1";
const palettes = {
  lime: { name: "Lime", head: "#b4f36c", tail: "#4d9670" },
  mint: { name: "Mint", head: "#72ddae", tail: "#388b86" },
  ice: { name: "Ice", head: "#8ac9e8", tail: "#497fbd" },
  rose: { name: "Rose", head: "#e9a9bb", tail: "#a76182" },
};
const statusLabels = { ready: "Ready to play", running: "In play", paused: "Paused", lost: "Run ended", won: "Board complete" };
let config, state, previousBody, movedAt = 0, tickTimer, toastTimer;
let commandQueue = Promise.resolve();
let pending = 0, offline = false, finished = false, initializing = false;
let turns = [], inFlightDirection = null, pendingReset = null, resumeAfterDialog = false;
let frameWidth = 0, frameHeight = 0, audioContext;

function readSaved() {
  try {
    const data = JSON.parse(localStorage.getItem(storageKey)) || {};
    return {
      best: Number.isSafeInteger(data.best) && data.best >= 0 ? data.best : 0,
      rounds: Number.isSafeInteger(data.rounds) && data.rounds >= 0 ? data.rounds : 0,
      runs: Array.isArray(data.runs) ? data.runs.filter((run) => run &&
        Number.isSafeInteger(run.score) && run.score >= 0 &&
        ["classic", "wrap"].includes(run.mode)).slice(0, 3) : [],
      color: Object.hasOwn(palettes, data.color) ? data.color : "lime",
      sound: data.sound === true,
    };
  } catch { return { best: 0, rounds: 0, runs: [], color: "lime", sound: false }; }
}
const saved = readSaved();

function persist() {
  try { localStorage.setItem(storageKey, JSON.stringify(saved)); } catch { /* Storage is optional. */ }
}

function icon(element, name) {
  element.replaceChildren(icons.createElement(icons[name], { "aria-hidden": "true", width: 20, height: 20 }));
}
document.querySelectorAll("[data-icon]").forEach((element) => icon(element, element.dataset.icon));

function announce(message) {
  clearTimeout(toastTimer);
  $("announcement").textContent = message;
  $("announcement").classList.add("visible");
  toastTimer = setTimeout(() => $("announcement").classList.remove("visible"), 2600);
}

function unlockAudio() {
  if (!saved.sound) return;
  try {
    audioContext ??= new (window.AudioContext || window.webkitAudioContext)();
    audioContext.resume().catch(() => {});
  } catch { /* The game also works without an audio device. */ }
}

function tone(kind) {
  if (!saved.sound || !audioContext || audioContext.state !== "running") return;
  const frequencies = kind === "food" ? [660, 880] : kind === "lost" ? [180, 90] : [440, 660];
  frequencies.forEach((frequency, index) => {
    const oscillator = audioContext.createOscillator();
    const gain = audioContext.createGain();
    const start = audioContext.currentTime + index * .075;
    oscillator.type = "sine";
    oscillator.frequency.setValueAtTime(frequency, start);
    gain.gain.setValueAtTime(0, start);
    gain.gain.linearRampToValueAtTime(.07, start + .01);
    gain.gain.exponentialRampToValueAtTime(.001, start + .16);
    oscillator.connect(gain).connect(audioContext.destination);
    oscillator.start(start);
    oscillator.stop(start + .18);
  });
}

async function request(path, body) {
  const response = await fetch(path, {
    method: body === undefined ? "GET" : "POST",
    headers: body === undefined ? {} : { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(4000),
    cache: "no-store",
  });
  const result = await response.json();
  if (!response.ok) {
    const error = new Error(result.error || "The game could not be reached");
    error.status = response.status;
    throw error;
  }
  return result;
}

function stopClock() { clearTimeout(tickTimer); }

function schedule(delay) {
  stopClock();
  if (state?.status !== "running" || offline || pending || dialog.open || document.hidden) return;
  tickTimer = setTimeout(() => {
    inFlightDirection = turns.shift() ?? -1;
    perform("step", { direction: inFlightDirection });
  }, delay ?? 1000 / state.speed);
}

function connectionLost(error) {
  offline = true;
  turns = [];
  inFlightDirection = null;
  stopClock();
  render();
  $("overlay-detail").textContent = error.name === "TimeoutError" ? "The game is taking too long to respond." : "The game connection was interrupted.";
  $("overlay-detail").hidden = false;
}

// Serialize native commands so a late tick cannot overwrite a pause or a new game.
function perform(action, extra = {}) {
  stopClock();
  pending++;
  renderControls();
  const job = commandQueue.then(async () => {
    if (offline) return;
    const started = performance.now();
    const next = await request(`/api/games/${state.id}`, { action, ...extra });
    if (action === "reset") {
      finished = false;
      turns = [];
      previousBody = null;
    }
    applyState(next, action === "step");
    if (action === "step") inFlightDirection = null;
    return Math.max(0, 1000 / next.speed - (performance.now() - started));
  }).catch(connectionLost).finally(() => {
    pending--;
    renderControls();
  });
  commandQueue = job.then(() => {});
  job.then((delay) => schedule(action === "step" ? delay : undefined));
  return job;
}

function applyState(next, moved = false) {
  const old = state;
  if (moved && old && next.ticks !== old.ticks) {
    previousBody = old.body;
    movedAt = performance.now();
  } else if (!old || next.status === "ready" || next.status !== "running") {
    previousBody = null;
  }
  state = next;
  if (old && next.score > old.score) {
    tone("food");
    $("food-gain").textContent = `+${next.score - old.score}`;
    $("food-gain").classList.remove("pop");
    void $("food-gain").offsetWidth;
    $("food-gain").classList.add("pop");
  }
  if (next.score > saved.best) { saved.best = next.score; persist(); }
  if (["lost", "won"].includes(next.status) && !finished) {
    finished = true;
    saved.rounds++;
    saved.runs.unshift({ score: next.score, mode: next.mode });
    saved.runs = saved.runs.slice(0, 3);
    persist();
    tone(next.status === "lost" ? "lost" : "food");
    announce(next.status === "won" ? "Board complete!" : `Run ended. ${next.score} points.`);
  }
  render();
}

function renderHistory() {
  $("rounds").textContent = String(saved.rounds).padStart(2, "0");
  if (!saved.runs.length) return;
  $("history").replaceChildren(...saved.runs.map((run, index) => {
    const li = document.createElement("li");
    const rank = document.createElement("span");
    rank.className = "run-rank";
    rank.textContent = String(index + 1).padStart(2, "0");
    const mode = document.createElement("span");
    mode.className = "run-mode";
    mode.textContent = run.mode === "wrap" ? "Wrap" : "Classic";
    const score = document.createElement("span");
    score.className = "run-score";
    score.textContent = String(run.score).padStart(3, "0");
    li.append(rank, mode, score);
    return li;
  }));
}

function renderControls() {
  const available = state && !offline;
  $("pause").disabled = !available || !["running", "paused"].includes(state.status);
  $("restart").disabled = !available || pending > 0;
  $("play").disabled = initializing || pending > 0 || (!state && !offline);
  $("speed").disabled = !available;
  document.querySelectorAll("[data-mode]").forEach((button) => { button.disabled = !available || pending > 0; });
  $("dialog-confirm").disabled = pending > 0;
}

function render() {
  $("best").textContent = String(saved.best).padStart(3, "0");
  renderHistory();
  renderControls();
  if (!state && !offline) return;
  if (state) {
    $("score").textContent = String(state.score).padStart(3, "0");
    $("length").textContent = String(state.length).padStart(2, "0");
    if (document.activeElement !== $("speed")) {
      $("speed").value = state.speed;
      $("speed-value").value = state.speed;
    }
    document.querySelectorAll("[data-mode]").forEach((button) => {
      button.setAttribute("aria-pressed", String(button.dataset.mode === state.mode));
    });
  }
  const status = offline ? "error" : state.status;
  $("status").textContent = offline ? "Disconnected" : statusLabels[status];
  $("state-dot").className = `state-dot ${status}`;
  $("overlay").hidden = status === "running";
  const views = {
    ready: ["SNAKE / 001", "A fresh start.", "Play", "Play", ""],
    paused: ["TAKE YOUR TIME", "A little breather.", "Resume", "Play", ""],
    lost: ["THERE'S ALWAYS ANOTHER", "One more round?", "Play again", "RotateCcw", state?.reason === "wall" ? "A close encounter with the wall." : "Tied up in yourself."],
    won: ["EVERY CELL. ALL YOURS.", "Board complete.", "Play again", "RotateCcw", `${state?.score ?? 0} points. A perfect finish.`],
    error: ["CONNECTION LOST", "Hold that thought.", "Reconnect", "RotateCcw", ""],
  };
  if (views[status]) {
    const [eyebrow, title, label, symbol, detail] = views[status];
    $("overlay-eyebrow").textContent = eyebrow;
    $("overlay-title").textContent = title;
    $("play-label").textContent = label;
    icon($("play").firstElementChild, symbol);
    $("overlay-detail").textContent = detail;
    $("overlay-detail").hidden = !detail;
  }
  const paused = status === "paused";
  icon($("pause"), paused ? "Play" : "Pause");
  $("pause").setAttribute("aria-label", paused ? "Resume game" : "Pause game");
  $("pause").dataset.tip = paused ? "Resume" : "Pause";
}

function applyPreferences() {
  icon($("sound"), saved.sound ? "Volume2" : "VolumeX");
  $("sound").setAttribute("aria-pressed", String(saved.sound));
  $("sound").setAttribute("aria-label", saved.sound ? "Mute sound" : "Enable sound");
  $("sound").dataset.tip = saved.sound ? "Mute sound" : "Enable sound";
  $("color-name").textContent = palettes[saved.color].name;
  document.querySelectorAll("[data-color]").forEach((button) => {
    button.setAttribute("aria-pressed", String(button.dataset.color === saved.color));
  });
}

async function initialize() {
  if (initializing) return;
  initializing = true;
  renderControls();
  try {
    config = await request("/api/config");
    palettes.lime.head = config.visual.snake;
    palettes.lime.tail = config.visual.tail;
    $("board-size").textContent = `${config.game.width} x ${config.game.height}`;
    $("speed").min = config.game.min_speed;
    $("speed").max = config.game.max_speed;
    let next;
    if (state?.id) {
      try {
        next = await request(`/api/games/${state.id}`);
        if (next.status === "running") next = await request(`/api/games/${state.id}`, { action: "pause" });
      } catch (error) {
        if (error.status !== 404) throw error;
      }
    }
    if (!next) {
      next = await request("/api/games", {});
      finished = false;
    }
    offline = false;
    applyState(next);
  } catch (error) { connectionLost(error); }
  finally { initializing = false; renderControls(); }
}

async function play() {
  unlockAudio();
  if (offline) { await initialize(); return; }
  if (!state || pending) return;
  if (["lost", "won"].includes(state.status)) await perform("reset");
  if (offline) return;
  if (["ready", "paused"].includes(state.status)) {
    tone("start");
    await perform("start");
    canvas.focus({ preventScroll: true });
  }
}

function togglePause() {
  if (!state || offline || dialog.open) return;
  if (state.status === "running") {
    turns = [];
    perform("pause");
  } else if (["ready", "paused"].includes(state.status)) play();
}

function direction(next) {
  if (!state || offline || dialog.open || !["ready", "running"].includes(state.status)) return;
  unlockAudio();
  const last = turns.at(-1) ?? (inFlightDirection !== null && inFlightDirection >= 0 ? inFlightDirection : state.direction);
  if (next === last) {
    if (state.status === "ready" && !pending) play();
    return;
  }
  if (next === (last + 2) % 4 || turns.length >= 2) return;
  turns.push(next);
  if (state.status === "ready" && !pending) play();
}

async function newRound(options = {}) {
  if (!state || offline || pending || dialog.open) return;
  if (["running", "paused"].includes(state.status)) {
    pendingReset = options;
    resumeAfterDialog = state.status === "running";
    dialog.showModal();
    turns = [];
    stopClock();
    if (resumeAfterDialog) await perform("pause");
  } else {
    await perform("reset", options);
  }
}

function cancelRestart() {
  dialog.close();
  pendingReset = null;
  if (resumeAfterDialog && !offline) perform("start");
  resumeAfterDialog = false;
  canvas.focus({ preventScroll: true });
}

$("play").addEventListener("click", play);
$("pause").addEventListener("click", togglePause);
$("restart").addEventListener("click", () => newRound());
$("dialog-cancel").addEventListener("click", cancelRestart);
$("dialog-close").addEventListener("click", cancelRestart);
dialog.addEventListener("cancel", (event) => { event.preventDefault(); cancelRestart(); });
$("dialog-confirm").addEventListener("click", async () => {
  const options = pendingReset || {};
  pendingReset = null;
  resumeAfterDialog = false;
  dialog.close();
  await perform("reset", options);
  canvas.focus({ preventScroll: true });
});
document.querySelectorAll("[data-mode]").forEach((button) => button.addEventListener("click", () => {
  if (button.dataset.mode !== state?.mode) newRound({ mode: button.dataset.mode });
}));
$("speed").addEventListener("input", () => { $("speed-value").value = $("speed").value; });
$("speed").addEventListener("change", () => perform("speed", { speed: Number($("speed").value) }));
document.querySelectorAll("[data-color]").forEach((button) => button.addEventListener("click", () => {
  saved.color = button.dataset.color;
  persist();
  applyPreferences();
}));
$("sound").addEventListener("click", () => {
  saved.sound = !saved.sound;
  persist();
  applyPreferences();
  unlockAudio();
  tone("start");
});
$("fullscreen").hidden = !document.fullscreenEnabled;
$("fullscreen").addEventListener("click", async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.querySelector(".app-shell").requestFullscreen();
  } catch { announce("Fullscreen is unavailable in this browser."); }
});
document.addEventListener("fullscreenchange", () => {
  const full = Boolean(document.fullscreenElement);
  icon($("fullscreen"), full ? "Minimize" : "Maximize");
  $("fullscreen").setAttribute("aria-label", full ? "Exit fullscreen" : "Enter fullscreen");
  $("fullscreen").dataset.tip = full ? "Exit fullscreen" : "Fullscreen";
});

const keys = { ArrowUp: 0, w: 0, ArrowRight: 1, d: 1, ArrowDown: 2, s: 2, ArrowLeft: 3, a: 3 };
document.addEventListener("keydown", (event) => {
  if (event.ctrlKey || event.metaKey || event.altKey || dialog.open || event.target.closest("input, select, textarea")) return;
  const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
  if (Object.hasOwn(keys, key)) {
    event.preventDefault();
    direction(keys[key]);
  } else if (!event.repeat && (key === "p" || (key === " " && !event.target.closest("button, a")))) {
    event.preventDefault();
    togglePause();
  } else if (!event.repeat && key === "r") {
    event.preventDefault();
    newRound();
  }
});
document.querySelectorAll("[data-direction]").forEach((button) => button.addEventListener("pointerdown", (event) => {
  event.preventDefault();
  direction(Number(button.dataset.direction));
}));
let swipeStart;
canvas.addEventListener("pointerdown", (event) => {
  swipeStart = [event.clientX, event.clientY];
  canvas.setPointerCapture(event.pointerId);
  canvas.focus({ preventScroll: true });
});
canvas.addEventListener("pointerup", (event) => {
  if (!swipeStart) return;
  const dx = event.clientX - swipeStart[0], dy = event.clientY - swipeStart[1];
  swipeStart = null;
  if (Math.max(Math.abs(dx), Math.abs(dy)) < 18) return;
  direction(Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0));
});
canvas.addEventListener("pointercancel", () => { swipeStart = null; });
function autoPause() {
  if (state?.status === "running" && !offline) { turns = []; perform("pause"); }
}
document.addEventListener("visibilitychange", () => { if (document.hidden) autoPause(); });
window.addEventListener("blur", autoPause);

function resize() {
  const bounds = canvas.getBoundingClientRect();
  const scale = Math.min(window.devicePixelRatio || 1, 3);
  frameWidth = bounds.width;
  frameHeight = bounds.height;
  canvas.width = Math.round(frameWidth * scale);
  canvas.height = Math.round(frameHeight * scale);
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
}
new ResizeObserver(resize).observe(canvas);

function mixedColor(start, end, amount) {
  const channels = [1, 3, 5].map((offset) => Math.round(
    parseInt(start.slice(offset, offset + 2), 16) * (1 - amount) +
    parseInt(end.slice(offset, offset + 2), 16) * amount));
  return `rgb(${channels.join(",")})`;
}

function draw(now) {
  requestAnimationFrame(draw);
  if (!frameWidth || !config || !state) return;
  const { width, height } = config.game;
  const cellW = frameWidth / width, cellH = frameHeight / height;
  const unit = Math.min(cellW, cellH);
  const palette = palettes[saved.color];
  ctx.fillStyle = config.visual.background;
  ctx.fillRect(0, 0, frameWidth, frameHeight);
  ctx.strokeStyle = config.visual.grid;
  ctx.lineWidth = .6;
  ctx.beginPath();
  for (let x = 1; x < width; x++) { ctx.moveTo(x * cellW, 0); ctx.lineTo(x * cellW, frameHeight); }
  for (let y = 1; y < height; y++) { ctx.moveTo(0, y * cellH); ctx.lineTo(frameWidth, y * cellH); }
  ctx.stroke();
  ctx.fillStyle = "#334232";
  for (let x = 4; x < width; x += 4) {
    for (let y = 4; y < height; y += 4) ctx.fillRect(x * cellW - .7, y * cellH - .7, 1.4, 1.4);
  }

  if (state.food >= 0) {
    const x = (state.food % width + .5) * cellW, y = (Math.floor(state.food / width) + .5) * cellH;
    const pulse = reducedMotion.matches || state.status !== "running" ? 1 : 1 + Math.sin(now / 260) * .045;
    ctx.save();
    ctx.translate(x, y);
    ctx.scale(pulse, pulse);
    ctx.shadowColor = "#ff776b40";
    ctx.shadowBlur = unit * .4;
    ctx.fillStyle = config.visual.food;
    ctx.beginPath();
    ctx.roundRect(-unit * .29, -unit * .24, unit * .58, unit * .54, unit * .17);
    ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = "#fdd1b9";
    ctx.fillRect(-unit * .15, -unit * .11, unit * .08, unit * .13);
    ctx.fillStyle = "#aacb80";
    ctx.beginPath();
    ctx.ellipse(unit * .09, -unit * .34, unit * .13, unit * .065, -.6, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  const progress = reducedMotion.matches || !previousBody ? 1 : Math.min(1, (now - movedAt) / (1000 / state.speed));
  const points = state.body.map((cell, index) => {
    let x = cell % width + .5, y = Math.floor(cell / width) + .5;
    if (previousBody && progress < 1) {
      const old = previousBody[Math.min(index, previousBody.length - 1)];
      const ox = old % width + .5, oy = Math.floor(old / width) + .5;
      if (Math.abs(ox - x) <= 1 && Math.abs(oy - y) <= 1) {
        x = ox + (x - ox) * progress;
        y = oy + (y - oy) * progress;
      }
    }
    return [x * cellW, y * cellH];
  });
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.lineWidth = unit * .70;
  for (let index = points.length - 1; index >= 0; index--) {
    const [x, y] = points[index];
    const color = mixedColor(palette.head, palette.tail, index / Math.max(1, points.length - 1));
    ctx.fillStyle = color;
    ctx.strokeStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, unit * .35, 0, Math.PI * 2);
    ctx.fill();
    if (index > 0) {
      const [nx, ny] = points[index - 1];
      if (Math.abs(nx - x) < cellW * 1.5 && Math.abs(ny - y) < cellH * 1.5) {
        ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(nx, ny); ctx.stroke();
      }
    }
    if (index > 0 && index < points.length - 1) {
      ctx.fillStyle = "#ffffff0d";
      ctx.beginPath(); ctx.arc(x, y, unit * .09, 0, Math.PI * 2); ctx.fill();
    }
  }
  if (points.length) {
    ctx.save();
    ctx.translate(...points[0]);
    ctx.rotate((state.direction - 1) * Math.PI / 2);
    ctx.fillStyle = palette.head;
    ctx.beginPath();
    ctx.roundRect(-unit * .39, -unit * .39, unit * .82, unit * .78, unit * .23);
    ctx.fill();
    for (const side of [-1, 1]) {
      ctx.fillStyle = "#f3f8e8";
      ctx.beginPath(); ctx.arc(unit * .14, side * unit * .20, unit * .09, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = config.visual.eyes;
      ctx.beginPath(); ctx.arc(unit * .17, side * unit * .20, unit * .046, 0, Math.PI * 2); ctx.fill();
    }
    ctx.restore();
  }
}

applyPreferences();
renderHistory();
resize();
requestAnimationFrame(draw);
initialize();
