/* ============================================================
   ESPET Control Center — dashboard app
   Views: Overview · Profile · Personality · Memory · Schedule ·
   Skills · Device · Tools. Data: /api/config, /health,
   /auth/status, /api/tools.
   ============================================================ */

/* Shared device API key — matches MIMI_BACKEND_API_KEY in the firmware
   (main/mimi_config.h) and DEVICE_API_KEY in espet-backend/.env.
   Single-user demo scope: this page is served by the backend itself. */
const API_KEY = "espet_secret_device_key_2026";

/* ── Icons (inline SVG, lucide paths) ─────────────────────── */

const PATHS = {
  dashboard: '<rect width="7" height="9" x="3" y="3" rx="1"/><rect width="7" height="5" x="14" y="3" rx="1"/><rect width="7" height="9" x="14" y="12" rx="1"/><rect width="7" height="5" x="3" y="16" rx="1"/>',
  user: '<path d="M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
  sparkles: '<path d="m12 3-1.9 5.8a2 2 0 0 1-1.3 1.3L3 12l5.8 1.9a2 2 0 0 1 1.3 1.3L12 21l1.9-5.8a2 2 0 0 1 1.3-1.3L21 12l-5.8-1.9a2 2 0 0 1-1.3-1.3L12 3Z"/>',
  database: '<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M3 5v14a9 3 0 0 0 18 0V5"/><path d="M3 12a9 3 0 0 0 18 0"/>',
  calendar: '<path d="M8 2v4"/><path d="M16 2v4"/><rect width="18" height="18" x="3" y="4" rx="2"/><path d="M3 10h18"/>',
  book: '<path d="M4 19.5v-15A2.5 2.5 0 0 1 6.5 2H20v20H6.5a2.5 2.5 0 0 1 0-5H20"/>',
  cpu: '<rect width="16" height="16" x="4" y="4" rx="2"/><rect width="6" height="6" x="9" y="9" rx="1"/><path d="M15 2v2"/><path d="M15 20v2"/><path d="M2 15h2"/><path d="M2 9h2"/><path d="M20 15h2"/><path d="M20 9h2"/><path d="M9 2v2"/><path d="M9 20v2"/>',
  box: '<path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/><path d="m3.3 7 8.7 5 8.7-5"/><path d="M12 22V12"/>',
  plus: '<path d="M5 12h14"/><path d="M12 5v14"/>',
  trash: '<path d="M3 6h18"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
  pencil: '<path d="M17 3a2.85 2.83 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z"/>',
  alert: '<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 20h16a2 2 0 0 0 1.73-2Z"/><path d="M12 9v4"/><path d="M12 17h.01"/>',
  info: '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/>',
  check: '<path d="M20 6 9 17l-5-5"/>',
  server: '<rect width="20" height="8" x="2" y="2" rx="2"/><rect width="20" height="8" x="2" y="14" rx="2"/><line x1="6" x2="6.01" y1="6" y2="6"/><line x1="6" x2="6.01" y1="18" y2="18"/>',
  key: '<path d="m15.5 7.5 3 3L22 7l-3-3"/><path d="m21 2-9.6 9.6"/><circle cx="7.5" cy="15.5" r="5.5"/>',
  refresh: '<path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/>',
  clock: '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>',
  save: '<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7"/><path d="M7 3v4a1 1 0 0 0 1 1h7"/>',
  google: '<path d="M21.35 11.1h-9.17v2.73h6.51c-.33 3.81-3.5 5.44-6.5 5.44C8.36 19.27 5 16.25 5 12c0-4.1 3.2-7.27 7.2-7.27 3.09 0 4.9 1.97 4.9 1.97L19 4.72S16.56 2 12.1 2C6.42 2 2.03 6.8 2.03 12c0 5.05 4.13 10 10.22 10 5.35 0 9.25-3.67 9.25-9.09 0-1.15-.15-1.81-.15-1.81Z"/>',
  send: '<path d="m22 2-7 20-4-9-9-4Z"/><path d="M22 2 11 13"/>',
  shield: '<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/>',
  bot: '<path d="M12 8V4H8"/><rect width="16" height="12" x="4" y="8" rx="2"/><path d="M2 14h2"/><path d="M20 14h2"/><path d="M15 13v2"/><path d="M9 13v2"/>',
  dice: '<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M8 8h.01"/><path d="M16 8h.01"/><path d="M12 12h.01"/><path d="M8 16h.01"/><path d="M16 16h.01"/>',
  spark: '<path d="M9.937 15.5A2 2 0 0 0 8.5 14.063l-6.135-1.582a.5.5 0 0 1 0-.962L8.5 9.936A2 2 0 0 0 9.937 8.5l1.582-6.135a.5.5 0 0 1 .963 0L14.063 8.5A2 2 0 0 0 15.5 9.937l6.135 1.581a.5.5 0 0 1 0 .964L15.5 14.063a2 2 0 0 0-1.437 1.437l-1.582 6.135a.5.5 0 0 1-.963 0z"/>',
};

function icon(name, cls = "") {
  return `<svg class="${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${PATHS[name]}</svg>`;
}

/* ── Motion (anime.js v4) ─────────────────────────────────── */

const ANIME = window.anime || null;
const MOTION_OK = !!(ANIME && !window.matchMedia("(prefers-reduced-motion: reduce)").matches);

function anim(targets, params, opts = {}) {
  if (!MOTION_OK) return null;
  return ANIME.animate(targets, params, opts);
}

/* Stagger-rise the cards/banners of a freshly rendered view */
function animateView(root) {
  if (!MOTION_OK) return;
  anim(root.querySelectorAll(".card, .banner, .empty"), {
    opacity: [0, 1],
    translateY: [18, 0],
  }, { delay: ANIME.stagger(75), duration: 550, ease: "outExpo" });
}

/* rAF number roll-up for stat values (element has data-countup) */
function runCountups(root) {
  root.querySelectorAll("[data-countup]").forEach((el) => {
    const target = parseFloat(el.dataset.countup || "0");
    if (!MOTION_OK) { el.textContent = target; return; }
    const dur = 900, t0 = performance.now();
    const tick = (t) => {
      const p = Math.min(1, (t - t0) / dur);
      el.textContent = Math.round(target * (1 - Math.pow(1 - p, 3)));
      if (p < 1) requestAnimationFrame(tick);
    };
    el.textContent = "0";
    requestAnimationFrame(tick);
  });
}

/* Open a modal backdrop with a soft pop on the dialog itself */
function showModal(backdrop) {
  backdrop.classList.add("open");
  if (MOTION_OK) {
    ANIME.animate(backdrop.querySelector(".modal"), {
      opacity: [0, 1],
      scale: [0.92, 1],
      translateY: [12, 0],
    }, { duration: 320, ease: "outCubic" });
  }
}

/* ── State & API ──────────────────────────────────────────── */

const state = {
  config: null,        // full bundle from /api/config
  health: null,        // from /health
  auth: null,          // from /auth/status
  tools: null,         // from /api/tools (lazy)
  toolFilter: "all",
  toolSearch: "",
  dirty: {},           // section -> bool
  view: "overview",
};

async function api(path, options = {}) {
  const res = await fetch(path, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${API_KEY}`,
      ...(options.headers || {}),
    },
  });
  if (!res.ok) {
    let detail = res.statusText;
    try { detail = (await res.json()).detail || detail; } catch {}
    throw new Error(detail);
  }
  return res.json();
}

/* ── Small helpers ────────────────────────────────────────── */

const $ = (sel) => document.querySelector(sel);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

function humanInterval(s) {
  if (s % 604800 === 0 && s >= 604800) return `${s / 604800} week${s > 604800 ? "s" : ""}`;
  if (s % 86400 === 0 && s >= 86400) return `${s / 86400} day${s > 86400 ? "s" : ""}`;
  if (s % 3600 === 0) return `${s / 3600} hour${s >= 7200 ? "s" : ""}`;
  return `${Math.round(s / 60)} min`;
}

function humanAgo(iso) {
  if (!iso) return "never";
  const d = new Date(iso);
  return d.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
}

function toast(message, isError = false) {
  const el = document.createElement("div");
  el.className = `toast${isError ? " error" : ""}`;
  el.innerHTML = `${icon(isError ? "alert" : "check")}<span>${esc(message)}</span>`;
  $("#toast-wrap").appendChild(el);
  setTimeout(() => { el.style.opacity = "0"; el.style.transition = "opacity .3s"; }, 3200);
  setTimeout(() => el.remove(), 3600);
}

function confirmDialog({ title, message, okLabel = "Confirm" }) {
  return new Promise((resolve) => {
    const backdrop = $("#confirm-backdrop");
    $("#confirm-title").textContent = title;
    $("#confirm-message").textContent = message;
    $("#confirm-ok").textContent = okLabel;
    showModal(backdrop);
    const done = (val) => {
      backdrop.classList.remove("open");
      $("#confirm-ok").onclick = null;
      $("#confirm-cancel").onclick = null;
      resolve(val);
    };
    $("#confirm-ok").onclick = () => done(true);
    $("#confirm-cancel").onclick = () => done(false);
  });
}

function setDirty(section, isDirty) {
  state.dirty[section] = isDirty;
  const any = Object.values(state.dirty).some(Boolean);
  $("#chip-dirty").hidden = !any;
}

function markSaved(section) {
  setDirty(section, false);
}

function refreshVersionChip() {
  const v = state.config ? Math.max(...Object.values(state.config.versions)) : null;
  $("#version-text").textContent = v ? `config v${v}` : "config v–";
}

/* ── Shell: nav + topbar + routing ────────────────────────── */

const VIEWS = [
  { id: "overview",    label: "Overview",    icon: "dashboard", group: "Monitor", title: "Overview",    subtitle: "Live status of the backend, Google services, and the device config" },
  { id: "agent",       label: "Agent",       icon: "bot",       group: "Assistant", title: "Agent",     subtitle: "Your agent, styled by its own settings — personality, memory and schedules shape the look" },
  { id: "profile",     label: "Profile",     icon: "user",      group: "Assistant", title: "Profile",    subtitle: "Who MimiClaw is assisting — synced to the device as USER.md" },
  { id: "personality", label: "Personality", icon: "sparkles",  group: "Assistant", title: "Personality", subtitle: "Character and behavior rules — synced to the device as SOUL.md" },
  { id: "memory",      label: "Memory",      icon: "database",  group: "Assistant", title: "Memory",     subtitle: "Long-term memory — synced to the device as MEMORY.md" },
  { id: "schedule",    label: "Schedule",    icon: "calendar",  group: "Assistant", title: "Schedule",   subtitle: "Proactive jobs: briefings and reminders the device runs on its own" },
  { id: "skills",      label: "Skills",      icon: "book",      group: "Assistant", title: "Skills",     subtitle: "Reusable playbooks the agent can follow — synced to the device as markdown files" },
  { id: "device",      label: "Device",      icon: "cpu",       group: "System",   title: "Device",      subtitle: "Device identity, timezone, and sync settings" },
  { id: "tools",       label: "Tools",       icon: "box",       group: "System",   title: "MCP Tools",   subtitle: "Remote tools discovered from the backend's connected MCP servers" },
];

function buildNav() {
  const nav = $("#nav");
  let html = "", lastGroup = "";
  for (const v of VIEWS) {
    if (v.group !== lastGroup) {
      html += `<div class="nav-label">${v.group}</div>`;
      lastGroup = v.group;
    }
    html += `
      <a class="nav-item" href="#${v.id}" data-view="${v.id}">
        ${icon(v.icon)}<span>${v.label}</span>
        ${v.id === "overview" ? '<span class="nav-dot" id="nav-status-dot"></span>' : ""}
      </a>`;
  }
  nav.innerHTML = html;
}

function currentView() {
  const hash = location.hash.replace("#", "") || "overview";
  return VIEWS.some((v) => v.id === hash) ? hash : "overview";
}

async function navigate() {
  const view = currentView();
  if (Object.values(state.dirty).some(Boolean) && state.view !== view) {
    const leave = await confirmDialog({
      title: "Unsaved changes",
      message: "You have unsaved edits. Leave this page anyway?",
      okLabel: "Discard & leave",
    });
    if (!leave) { location.hash = `#${state.view}`; return; }
    state.dirty = {};
  }
  state.view = view;
  const meta = VIEWS.find((v) => v.id === view);
  $("#view-title").textContent = meta.title;
  $("#view-subtitle").textContent = meta.subtitle;
  document.querySelectorAll(".nav-item").forEach((el) =>
    el.classList.toggle("active", el.dataset.view === view));

  const root = $("#view-root");
  root.innerHTML = `<div class="skeleton" style="height:120px"></div>`;
  try {
    await RENDERERS[view](root);
  } catch (err) {
    root.innerHTML = `<div class="banner danger">${icon("alert")}<span>Failed to load this view: ${esc(err.message)}</span></div>`;
  }
  animateView(root);
}

/* ── View: Overview ───────────────────────────────────────── */

async function renderOverview(root) {
  const [cfg, health, auth] = await Promise.all([
    api("/api/config"), api("/health").catch(() => null), api("/auth/status").catch(() => null),
  ]);
  state.config = cfg; state.health = health; state.auth = auth;
  refreshVersionChip(); updateServerChip();

  const jobs = cfg.jobs;
  const recurring = jobs.filter((j) => j.kind === "every").length;
  const oneshot = jobs.length - recurring;
  const servers = Object.entries(health?.mcp_servers || {});
  const connected = servers.filter(([, s]) => s.connected).length;
  const dev = cfg.device;

  root.innerHTML = `
    <div class="stack">
      <div class="grid cols-3">
        <div class="card stat">
          <div class="stat-label">${icon("server")} Backend</div>
          <div class="stat-value ${health ? "ok" : "bad"}">${health ? "Healthy" : "Down"}</div>
          <div class="stat-detail">${health ? `<span data-countup="${health.total_tools}">0</span> MCP tools · ${servers.length} servers` : "Unreachable"}</div>
        </div>
        <div class="card stat">
          <div class="stat-label">${icon("key")} Google account</div>
          <div class="stat-value ${auth?.authenticated ? "ok" : "dim"}">${auth?.authenticated ? "Linked" : "Not linked"}</div>
          <div class="stat-detail">${auth?.authenticated ? esc(auth.email || "Google Account") : "Link Google to enable Gmail, Calendar and Drive tools"}</div>
          ${auth?.authenticated
            ? `<button class="btn small" id="auth-relink" style="margin-top:10px">${icon("google")} Re-link Google</button>`
            : `<button class="btn blue" id="auth-login" style="width:100%;margin-top:12px">${icon("google")} Login with Google</button>`}
        </div>
        <div class="card stat">
          <div class="stat-label">${icon("calendar")} Scheduled jobs</div>
          <div class="stat-value" data-countup="${jobs.length}">0</div>
          <div class="stat-detail">${recurring} recurring · ${oneshot} one-time</div>
        </div>
      </div>

      <div class="grid cols-2">
        <div class="card">
          <div class="card-head">
            <div class="card-title">${icon("cpu")} ${esc(dev.name)}</div>
            <span class="badge emerald">pulls every ${humanInterval(dev.sync_interval_s)}</span>
          </div>
          <div class="kv">
            <div class="kv-row"><span class="k">Timezone</span><span class="v">${esc(dev.timezone)}</span></div>
            <div class="kv-row"><span class="k">POSIX TZ applied on device</span><span class="v">${esc(cfg.device.posix_tz || "UTC0")}</span></div>
            <div class="kv-row"><span class="k">Boot behavior</span><span class="v">pull once on startup</span></div>
          </div>
        </div>

        <div class="card">
          <div class="card-head">
            <div class="card-title">${icon("refresh")} Config versions</div>
            <span class="badge blue">config v${Math.max(...Object.values(cfg.versions))}</span>
          </div>
          <div class="kv">
            ${["user", "soul", "memory", "device", "jobs", "skills"].map((s) => `
              <div class="kv-row">
                <span class="k">${{ user: "USER.md (profile)", soul: "SOUL.md (personality)", memory: "MEMORY.md (memory)", device: "Device settings", jobs: "Cron jobs", skills: "Skill files" }[s]}</span>
                <span class="v">v${cfg.versions[s]}</span>
              </div>`).join("")}
          </div>
          <p class="section-note" style="margin-top:12px">${icon("info")}<span>The ESP32 pulls this config on boot and every ${humanInterval(dev.sync_interval_s)}. Sections are only applied when their version increases, so unchanged files are never rewritten.</span></p>
        </div>
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("server")} Google MCP servers</div>
          <span class="badge">${connected}/${servers.length} connected</span>
        </div>
        ${servers.length ? `<div class="stack" style="gap:9px">${servers.map(([name, s]) => `
          <div class="row-item">
            <div class="grow">
              <div class="name">${esc(name.replace("google-", "Google "))}</div>
              <div class="meta">${s.tool_count} tools</div>
            </div>
            <span class="badge ${s.connected ? "emerald" : "amber"}">${s.connected ? "Connected" : "Auth required"}</span>
          </div>`).join("")}</div>`
        : `<div class="empty">${icon("server")}<div>No MCP servers reported yet.</div></div>`}
      </div>
    </div>`;

  runCountups(root);

  /* Google login stays on the page: OAuth runs in a popup, we poll
     /auth/status, then reconnect the MCP servers with the fresh token. */
  const loginBtn = root.querySelector("#auth-login");
  const relinkBtn = root.querySelector("#auth-relink");
  (loginBtn || relinkBtn)?.addEventListener("click", loginWithGoogle);
}

let authPollTimer = null;

async function loginWithGoogle() {
  /* Snapshot the stored token's timestamp so we can tell when the popup
     actually wrote a NEW token (the old one may be a dead refresh token
     that still reports authenticated=true). */
  let seenUpdatedAt = null;
  try { seenUpdatedAt = (await api("/auth/status")).token_updated_at || null; } catch { /* backend down */ }

  const popup = window.open("/auth/google", "esp_google_oauth", "width=520,height=640");
  if (!popup) {
    toast("Popup blocked — allow popups, or open /auth/google directly", true);
    return;
  }
  if (authPollTimer) clearInterval(authPollTimer);
  const started = Date.now();
  toast("Waiting for Google login in the popup…");

  authPollTimer = setInterval(async () => {
    try {
      const st = await api("/auth/status");
      const tokenChanged = st.token_updated_at && st.token_updated_at !== seenUpdatedAt;
      if (st.authenticated && (seenUpdatedAt === null || tokenChanged)) {
        clearInterval(authPollTimer);
        authPollTimer = null;
        /* Popup is on the Google consent page or already back to our callback
           — in either case the user closes it. No cross-origin interaction. */
        state.auth = st;
        toast("Google linked ✓ — reconnecting MCP servers…");
        await api("/api/mcp/reconnect", { method: "POST", body: "{}" });
        await refreshHealth();
        if (state.view === "overview") navigate();
        return;
      }
      if (Date.now() - started > 180000) {
        clearInterval(authPollTimer);
        authPollTimer = null;
        toast("Google login timed out — try again", true);
      }
    } catch { /* transient network error — keep polling */ }
  }, 1000);
}

function updateServerChip() {
  const ok = !!state.health;
  $("#server-dot").className = `dot ${ok ? "ok" : "bad"}`;
  $("#server-text").textContent = ok ? "Server online" : "Server offline";
  const navDot = $("#nav-status-dot");
  if (navDot) navDot.className = `nav-dot ${ok ? "online" : ""}`;
}

/* ── View: Profile (USER.md) ──────────────────────────────── */

const LANGUAGES = ["Chinese / English", "English", "Chinese", "Bahasa Malaysia", "English / Bahasa Malaysia"];

function parseUserMd(md) {
  const fields = { name: "", language: "", timezone: "", notes: [] };
  const lines = md.split("\n");
  for (const line of lines) {
    const m = line.match(/^-\s*(Name|Language|Timezone):\s*(.*)$/i);
    if (m) {
      const key = m[1].toLowerCase();
      fields[key] = m[2].trim();
    } else if (line.trim() === "# User Profile" || (line.trim() === "" && fields.notes.length === 0 && !fields.name && !fields.language && !fields.timezone)) {
      continue; // skip header and leading blank lines
    } else {
      fields.notes.push(line);
    }
  }
  while (fields.notes.length && fields.notes[0].trim() === "") fields.notes.shift();
  while (fields.notes.length && fields.notes[fields.notes.length - 1].trim() === "") fields.notes.pop();
  return fields;
}

function buildUserMd(f) {
  const parts = ["# User Profile", "", `- Name: ${f.name || "(not set)"}`, `- Language: ${f.language}`, `- Timezone: ${f.timezone}`];
  const notes = f.notes.join("\n").trim();
  if (notes) parts.push("", notes);
  return parts.join("\n") + "\n";
}

async function renderProfile(root) {
  if (!state.config) state.config = await api("/api/config");
  refreshVersionChip();
  const f = parseUserMd(state.config.user_md);
  const knownTz = state.config.device.timezone;

  root.innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("user")} User profile</div>
          <span class="badge">USER.md · v${state.config.versions.user}</span>
        </div>
        <div class="grid cols-2" style="margin-bottom:14px">
          <div class="field">
            <label for="pf-name">Name</label>
            <input type="text" id="pf-name" value="${esc(f.name === "(not set)" ? "" : f.name)}" placeholder="Jeff">
          </div>
          <div class="field">
            <label for="pf-lang">Language</label>
            <select id="pf-lang">
              ${LANGUAGES.map((l) => `<option ${l === f.language ? "selected" : ""}>${esc(l)}</option>`).join("")}
              ${LANGUAGES.includes(f.language) || !f.language ? "" : `<option selected>${esc(f.language)}</option>`}
            </select>
          </div>
        </div>
        <div class="field" style="margin-bottom:14px">
          <label for="pf-tz">Timezone (in profile)</label>
          <select id="pf-tz">
            <option value="">(not set)</option>
            ${TIMEZONES.map((t) => `<option value="${t}" ${t === f.timezone ? "selected" : ""}>${t}</option>`).join("")}
            <option value="${esc(knownTz)}" ${f.timezone === knownTz ? "selected" : ""}>${esc(knownTz)} (device setting)</option>
          </select>
          <span class="hint">Tip: keep this the same as the Device timezone so scheduled jobs fire at the right time.</span>
        </div>
        <div class="field">
          <label for="pf-notes">Extra context for the agent</label>
          <textarea id="pf-notes" rows="5" placeholder="- Occupation: student&#10;- Prefers brief answers in the morning">${esc(f.notes.join("\n"))}</textarea>
          <span class="hint">Anything you want MimiClaw to know about you. Written into USER.md as-is.</span>
        </div>
        <div style="display:flex;justify-content:flex-end;gap:10px;margin-top:16px">
          <button class="btn" id="pf-revert">Revert</button>
          <button class="btn primary" id="pf-save">${icon("save")} Save profile</button>
        </div>
      </div>
      <p class="section-note">${icon("info")}<span>Saved content is pulled by the device at its next sync (boot or every ${humanInterval(state.config.device.sync_interval_s)}).</span></p>
    </div>`;

  const collect = () => buildUserMd({
    name: $("#pf-name").value.trim(),
    language: $("#pf-lang").value,
    timezone: $("#pf-tz").value,
    notes: $("#pf-notes").value.split("\n"),
  });

  ["pf-name", "pf-lang", "pf-tz", "pf-notes"].forEach((id) =>
    $(`#${id}`).addEventListener("input", () => setDirty("profile", collect() !== state.config.user_md)));
  $("#pf-name").addEventListener("input", () => setDirty("profile", collect() !== state.config.user_md));

  $("#pf-revert").onclick = () => navigate();
  $("#pf-save").onclick = async () => {
    const btn = $("#pf-save");
    btn.disabled = true;
    try {
      const md = collect();
      const res = await api("/api/ui/profile", { method: "PUT", body: JSON.stringify({ user_md: md }) });
      state.config.user_md = md;
      state.config.versions.user = res.versions.user;
      markSaved("profile");
      refreshVersionChip();
      toast("Profile saved — device will pull it");
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
  };
}

/* ── View: Personality (SOUL.md) ──────────────────────────── */

const DEFAULT_SOUL = `I am MimiClaw, a personal AI assistant running on an ESP32-S3 microcontroller.

Personality:
- Helpful and friendly
- Concise and to the point
- Curious and eager to learn

Values:
- Accuracy over speed
- User privacy and safety
- Transparency in actions

Behavior Rules:
- **Take Initiative**: If the user asks you to perform an action (like sending an email) and leaves some details vague (e.g. "just write a greeting"), DO NOT ask follow-up questions. Use your best judgment to invent reasonable content, fill in the blanks, and execute the tool immediately.
- **Stop Yapping**: Be extremely concise. Avoid phrases like "I can do that for you!" or "Here are your options." Just do the task.
`;

async function renderPersonality(root) {
  if (!state.config) state.config = await api("/api/config");
  refreshVersionChip();

  root.innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("sparkles")} Personality & behavior</div>
          <span class="badge">SOUL.md · v${state.config.versions.soul}</span>
        </div>
        <p class="card-sub">This is the first thing the agent reads in every prompt. Personality, values, and hard behavior rules.</p>
        <div class="field">
          <textarea id="soul-editor" class="mono" rows="16" spellcheck="false">${esc(state.config.soul_md)}</textarea>
          <div class="editor-meta">
            <span>Markdown — headings, lists and bold all work</span>
            <span class="char-count"><span id="soul-count">0</span> chars</span>
          </div>
        </div>
        <div style="display:flex;justify-content:flex-end;gap:10px;margin-top:14px">
          <button class="btn" id="soul-reset">Reset to default</button>
          <button class="btn primary" id="soul-save">${icon("save")} Save personality</button>
        </div>
      </div>
      <p class="section-note">${icon("info")}<span>Strong, specific rules work best on a small model — e.g. "Stop Yapping" and "Take Initiative" above visibly change how the device answers.</span></p>
    </div>`;

  const editor = $("#soul-editor");
  const count = () => { $("#soul-count").textContent = editor.value.length; };
  count();
  editor.addEventListener("input", () => {
    count();
    setDirty("soul", editor.value !== state.config.soul_md);
  });

  $("#soul-reset").onclick = () => {
    editor.value = DEFAULT_SOUL;
    count();
    setDirty("soul", editor.value !== state.config.soul_md);
  };

  $("#soul-save").onclick = async () => {
    const btn = $("#soul-save");
    btn.disabled = true;
    try {
      const res = await api("/api/ui/soul", { method: "PUT", body: JSON.stringify({ soul_md: editor.value }) });
      state.config.soul_md = editor.value;
      state.config.versions.soul = res.versions.soul;
      markSaved("soul");
      refreshVersionChip();
      toast("Personality saved — device will pull it");
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
  };
}

/* ── View: Memory (MEMORY.md) ─────────────────────────────── */

async function renderMemory(root) {
  if (!state.config) state.config = await api("/api/config");
  refreshVersionChip();

  root.innerHTML = `
    <div class="stack">
      <div class="banner">${icon("alert")}<span>The agent also writes to this file when it consolidates memories on the device. Your edit is applied at the device's next sync — after that, the agent may reorganize or extend it on its own.</span></div>
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("database")} Long-term memory</div>
          <span class="badge">MEMORY.md · v${state.config.versions.memory}</span>
        </div>
        <div class="field">
          <textarea id="mem-editor" class="mono" rows="12" spellcheck="false">${esc(state.config.memory_md)}</textarea>
          <div class="editor-meta">
            <span>One fact per line works best (bulleted)</span>
            <span class="char-count"><span id="mem-count">0</span> chars</span>
          </div>
        </div>
        <div class="field" style="margin-top:12px">
          <label for="mem-note">Quick note</label>
          <div style="display:flex;gap:10px">
            <input type="text" id="mem-note" placeholder="e.g. FYP demo is on Sept 11">
            <button class="btn" id="mem-append">Append</button>
          </div>
          <span class="hint">Adds a dated bullet at the end of MEMORY.md without opening the editor.</span>
        </div>
        <div style="display:flex;justify-content:space-between;gap:10px;margin-top:16px">
          <button class="btn danger-ghost" id="mem-clear">${icon("trash")} Clear memory</button>
          <button class="btn primary" id="mem-save">${icon("save")} Save memory</button>
        </div>
      </div>
    </div>`;

  const editor = $("#mem-editor");
  const count = () => { $("#mem-count").textContent = editor.value.length; };
  count();
  editor.addEventListener("input", () => {
    count();
    setDirty("memory", editor.value !== state.config.memory_md);
  });

  $("#mem-append").onclick = () => {
    const input = $("#mem-note");
    const note = input.value.trim();
    if (!note) return;
    const date = new Date().toISOString().slice(0, 10);
    editor.value = editor.value.replace(/\s*$/, "") + `\n- ${note} (added ${date})\n`;
    input.value = "";
    count();
    setDirty("memory", editor.value !== state.config.memory_md);
    $("#mem-save").focus();
  };

  $("#mem-clear").onclick = async () => {
    const ok = await confirmDialog({
      title: "Clear long-term memory?",
      message: "MEMORY.md on the device will be replaced with an empty memory file at the next sync. The agent will start learning from scratch.",
      okLabel: "Clear memory",
    });
    if (!ok) return;
    editor.value = "# Long-term Memory\n\n(empty - MimiClaw will write memories here as it learns)\n";
    count();
    setDirty("memory", editor.value !== state.config.memory_md);
  };

  $("#mem-save").onclick = async () => {
    const btn = $("#mem-save");
    btn.disabled = true;
    try {
      const res = await api("/api/ui/memory", { method: "PUT", body: JSON.stringify({ memory_md: editor.value }) });
      state.config.memory_md = editor.value;
      state.config.versions.memory = res.versions.memory;
      markSaved("memory");
      refreshVersionChip();
      toast("Memory saved — device will pull it");
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
  };
}

/* ── View: Schedule (cron jobs) ───────────────────────────── */

async function renderSchedule(root) {
  if (!state.config) state.config = await api("/api/config");
  refreshVersionChip();
  const jobs = state.config.jobs;

  const scheduleText = (j) => j.kind === "every"
    ? `every ${humanInterval(j.interval_s)}`
    : `${new Date(j.at_epoch * 1000).toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" })}${j.delete_after_run ? " · once" : ""}`;

  const destText = (j) => j.channel === "telegram"
    ? `Telegram · chat ${j.chat_id}`
    : "Device (OLED + voice)";

  root.innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("calendar")} Scheduled jobs</div>
          <div style="display:flex;gap:10px;align-items:center">
            <span class="badge">${jobs.length}/16 slots</span>
            <button class="btn primary" id="job-add">${icon("plus")} New job</button>
          </div>
        </div>
        <p class="card-sub">Each job sends a message to the agent at its trigger time. MimiClaw then acts on it like any other request and replies through the chosen channel.</p>
        <div class="stack" style="gap:10px" id="job-list">
          ${jobs.length ? jobs.map((j) => `
            <div class="row-item" data-id="${esc(j.id)}">
              <label class="switch" title="${j.enabled ? "Enabled" : "Disabled"}">
                <input type="checkbox" data-action="toggle" ${j.enabled ? "checked" : ""}>
                <span class="track"></span>
              </label>
              <div class="grow">
                <div class="name">${esc(j.name)}</div>
                <div class="meta">
                  <span class="badge ${j.kind === "every" ? "blue" : "emerald"}">${scheduleText(j)}</span>
                  &nbsp;<span>${esc(destText(j))}</span>
                </div>
              </div>
              <div class="actions">
                <button class="icon-btn" data-action="edit" title="Edit">${icon("pencil")}</button>
                <button class="icon-btn danger" data-action="delete" title="Delete">${icon("trash")}</button>
              </div>
            </div>`).join("")
          : `<div class="empty">${icon("calendar")}<div>No jobs yet. Add a morning briefing or a one-time reminder.</div></div>`}
        </div>
      </div>
      <div class="banner info">${icon("info")}<span>Jobs created here are pushed to the device as <span class="mono">cron.json</span>. Jobs MimiClaw creates by itself (e.g. "remind me at 5pm" by voice) live only on the device and are preserved during sync — they never get overwritten by this page.</span></div>
    </div>`;

  $("#job-add").onclick = () => openJobModal(null);
  $("#job-list").addEventListener("click", async (e) => {
    const row = e.target.closest(".row-item");
    if (!row) return;
    const id = row.dataset.id;
    const job = jobs.find((j) => j.id === id);
    const action = e.target.closest("[data-action]")?.dataset.action;
    if (action === "edit") {
      openJobModal(job);
    } else if (action === "delete") {
      const ok = await confirmDialog({
        title: `Delete “${job.name}”?`,
        message: "The job will be removed from the device at its next sync.",
        okLabel: "Delete job",
      });
      if (!ok) return;
      try {
        const res = await api(`/api/ui/jobs/${id}`, { method: "DELETE" });
        state.config.jobs = res.jobs;
        state.config.versions.jobs = res.versions.jobs;
        refreshVersionChip();
        toast("Job deleted");
        navigate();
      } catch (err) { toast(err.message, true); }
    }
  });
  $("#job-list").addEventListener("change", async (e) => {
    if (e.target.dataset.action !== "toggle") return;
    const row = e.target.closest(".row-item");
    try {
      const res = await api(`/api/ui/jobs/${row.dataset.id}`, { method: "PUT", body: JSON.stringify({ enabled: e.target.checked }) });
      state.config.jobs = res.jobs;
      state.config.versions.jobs = res.versions.jobs;
      refreshVersionChip();
      toast(e.target.checked ? "Job enabled" : "Job disabled");
    } catch (err) {
      toast(err.message, true);
      e.target.checked = !e.target.checked;
    }
  });
}

/* Job editor modal */

let editingJobId = null;

function openJobModal(job) {
  editingJobId = job ? job.id : null;
  $("#job-modal-title").textContent = job ? "Edit scheduled job" : "New scheduled job";
  $("#job-name").value = job?.name || "";
  $("#job-message").value = job?.message || "";
  $("#job-channel").value = job?.channel || "system";
  $("#job-chat").value = job && job.channel === "telegram" ? job.chat_id : "";
  $("#job-enabled").checked = job ? job.enabled : true;
  $("#job-delete-after").checked = job?.delete_after_run || false;
  setJobKind(job?.kind || "every");
  if (job?.kind === "every" && job.interval_s) {
    const sel = $("#job-interval");
    if (![...sel.options].some((o) => +o.value === job.interval_s)) {
      const opt = document.createElement("option");
      opt.value = job.interval_s;
      opt.textContent = humanInterval(job.interval_s);
      sel.appendChild(opt);
    }
    sel.value = String(job.interval_s);
  } else {
    $("#job-interval").value = "86400";
  }
  if (job?.kind === "at") {
    const d = new Date(job.at_epoch * 1000);
    const pad = (n) => String(n).padStart(2, "0");
    $("#job-at").value = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
  } else {
    $("#job-at").value = "";
  }
  syncJobFields();
  showModal($("#job-backdrop"));
  $("#job-name").focus();
}

function setJobKind(kind) {
  document.querySelectorAll("#job-kind button").forEach((b) =>
    b.classList.toggle("active", b.dataset.kind === kind));
  $("#field-interval").hidden = kind !== "every";
  $("#field-at").hidden = kind !== "at";
}

function syncJobFields() {
  const isTg = $("#job-channel").value === "telegram";
  const kind = document.querySelector("#job-kind button.active").dataset.kind;
  $("#field-chat").style.display = isTg ? "" : "none";
  $("#job-delete-after").closest(".checkbox-row").style.visibility = kind === "at" ? "visible" : "hidden";
}

function wireJobModal() {
  document.querySelectorAll("#job-kind button").forEach((b) =>
    b.addEventListener("click", () => { setJobKind(b.dataset.kind); syncJobFields(); }));
  $("#job-channel").addEventListener("change", syncJobFields);
  $("#job-close").onclick = $("#job-cancel").onclick = () =>
    $("#job-backdrop").classList.remove("open");

  $("#job-save").onclick = async () => {
    const kind = document.querySelector("#job-kind button.active").dataset.kind;
    const payload = {
      name: $("#job-name").value,
      kind,
      message: $("#job-message").value,
      channel: $("#job-channel").value,
      chat_id: $("#job-chat").value.trim() || "cron",
      enabled: $("#job-enabled").checked,
      delete_after_run: kind === "at" ? $("#job-delete-after").checked : false,
    };
    if (kind === "every") payload.interval_s = +$("#job-interval").value;
    else payload.at_local = $("#job-at").value;

    if (!payload.name.trim()) { toast("Job name is required", true); return; }
    if (!payload.message.trim()) { toast("Message is required", true); return; }
    if (kind === "at" && !payload.at_local) { toast("Pick a date and time", true); return; }
    if (payload.channel === "telegram" && !$("#job-chat").value.trim()) {
      toast("Telegram chat ID is required for Telegram jobs", true); return;
    }

    const btn = $("#job-save");
    btn.disabled = true;
    try {
      const res = editingJobId
        ? await api(`/api/ui/jobs/${editingJobId}`, { method: "PUT", body: JSON.stringify(payload) })
        : await api("/api/ui/jobs", { method: "POST", body: JSON.stringify(payload) });
      state.config.jobs = res.jobs;
      state.config.versions.jobs = res.versions.jobs;
      refreshVersionChip();
      $("#job-backdrop").classList.remove("open");
      toast(editingJobId ? "Job updated" : "Job created");
      if (state.view === "schedule") navigate();
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
};
	}

/* ── View: Skills (web-managed skill markdown files) ──────── */

function humanBytes(n) {
  return n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`;
}

async function renderSkills(root) {
  if (!state.config) state.config = await api("/api/config");
  /* Skills content is intentionally excluded from /api/config (it would
     overflow the device's 32 KB sync buffer) — fetch the full list here. */
  const list = await api("/api/ui/skills");
  state.config.skills = list.skills;
  refreshVersionChip();
  const skills = state.config.skills || [];

  root.innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("book")} Skills</div>
          <div style="display:flex;gap:10px;align-items:center">
            <span class="badge">${skills.length}/16 slots</span>
            <button class="btn primary" id="skill-add">${icon("plus")} New skill</button>
          </div>
        </div>
        <p class="card-sub">Each skill is a markdown playbook. The agent sees every enabled skill's title and summary in its prompt and can read the full file on demand.</p>
        <div class="stack" style="gap:10px" id="skill-list">
          ${skills.length ? skills.map((s) => `
            <div class="row-item" data-id="${esc(s.id)}">
              <label class="switch" title="${s.enabled ? "Enabled" : "Disabled"}">
                <input type="checkbox" data-action="toggle" ${s.enabled ? "checked" : ""}>
                <span class="track"></span>
              </label>
              <div class="grow">
                <div class="name">${esc(s.name)}</div>
                <div class="meta">
                  <span class="badge blue mono">${esc(s.filename)}</span>
                  &nbsp;<span>${humanBytes(new Blob([s.content]).size)}</span>
                </div>
              </div>
              <div class="actions">
                <button class="icon-btn" data-action="edit" title="Edit">${icon("pencil")}</button>
                <button class="icon-btn danger" data-action="delete" title="Delete">${icon("trash")}</button>
              </div>
            </div>`).join("")
          : `<div class="empty">${icon("book")}<div>No skills yet. Add a playbook the agent can follow.</div></div>`}
        </div>
      </div>
      <div class="banner info">${icon("info")}<span>Skills created here are written to <span class="mono">/spiffs/skills/</span> on the device. Skills MimiClaw creates by itself (via <span class="mono">write_file</span>) and the skills baked into the firmware image are never touched by this page. A skill must start with a <span class="mono"># Title</span> heading — that's what the device parses.</span></div>
    </div>`;

  $("#skill-add").onclick = () => openSkillModal(null);
  $("#skill-list").addEventListener("click", async (e) => {
    const row = e.target.closest(".row-item");
    if (!row) return;
    const id = row.dataset.id;
    const skill = skills.find((s) => s.id === id);
    const action = e.target.closest("[data-action]")?.dataset.action;
    if (action === "edit") {
      openSkillModal(skill);
    } else if (action === "delete") {
      const ok = await confirmDialog({
        title: `Delete “${skill.name}”?`,
        message: `The file ${skill.filename} will be removed from the device at its next sync.`,
        okLabel: "Delete skill",
      });
      if (!ok) return;
      try {
        const res = await api(`/api/ui/skills/${id}`, { method: "DELETE" });
        state.config.skills = res.skills;
        state.config.versions.skills = res.versions.skills;
        refreshVersionChip();
        toast("Skill deleted");
        navigate();
      } catch (err) { toast(err.message, true); }
    }
  });
  $("#skill-list").addEventListener("change", async (e) => {
    if (e.target.dataset.action !== "toggle") return;
    const row = e.target.closest(".row-item");
    try {
      const res = await api(`/api/ui/skills/${row.dataset.id}`, { method: "PUT", body: JSON.stringify({ enabled: e.target.checked }) });
      state.config.skills = res.skills;
      state.config.versions.skills = res.versions.skills;
      refreshVersionChip();
      toast(e.target.checked ? "Skill enabled" : "Skill disabled");
    } catch (err) {
      toast(err.message, true);
      e.target.checked = !e.target.checked;
    }
  });
}

/* Skill editor modal */

let editingSkillId = null;

function openSkillModal(skill) {
  editingSkillId = skill ? skill.id : null;
  $("#skill-modal-title").textContent = skill ? "Edit skill" : "New skill";
  $("#skill-name").value = skill?.name || "";
  $("#skill-content").value = skill?.content || "";
  $("#skill-enabled").checked = skill ? skill.enabled : true;
  updateSkillCount();
  showModal($("#skill-backdrop"));
  $("#skill-name").focus();
}

function updateSkillCount() {
  const v = $("#skill-content").value;
  $("#skill-count").textContent = new Blob([v]).size;
}

function wireSkillModal() {
  $("#skill-content").addEventListener("input", updateSkillCount);
  $("#skill-close").onclick = $("#skill-cancel").onclick = () =>
    $("#skill-backdrop").classList.remove("open");

  $("#skill-save").onclick = async () => {
    const payload = {
      name: $("#skill-name").value,
      content: $("#skill-content").value,
      enabled: $("#skill-enabled").checked,
    };
    if (!payload.name.trim()) { toast("Skill name is required", true); return; }
    if (!payload.content.trim()) { toast("Skill content is required", true); return; }
    if (!payload.content.trimStart().startsWith("#")) {
      toast("Skill must start with a '# Title' heading", true); return;
    }

    const btn = $("#skill-save");
    btn.disabled = true;
    try {
      const res = editingSkillId
        ? await api(`/api/ui/skills/${editingSkillId}`, { method: "PUT", body: JSON.stringify(payload) })
        : await api("/api/ui/skills", { method: "POST", body: JSON.stringify(payload) });
      state.config.skills = res.skills;
      state.config.versions.skills = res.versions.skills;
      refreshVersionChip();
      $("#skill-backdrop").classList.remove("open");
      toast(editingSkillId ? "Skill updated" : "Skill created");
      if (state.view === "skills") navigate();
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
  };
}

/* ── Dress-up modal (persistent, defined in index.html) ───────── */

function wireDressModal() {
  $("#av-dress").addEventListener("click", (e) => {
    const btn = e.target.closest("[data-key]");
    if (!btn) return;
    setAvatarOpts({ [btn.dataset.key]: btn.dataset.val });
  });
  $("#dress-close").onclick = () => $("#dress-backdrop").classList.remove("open");
}

/* ── View: Agent showcase — 3D toy robot (three.js + RobotExpressive) ── */

const TOY_BODY = ["10B981", "3B82F6", "8B5CF6", "F59E0B", "EF4444", "EC4899", "14B8A6", "64748B"];
const TOY_HAT_COLOR = ["262E33", "FF6B6B", "FFD93D", "65C9FF", "C084FC", "A7FFC4"];

const TOY_HATS = ["none", "tophat", "party", "propeller", "beanie"];
const TOY_GLASSES = ["none", "round", "visor"];
const TOY_ANTENNA = ["none", "ball"];
const TOY_BOWTIE = ["none", "on"];
const TOY_MOODS = ["idle", "dance", "wave", "jump", "thumbsup"];

const TOY_LABELS = {
  none: "None", tophat: "Top hat", party: "Party cone", propeller: "Propeller",
  beanie: "Beanie", round: "Round specs", visor: "Visor", ball: "Ball",
  on: "On", idle: "Idle", dance: "Dance", wave: "Wave", jump: "Jump",
  thumbsup: "Thumbs up",
};

function avLabel(v) { return TOY_LABELS[v] || v; }
function pick(arr) { return arr[Math.floor(Math.random() * arr.length)]; }
function swatch(hex) { return `<span class="swatch" style="background:#${hex}"></span>`; }

/**
 * Auto-style: the robot dresses itself from the agent's own settings
 * (SOUL.md personality keywords, memory facts, scheduled jobs, user name).
 * Returns { opts, traits } — traits explain each mapping in the UI.
 */
function deriveAvatar(cfg) {
  const soul = (cfg.soul_md || "").toLowerCase();
  const user = (cfg.user_md || "").toLowerCase();
  const memLines = (cfg.memory_md || "").split("\n").filter((l) => l.trim().startsWith("-")).length;
  const jobs = (cfg.jobs || []).length;
  const nameMatch = user.match(/- name:\s*(.+)/);
  const name = (nameMatch ? nameMatch[1] : (cfg.device?.name || "MimiClaw")).replace(/[()]/g, "").trim() || "MimiClaw";

  let hash = 0;
  for (const c of name) hash = (hash * 31 + c.charCodeAt(0)) >>> 0;

  const opts = {
    bodyColor: TOY_BODY[hash % TOY_BODY.length],
    headColor: TOY_BODY[(hash >> 3) % TOY_BODY.length],
    hat: "none",
    hatColor: TOY_HAT_COLOR[hash % TOY_HAT_COLOR.length],
    glasses: "none",
    antenna: "ball",
    bowtie: "none",
    mood: "idle",
  };

  const traits = [`"${name}" → factory paint job`];

  if (/friendly|helpful/.test(soul)) { opts.mood = "wave"; traits.push("friendly → waves hello"); }
  if (/curious|eager to learn/.test(soul)) traits.push("curious → antenna up");
  if (/concise|stop yapping|to the point/.test(soul)) { opts.antenna = "none"; traits.push("concise → stripped to basics"); }
  if (/professional|formal|business/.test(soul)) { opts.bowtie = "on"; opts.bodyColor = "334155"; traits.push("professional → bow tie, slate paint"); }
  if (/playful|fun|joke/.test(soul)) { opts.mood = "dance"; opts.hat = "party"; traits.push("playful → party cone & dance"); }
  if (/privacy|safety|secure/.test(soul)) { opts.glasses = "visor"; traits.push("privacy-first → visor down"); }
  if (memLines >= 3 && opts.glasses === "none") { opts.glasses = "round"; traits.push(`${memLines} memories → wise round specs`); }
  if (jobs > 0) { opts.hat = "tophat"; traits.push(`${jobs} scheduled job${jobs > 1 ? "s" : ""} → top hat`); }

  return { opts, traits };
}

function effectiveAvatarOpts() {
  const base = deriveAvatar(state.config).opts;
  return state.avatar.mode === "custom" ? { ...base, ...(state.avatar.opts || {}) } : base;
}

function renderAvatarStage() {
  const box = $("#av-3d");
  if (!box) return;
  if (!window.ToyAgent) {
    box.innerHTML = `<div style="display:grid;place-items:center;height:100%;font-size:5rem">🤖</div>`;
    return;
  }
  if (!box.querySelector("canvas")) {
    ToyAgent.setOpts(effectiveAvatarOpts()); // stash while the model loads
    ToyAgent.mount(box, { zoom: 1.18 }).catch((err) => {
      console.warn("3D stage failed, showing fallback:", err);
      box.innerHTML = `<div style="display:grid;place-items:center;height:100%;font-size:5rem">🤖</div>`;
    });
  } else {
    ToyAgent.setOpts(effectiveAvatarOpts()); // scene already live — just update
  }
}

let avatarSaveTimer = null;
function persistAvatar() {
  clearTimeout(avatarSaveTimer);
  avatarSaveTimer = setTimeout(async () => {
    try {
      const res = await api("/api/ui/avatar", {
        method: "PUT",
        body: JSON.stringify({ mode: state.avatar.mode, opts: state.avatar.opts || {} }),
      });
      state.config.versions.avatar = res.versions.avatar;
      refreshVersionChip();
      const badge = $("#av-badge-text");
      if (badge && !badge.dataset.busy) {
        const prev = badge.textContent;
        badge.textContent = "saved ✓";
        setTimeout(() => { badge.textContent = prev; }, 900);
      }
    } catch (err) { toast(err.message, true); }
  }, 400);
}

function updateAvatarBadge() {
  const badge = $("#av-badge-text");
  if (badge) {
    badge.className = `badge ${state.avatar.mode === "custom" ? "blue" : "emerald"}`;
    badge.textContent = state.avatar.mode === "custom" ? "Custom style" : "Auto style · from your settings";
  }
}

function setAvatarOpts(patch) {
  state.avatar.mode = "custom";
  state.avatar.opts = { ...(state.avatar.opts || {}), ...patch };
  persistAvatar();
  renderAvatarStage();
  updateAvatarBadge();
  refreshDressGrid();
  const traitsEl = $("#av-traits");
  if (traitsEl && !traitsEl.querySelector(".trait.manual")) {
    traitsEl.insertAdjacentHTML("beforeend",
      `<span class="trait manual" style="border-color:var(--blue-border);background:var(--blue-soft);color:#93c5fd">manual overrides active</span>`);
  }
}

function buildDressGrid() {
  const eff = effectiveAvatarOpts();
  const valueGroup = (label, key, values, current) => `
    <div class="dress-group">
      <div class="dress-label">${label}</div>
      <div class="dress-row">
        ${values.map((v) => `
          <button class="chip-btn ${current === v ? "active" : ""}" data-key="${key}" data-val="${v}">${avLabel(v)}</button>`).join("")}
      </div>
    </div>`;
  const colorGroup = (label, key, palette, current) => `
    <div class="dress-group">
      <div class="dress-label">${label}</div>
      <div class="dress-row">
        ${palette.map((c) => `
          <button class="chip-btn ${String(current || "").toUpperCase() === c ? "active" : ""}" data-key="${key}" data-val="${c}" title="#${c}">${swatch(c)}</button>`).join("")}
      </div>
    </div>`;

  return `
    ${valueGroup("Hat", "hat", TOY_HATS, eff.hat)}
    ${valueGroup("Glasses", "glasses", TOY_GLASSES, eff.glasses)}
    ${valueGroup("Antenna", "antenna", TOY_ANTENNA, eff.antenna)}
    ${valueGroup("Bow tie", "bowtie", TOY_BOWTIE, eff.bowtie)}
    ${valueGroup("Mood", "mood", TOY_MOODS, eff.mood)}
    ${colorGroup("Body paint", "bodyColor", TOY_BODY, eff.bodyColor)}
    ${colorGroup("Head paint", "headColor", TOY_BODY, eff.headColor)}
    ${colorGroup("Hat color", "hatColor", TOY_HAT_COLOR, eff.hatColor)}`;
}

function refreshDressGrid() {
  const grid = $("#av-dress");
  if (grid) grid.innerHTML = buildDressGrid();
}

async function renderAgent(root) {
  if (!state.config) state.config = await api("/api/config");
  state.avatar = await api("/api/ui/avatar").catch(() => ({ mode: "auto", opts: {} }));
  refreshVersionChip();

  const dev = state.config.device;
  const derived = deriveAvatar(state.config);
  const name = esc(dev.name || "MimiClaw");

  root.innerHTML = `
    <div class="stack">
      <div class="card agent-hero">
        <div class="stage stage-hero" id="av-stage">
          <div class="stage-spot"></div>
          <span class="stage-badge" id="av-badge"><span class="badge" id="av-badge-text"></span></span>
          <span class="stage-name">${name}</span>
          <div class="stage-3d" id="av-3d"></div>
          <div class="stage-podium"></div>
        </div>
        <div class="agent-actions">
          <button class="btn primary" id="av-customize">${icon("bot")} Customize appearance</button>
          <button class="btn" id="av-dice">${icon("dice")} Surprise me</button>
          <button class="btn" id="av-reset">${icon("refresh")} Reset to auto</button>
        </div>
      </div>

      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("spark")} Style traits from your settings</div>
        </div>
        <p class="card-sub">The robot reads its own configuration and suits up accordingly — change SOUL.md, memories or schedules and it re-outfits at the next sync.</p>
        <div class="trait-list" id="av-traits">
          ${derived.traits.map((t) => `<span class="trait">${icon("spark")}${esc(t)}</span>`).join("")}
          ${state.avatar.mode === "custom" ? '<span class="trait manual" style="border-color:var(--blue-border);background:var(--blue-soft);color:#93c5fd">manual overrides active</span>' : ""}
        </div>
        <p class="section-note" style="margin-top:14px">${icon("info")}<span>Drag the robot to spin it. Auto style re-derives whenever your settings change — "Reset to auto" hands styling back to the settings.</span></p>
      </div>
    </div>`;

  updateAvatarBadge();
  renderAvatarStage();
  refreshDressGrid(); // fill the persistent dress-up modal with current options

  $("#av-customize").onclick = () => showModal($("#dress-backdrop"));

  $("#av-dice").onclick = () => {
    const keep = effectiveAvatarOpts();
    const r30 = () => Math.random() < 0.3;
    const patch = {
      bodyColor: pick(TOY_BODY),
      headColor: pick(TOY_BODY),
      hat: r30() ? pick(TOY_HATS.slice(1)) : "none",
      hatColor: pick(TOY_HAT_COLOR),
      glasses: r30() ? pick(TOY_GLASSES.slice(1)) : "none",
      antenna: Math.random() < 0.5 ? "ball" : "none",
      bowtie: r30() ? "on" : "none",
      mood: keep.mood || "idle",
    };
    setAvatarOpts(patch);
    if (window.ToyAgent) ToyAgent.play("Dance");
    anim("#av-stage", { rotate: [{ to: -1.6 }, { to: 1.4 }, { to: -0.8 }, { to: 0 }] }, { duration: 460, ease: "outQuad" });
  };

  $("#av-reset").onclick = () => {
    state.avatar = { mode: "auto", opts: {} };
    persistAvatar();
    renderAvatarStage();
    updateAvatarBadge();
    refreshDressGrid();
    const traitsEl = $("#av-traits");
    if (traitsEl) {
      traitsEl.innerHTML = `
        ${derived.traits.map((t) => `<span class="trait">${icon("spark")}${esc(t)}</span>`).join("")}`;
    }
  };
}

/* ── View: Device ─────────────────────────────────────────── */

const TIMEZONES = [
  "Asia/Kuala_Lumpur", "Asia/Singapore", "Asia/Jakarta", "Asia/Bangkok",
  "Asia/Manila", "Asia/Hong_Kong", "Asia/Shanghai", "Asia/Taipei",
  "Asia/Seoul", "Asia/Tokyo", "Asia/Kolkata", "Asia/Dubai",
  "Australia/Perth", "Australia/Sydney", "Pacific/Auckland",
  "Europe/London", "Europe/Paris", "Europe/Berlin", "Europe/Moscow",
  "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
  "America/Toronto", "America/Sao_Paulo", "UTC",
];

async function renderDevice(root) {
  if (!state.config) state.config = await api("/api/config");
  refreshVersionChip();
  const dev = state.config.device;
  const browserTz = Intl.DateTimeFormat().resolvedOptions().timeZone;

  root.innerHTML = `
    <div class="stack">
      <div class="card">
        <div class="card-head">
          <div class="card-title">${icon("cpu")} Device settings</div>
          <span class="badge">v${state.config.versions.device}</span>
        </div>
        <div class="grid cols-2" style="margin-bottom:14px">
          <div class="field">
            <label for="dev-name">Device name</label>
            <input type="text" id="dev-name" maxlength="31" value="${esc(dev.name)}">
            <span class="hint">Shown in the dashboard header cards. Max 31 chars.</span>
          </div>
          <div class="field">
            <label for="dev-tz">Timezone</label>
            <select id="dev-tz">
              ${TIMEZONES.map((t) => `<option value="${t}" ${t === dev.timezone ? "selected" : ""}>${t}</option>`).join("")}
              ${TIMEZONES.includes(dev.timezone) ? "" : `<option value="${esc(dev.timezone)}" selected>${esc(dev.timezone)}</option>`}
            </select>
            <span class="hint">Your browser is in ${esc(browserTz)}. Applied on the device via TZ so schedules and timestamps are local.</span>
          </div>
        </div>
        <div class="field" style="margin-bottom:6px">
          <label for="dev-interval">Config sync interval</label>
          <select id="dev-interval">
            ${[60, 300, 900, 1800, 3600].map((s) =>
              `<option value="${s}" ${dev.sync_interval_s === s ? "selected" : ""}>Every ${humanInterval(s)}</option>`).join("")}
            ${[60, 300, 900, 1800, 3600].includes(dev.sync_interval_s) ? "" :
              `<option value="${dev.sync_interval_s}" selected>Every ${humanInterval(dev.sync_interval_s)} (custom)</option>`}
          </select>
          <span class="hint">How often the ESP32 pulls this config. It also pulls once at every boot. Changes here take effect after the device's next sync.</span>
        </div>
        <div style="display:flex;justify-content:flex-end;margin-top:14px">
          <button class="btn primary" id="dev-save">${icon("save")} Save device settings</button>
        </div>
      </div>

      <div class="grid cols-2">
        <div class="card">
          <div class="card-head"><div class="card-title">${icon("send")} Messaging channels</div></div>
          <div class="stack" style="gap:9px">
            <div class="row-item">
              <div class="grow">
                <div class="name">Telegram</div>
                <div class="meta">Bot token stored on the device (NVS — set via serial CLI). Cron jobs and the agent can deliver messages to any chat ID.</div>
              </div>
              <span class="badge emerald">on device</span>
            </div>
            <div class="row-item">
              <div class="grow">
                <div class="name">Feishu / Lark</div>
                <div class="meta">App credentials stored on the device (NVS). Replies can be routed to Feishu chats.</div>
              </div>
              <span class="badge emerald">on device</span>
            </div>
            <div class="row-item">
              <div class="grow">
                <div class="name">Google (Gmail · Calendar · Drive)</div>
                <div class="meta">Managed by this backend — link or relink the account from the Overview page.</div>
              </div>
              <span class="badge blue">backend</span>
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-head"><div class="card-title">${icon("refresh")} What the device pulls</div></div>
          <div class="kv">
            <div class="kv-row"><span class="k">USER.md — profile</span><span class="v">v${state.config.versions.user}</span></div>
            <div class="kv-row"><span class="k">SOUL.md — personality</span><span class="v">v${state.config.versions.soul}</span></div>
            <div class="kv-row"><span class="k">MEMORY.md — memory</span><span class="v">v${state.config.versions.memory}</span></div>
            <div class="kv-row"><span class="k">cron.json — schedules</span><span class="v">v${state.config.versions.jobs}</span></div>
            <div class="kv-row"><span class="k">skills/*.md — playbooks</span><span class="v">v${state.config.versions.skills}</span></div>
            <div class="kv-row"><span class="k">Timezone (TZ)</span><span class="v">${esc(state.config.device.posix_tz || "UTC0")}</span></div>
          </div>
          <p class="section-note" style="margin-top:12px">${icon("shield")}<span>Transport is plain HTTP on your LAN with a shared device API key (Bearer). Serve over HTTPS before exposing beyond your network.</span></p>
        </div>
      </div>
    </div>`;

  setDirty("device", false);
  ["dev-name", "dev-tz", "dev-interval"].forEach((id) =>
    $(`#${id}`).addEventListener("input", () => {
      setDirty("device",
        $("#dev-name").value !== dev.name ||
        $("#dev-tz").value !== dev.timezone ||
        +$("#dev-interval").value !== dev.sync_interval_s);
    }));

  $("#dev-save").onclick = async () => {
    const btn = $("#dev-save");
    btn.disabled = true;
    try {
      const res = await api("/api/ui/device", {
        method: "PUT",
        body: JSON.stringify({
          name: $("#dev-name").value.trim(),
          timezone: $("#dev-tz").value,
          sync_interval_s: +$("#dev-interval").value,
        }),
      });
      state.config.device = res.device;
      state.config.versions.device = res.versions.device;
      markSaved("device");
      refreshVersionChip();
      toast("Device settings saved — device will pull them");
    } catch (err) {
      toast(err.message, true);
    } finally { btn.disabled = false; }
  };
}

/* ── View: Tools ──────────────────────────────────────────── */

function toolCategory(name) {
  if (/gmail|thread|draft|message|label/.test(name)) return "gmail";
  if (/event|calendar|time/.test(name)) return "calendar";
  if (/doc|document/.test(name)) return "docs";
  if (/file|drive/.test(name)) return "drive";
  return "system";
}

async function renderTools(root) {
  if (!state.tools) {
    const data = await api("/api/tools");
    state.tools = data.tools || [];
  }
  renderToolsInto(root);
}

function renderToolsInto(root) {
  const q = state.toolSearch.toLowerCase();
  const filtered = state.tools.filter((t) => {
    const matchSearch = t.name.toLowerCase().includes(q) ||
      (t.description || "").toLowerCase().includes(q);
    const matchCat = state.toolFilter === "all" || toolCategory(t.name) === state.toolFilter;
    return matchSearch && matchCat;
  });
  const cats = ["all", "gmail", "calendar", "docs", "drive", "system"];

  root.innerHTML = `
    <div class="card">
      <div class="search-row">
        <input type="text" id="tool-search" placeholder="Search tools…" value="${esc(state.toolSearch)}">
        <div style="display:flex;gap:7px;flex-wrap:wrap">
          ${cats.map((c) => `<button class="filter-btn ${state.toolFilter === c ? "active" : ""}" data-cat="${c}">${c === "all" ? "All" : c[0].toUpperCase() + c.slice(1)}</button>`).join("")}
        </div>
      </div>
      ${filtered.length ? `<div class="tools-grid">${filtered.map((t) => `
        <div class="tool-card">
          <div>
            <div style="display:flex;justify-content:space-between;gap:8px;margin-bottom:7px;align-items:center">
              <span class="tool-name">${esc(t.name)}</span>
              <span class="badge blue">${toolCategory(t.name)}</span>
            </div>
            <div class="tool-desc">${esc(t.description || "No description provided.")}</div>
          </div>
          <div style="display:flex;justify-content:flex-end">
            <button class="btn small" data-schema="${esc(t.name)}">${icon("info")} Schema</button>
          </div>
        </div>`).join("")}</div>`
      : `<div class="empty">${icon("box")}<div>No tools match your search.</div></div>`}
    </div>`;

  $("#tool-search").addEventListener("input", (e) => {
    state.toolSearch = e.target.value;
    renderToolsInto(root);
    $("#tool-search").focus();
    const input = $("#tool-search");
    input.setSelectionRange(input.value.length, input.value.length);
  });
  root.querySelectorAll(".filter-btn").forEach((b) =>
    b.addEventListener("click", () => {
      state.toolFilter = b.dataset.cat;
      renderToolsInto(root);
    }));
  root.querySelectorAll("[data-schema]").forEach((b) =>
    b.addEventListener("click", () => {
      const tool = state.tools.find((t) => t.name === b.dataset.schema);
      $("#schema-title").textContent = tool.name;
      $("#schema-json").textContent = JSON.stringify(tool.input_schema || {}, null, 2);
      showModal($("#schema-backdrop"));
    }));
}

/* ── Boot & global wiring ─────────────────────────────────── */

const RENDERERS = {
  overview: renderOverview,
  agent: renderAgent,
  profile: renderProfile,
  personality: renderPersonality,
  memory: renderMemory,
  schedule: renderSchedule,
  skills: renderSkills,
  device: renderDevice,
  tools: renderTools,
};

async function refreshHealth() {
  try {
    state.health = await api("/health");
  } catch {
    state.health = null;
  }
  updateServerChip();
}

window.addEventListener("hashchange", navigate);
window.addEventListener("beforeunload", (e) => {
  if (Object.values(state.dirty).some(Boolean)) e.preventDefault();
});

// Close modals on backdrop click / Escape
document.querySelectorAll(".modal-backdrop").forEach((bd) =>
  bd.addEventListener("click", (e) => { if (e.target === bd) bd.classList.remove("open"); }));
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape")
    document.querySelectorAll(".modal-backdrop.open").forEach((bd) => bd.classList.remove("open"));
});

buildNav();
wireJobModal();
wireSkillModal();
wireDressModal();

/* Boot intro + ambient motion (skipped silently if anime.js is
   unavailable or the user prefers reduced motion) */
if (MOTION_OK) {
  ANIME.animate(".brand-mark", {
    scale: [0.5, 1], opacity: [0, 1],
  }, { duration: 600, ease: "outBack(1.7)" });
  ANIME.animate(".nav-item", {
    opacity: [0, 1], translateX: [-12, 0],
  }, { delay: ANIME.stagger(45, { start: 120 }), duration: 420, ease: "outQuad" });
  ANIME.animate(".topbar", {
    opacity: [0, 1], translateY: [-8, 0],
  }, { duration: 500, ease: "outQuad", delay: 80 });

  /* Slow ambient drift of the background orbs */
  ANIME.animate(".orb-a", { translateX: [0, 70], translateY: [0, 45] },
    { duration: 17000, ease: "inOutSine", alternate: true, loop: true });
  ANIME.animate(".orb-b", { translateX: [0, -60], translateY: [0, 55] },
    { duration: 21000, ease: "inOutSine", alternate: true, loop: true });
  ANIME.animate(".orb-c", { translateX: [0, 50], translateY: [0, -50] },
    { duration: 25000, ease: "inOutSine", alternate: true, loop: true });
}

navigate();
refreshHealth();
setInterval(refreshHealth, 30000);
