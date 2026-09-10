# Chapter 4 UI Design Section — Screenshots + Written Content

## Goal
Screenshot every ESPET dashboard screen (desktop + all scroll sections + key modals + one mobile view), save them as report figures, and write the shared "UI design" section for Chapter 4 in edu-writer style. Content is shared between both reports (hm + jeff).

## Phase A — Environment & sample data
1. Check `http://localhost:8000/health`; if the backend is not running, start it in the background from `espet-backend` (venv python / `npm run dev:win`) and wait for it to come up.
2. Seed realistic sample data so figures show populated lists:
   - One scheduled cron job (e.g. "Morning briefing", recurring daily, device channel)
   - One skill entry
   - One quick memory note
   - Verify profile fields are sensibly filled
   (Done via the dashboard UI / localhost API with the device key.)

## Phase B — Screenshots (Playwright MCP)
Desktop at 1440×900, dark theme, navigating hash routes on `http://localhost:8000`. For long vertical pages, scroll and capture each logical section separately. Provisional figure list (numbering finalised in the markdown):

1. **Home/Overview** — hero + stat cards + onboarding checklist (top); device card + donut chart (scrolled)
2. **Agent** — 3D avatar stage + trait tags; **dress-up workbench modal**
3. **Chat** — offline state with status chip + disabled composer (graceful degradation)
4. **Camera** — offline/unreachable state with LIVE-tag UI
5. **Assistant page** (scroll sections captured individually via SectionRail):
   - Personality (SourceField Edit tab; Preview tab if visually distinct)
   - Profile (Fields tab, and Markdown tab)
   - Memory (editor + quick note)
   - Schedule (populated job list) + **New job modal** (trigger/channel form)
   - Skills (list) + **skill editor modal** (8 KB byte counter)
6. **System page**: Device (settings + address; channels + version table), Tools (explorer grid; schema modal if tools are populated — depends on Google OAuth state, captured as-is)
7. **Mobile** (390×844) — one figure showing bottom dock nav + stacked layout

Save PNGs (Playwright output dir), then copy into `C:\herman\OneDrive - Default Directory\.TARUMT\_FYP\_FYP Report\hm\figures\` as `figure-4-N-<screen-name>.png` (existing naming convention).

## Phase C — Written section (edu-writer style)
Create `hm/Chapter 4 - Design.md` containing only the UI design subsection:
- Intro: the web dashboard's role as the shared configuration/monitoring surface of the embodied agent (shared platform infrastructure for both members' reports)
- Design language: monochrome glass system, Outfit + JetBrains Mono typography, dim-blue primary accent, dimmed semantic status tones, restrained motion, honesty rules (explicit offline states, no fake data), 820px responsive breakpoint
- Grouped figures with explanations: global navigation & layout → monitoring (Home) → agent personalisation (Agent) → direct device views (Chat/Camera) → configuration views (Assistant sections + modals) → system views (Device/Tools) → responsive design
- Each figure: `Figure 4.N: <caption>` below the image reference, plus a short explanation paragraph tying UI elements to their function (Schedule ↔ cron job management, Memory ↔ on-device consolidation, etc.), keeping the framing neutral/shared
- edu-writer rules throughout: UK English, no em dashes, no semicolon-joined sentences, sentence-case headings, figures explicitly referenced in text ("Figure 4.1 shows…")
- Header note in the file: section is shared content for both reports, figure numbers provisional pending other Chapter 4 figures

## Phase D — Cleanup & wrap-up
1. Remove seeded sample data (delete demo job + skill, revert the memory note) so the live config returns to its prior state.
2. Stop the backend if I started it.
3. Report: figure inventory with paths + the markdown file location. (No repo code changed, so no SYSTEM_OVERVIEW.md/DESIGN.MD update is required.)