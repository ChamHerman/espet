# Plan: Skills Management + Herman Roadmap Doc

Two deliverables on branch `herman-2`:

1. **Skills management end-to-end** — users create/edit/enable/delete skill markdown files from the dashboard; they sync to the ESP32's `/spiffs/skills/` via the existing pull-based config sync.
2. **`.agent/HERMAN_ROADMAP.MD`** — complete inventory of tools/functions Herman (proactive half) needs, each with rationale and device-vs-backend placement (per your answer: doc only, no implementation of these items this round).

---

## Part 1 — Skills Management

### Design decisions

- **Skills = individual markdown files on SPIFFS**, same format as today (no frontmatter; `# Title` + description paragraph — the existing `skill_loader.c` parser is reused unchanged).
- **Ownership model mirrors the cron-jobs contract**: the device tracks web-synced skill *filenames* in `sync_state.json` (`synced_skills` CSV, analog of `s_synced_ids`). Web skills are written/overwritten; web-deleted skills are unlinked; **agent-created and flashed skills are never touched**.
- **Enabled = present**: disabled skills are simply absent from the sync payload, so the device deletes the file; content stays in the backend. Zero changes needed in `skill_loader.c` or the prompt builder's scan logic.
- **Separate sync endpoint** instead of stuffing `/api/config`: skills content (up to ~150 KB) would blow the device's 32 KB sync buffer and bloat every 5-min poll. Device polls `/api/config` as today (its `versions` now includes `skills`), and only when `versions.skills` increases does it fetch `GET /api/skills/sync` into a transient 192 KB PSRAM buffer.
- **Backend is seeded** with the 4 existing `spiffs_data/skills/*.md` files (daily-briefing, gpio-control, weather, skill-creator) so the dashboard starts populated and web/device content matches.
- **Limits (mirror device reality)**: max 16 skills, 8192 bytes content each, filename = sanitized name `[a-z0-9-]{1,48}` + `.md` (fits SPIFFS 64-char object names). Renaming a skill changes the filename → handled naturally by the filename diff (old file deleted, new written).

### Step 1 — Backend store (`espet-backend/config_store.py`)

- Add constants `SKILL_MAX_COUNT = 16`, `SKILL_MAX_CONTENT = 8192`; seed the 4 default skills as `_DEFAULT_SKILLS` (content copied verbatim from `spiffs_data/skills/`).
- `_default_config()` (line 78): add `"skills": []` and `"versions": {"skills": 1, ...}`. `_load_unlocked()` forward-fills this into existing `data/config.json` automatically.
- New functions following the jobs pattern (lines 223–343):
  - `_skill_filename(name)` — sanitize to slug; `_validate_skill_payload(payload, require_all)` — name/content/enabled checks, dedupe by filename.
  - `add_skill(payload)`, `update_skill(skill_id, payload)`, `delete_skill(skill_id)` — same `(cfg, error)` tuple contract, `_bump(cfg, "skills")` inside the lock, atomic save.
  - `skills_sync_bundle(cfg)` → `{"version": N, "skills": [{id, name, filename, content}, ...]}` (enabled only) for the device endpoint.
- `device_bundle()` unchanged except `versions` now carries `skills` (flows through automatically).

### Step 2 — Backend routes (`espet-backend/api/config_router.py`)

Following the jobs CRUD (lines 106–133), all with `Depends(verify_device_key)`:
- `GET /api/ui/skills`, `POST /api/ui/skills`, `PUT /api/ui/skills/{skill_id}`, `DELETE /api/ui/skills/{skill_id}` → 422/404 mapping identical to jobs.
- `GET /api/skills/sync` (device-facing) → `store.skills_sync_bundle(...)`.

### Step 3 — Firmware (`main/sync/config_sync.c`, `main/agent/context_builder.c`)

No new files → no `CMakeLists.txt` change; `skill_loader.c` untouched.

- `config_sync.c`:
  - Add `SEC_SKILLS` to the section enum + `SEC_NAMES` (lines 26–28) so version tracking works.
  - Add `s_synced_skills[1280]` static; extend `save_state()`/`load_state()` (lines 84–130) to persist it; update the boot log line.
  - **Critical**: in `sync_once()` (line 471 loop) `continue` on `SEC_SKILLS` — otherwise `apply_section` would record the version as applied without applying anything.
  - New `fetch_skills_bundle()`: GET `{backend}/api/skills/sync` with Bearer key into a 192 KB `heap_caps_malloc(MALLOC_CAP_SPIRAM)` buffer (pattern from `fetch_config_bundle`, lines 402–455); overflow-reject like today.
  - New `apply_skills(cJSON *skills_arr)`: unlink previously-synced files missing from the payload; `write_text_file("/spiffs/skills/<filename>", content)` for each incoming skill; rebuild `s_synced_skills`; then `s_applied_version[SEC_SKILLS] = version`. Needs `#include <unistd.h>` for `unlink`.
  - In `sync_once()`: after the section loop, if `versions.skills > applied` → fetch + apply, mark changed.
- `context_builder.c:95`: raise `skills_buf[2048]` → `3072` so a fuller skill catalog isn't silently truncated (system prompt budget is 16 KB).

### Step 4 — Dashboard (`espet-backend/static/`)

Replicating the Schedule view pattern exactly:
- `app.js`: new `VIEWS` entry `{id: "skills", group: "Assistant"}` (line ~191); `renderSkills()` — row list (name, mono filename, size, enabled `.switch`, edit/delete) + "Add skill" button; delegated click/change handlers (lines 710–748 pattern) with `confirmDialog` for delete and toggle-via-PUT; `openSkillModal()`/`wireSkillModal()` following the job modal (lines 753–847); register in `RENDERERS` (line 1305) and boot wiring (line 1339). Add `skills` to the Overview versions card (line 304) and the Device "what the device pulls" rows (line 1186). Version chip needs no change (`Math.max`).
- `index.html`: add `#skill-backdrop` modal (name input, enabled switch, content textarea) modeled on `#job-backdrop` (lines 72–145); bump `app.js?v=14` (and `style.css?v=9` only if CSS changes — the goal is to reuse `.row-item`, `.field`, `.switch`, modal CSS as-is).
- Design tokens/components per `DESIGN.MD` (no new components expected).

### Step 5 — Verification

- Backend: compile check, then boot uvicorn and curl: `/health`, `GET/POST/PUT/DELETE /api/ui/skills`, `GET /api/skills/sync` (device key), confirm `/api/config` shows `versions.skills` and existing config.json forward-fills.
- Dashboard: GUI check of the Skills view (list/add/edit/toggle/delete, toasts, confirm dialog) via the browser tooling.
- Firmware: `idf.py build` if the ESP-IDF toolchain is present locally (otherwise note that CI validates compilation).

### Step 6 — Docs (mandatory session rule)

- `.agent/SYSTEM_OVERVIEW.md`: §3.4 add skills sync subsection, §4.2 API table rows, §4.3 versions example, §5 sync rules (skills ownership model), §6 view table, Recent Changes entry.
- `DESIGN.MD`: only if new CSS was added.

---

## Part 2 — `.agent/HERMAN_ROADMAP.MD` (doc only)

A single reference doc: for every needed tool/function — **what it is, why Herman needs it (tied to his metrics: trigger precision/recall, false-interruption rate, memory retention), where it lives (ESP32 C / backend Python / both), effort, dependencies, priority**. Framed by the constraint you raised: the S3 is hardware-limited, so anything compute-heavy is explicitly pushed to the backend and the device only captures/executes/delivers.

Findings from code analysis that ground the doc (all verified against `herman-2`):

- **P0 — make the proactive loop observable**: (1) `system`-channel delivery currently dead-ends in `ESP_LOGI` (`mimi.c:148`) — no OLED/voice output for cron/heartbeat replies at all; (2) heartbeat is inert — `HEARTBEAT.md` isn't shipped or synced and nothing manages it; (3) no SNTP — clock is wrong after boot until an agent turn calls `get_current_time`, so `at` jobs misfire.
- **P1 — core proactive behaviors**: TTS (`POST /api/tts` backend + I2S TX playback; `audio_play_tone` is a stub, speaker amp wired but TX disabled), STT (`POST /api/transcribe` + PTT release → transcript → agent loop; PCM currently dumps stats to serial), time-of-day cron (`daily HH:MM`; only interval/one-shot exist), quiet hours / DND windows (false-interruption metric), `memory_append` tool + nightly memory consolidation (retention is bounded at 4 KB MEMORY.md + 3 days of notes), camera→agent path (snapshot currently bypasses the agent; video is a frame-dropping mock), mic-RMS/GPIO event triggers to the inbound bus.
- **P2 — research-grade**: backend event push channel, trigger logging + precision/recall instrumentation, scene-watch cron (camera + schedule), periodic memory retrieval tuning.

Each entry will state the concrete current-code gap (file:line), the proposed shape (endpoint signature / tool schema / C function), and why it can't or shouldn't live on the ESP32. Plus a suggested implementation order and which items pair with existing features (e.g. heartbeat needs only a sync section + seed file to activate).