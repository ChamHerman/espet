# ESPET Dashboard — Design System (DESIGN.MD)

Source of truth: `espet-backend/static/style.css` (dark glassmorphism).
Served by the FastAPI backend at `/`; single page, no build step.
Cache-busting: asset URLs carry `?v=N` in `index.html` — **bump N on every CSS/JS edit**.

---

## 1. Design language

- **Style**: dark glassmorphism, density 8/10 — translucent cards over an ambient gradient backdrop.
- **Palette**: slate-dark base + emerald accent; blue for informational/interactive, amber for warnings/dirty-state, red for danger.
- **Fonts**: Outfit (UI), JetBrains Mono (values, code, badges). Loaded from Google Fonts.
- **Personality**: soft glows on status dots, blurred ambient "orbs", gentle hover lifts — motion is progressive enhancement only (`prefers-reduced-motion` honored).

## 2. Tokens (`:root`)

### Colors

| Token | Value | Use |
|---|---|---|
| `--bg` | `#0a0e17` | Page background |
| `--bg-elevated` | `#101725` | Elevated surfaces |
| `--card` | `rgba(21, 28, 46, 0.62)` | Glass card fill |
| `--card-solid` | `#141b2d` | Modals, toasts (opaque) |
| `--card-hover` | `rgba(30, 40, 66, 0.72)` | Row hover fill |
| `--inset` | `rgba(255, 255, 255, 0.035)` | Inset wells, inputs, buttons |
| `--border` | `rgba(255, 255, 255, 0.08)` | Hairline borders |
| `--border-strong` | `rgba(255, 255, 255, 0.16)` | Input/button borders |
| `--text` | `#f1f5f9` | Primary text |
| `--text-muted` | `#94a3b8` | Secondary text |
| `--text-dim` | `#64748b` | Hints, labels |
| `--accent` | `#10b981` | Emerald accent |
| `--accent-soft` | `rgba(16, 185, 129, 0.14)` | Accent fills |
| `--accent-border` | `rgba(16, 185, 129, 0.38)` | Accent borders |
| `--blue` | `#3b82f6` | Info/focus |
| `--blue-soft` / `--blue-border` | `rgba(59,130,246,.14)` / `.38` | Info fills/borders |
| `--amber` | `#fbbf24` | Warnings, unsaved-changes |
| `--amber-soft` / `--amber-border` | `rgba(245,158,11,.13)` / `.4` | Warning fills/borders |
| `--red` | `#f87171` | Danger, errors |
| `--red-soft` / `--red-border` | `rgba(239,68,68,.13)` / `.4` | Danger fills/borders |
| `--ok` | `#34d399` | "Connected/online" states |

Status-color recipe: tinted fill + matching border + colored text, e.g. badges `.badge.emerald{color:#34d399;border-color:var(--accent-border);background:var(--accent-soft)}`.

### Geometry & type

| Token | Value |
|---|---|
| `--radius` | `14px` (cards, stages, empty states) |
| `--radius-sm` | `9px` (inputs, buttons, rows, nav items) |
| `--sidebar-w` | `232px` |
| `--font-ui` | `"Outfit", system-ui, sans-serif` |
| `--font-mono` | `"JetBrains Mono", ui-monospace, monospace` |

Body: `15px / 1.55`. Pills (chips, badges, filters) use `border-radius: 999px`.

## 3. Background & ambience

- **Body**: fixed layered radial gradients — blue top-left (`0.13`), purple top-right (`0.10`), emerald bottom-center (`0.09`) over `--bg`.
- **Orbs** (`.bg-orbs`, `aria-hidden`): three fixed blurred circles (`blur(80px)`, opacity `0.16`, blue/purple/emerald), drifted slowly by anime.js. Decorative only, `pointer-events: none`, `z-index: 0`; the app shell sits at `z-index: 1`.

## 4. Layout architecture

```
.app  display:grid; grid-template-columns: var(--sidebar-w) 1fr; min-height:100vh
├── .sidebar   sticky, 100vh, glass (rgba(10,14,23,.62) + blur(14px)), border-right
│   ├── .brand         40px gradient mark (blue→emerald) + name/sub
│   ├── .nav           column of .nav-item; .nav-label section headers
│   └── .sidebar-foot  margin-top:auto; backend/device footnote
└── .main      max-width:1080px; width:100%; margin-inline:auto  ← centered in
              the 1fr column (padding 22px 30px 60px)              remaining space
    ├── .topbar        h1 + subtitle, right: .topbar-meta chips; bottom hairline
    └── #view-root     per-view content (entrance animated by anime.js)
```

**Invariant**: `.main` must stay horizontally centered in the space right of the sidebar (`margin-inline: auto`). Removing that centering is what caused the all-left layout bug.

Sidebar active item: `linear-gradient(90deg, rgba(59,130,246,.16), rgba(16,185,129,.10))` + blue border; status dot (`.nav-dot`) glows `--ok` or `--red`.

### Status chips (`.chip`)

Pills in the topbar: server state (`.dot.ok/.bad` with glow), config version, unsaved-changes (`.chip.dirty` = amber tint + pulsing dot).

## 5. Components

| Component | Anatomy / key rules |
|---|---|
| **Card** `.card` | glass fill + `--border`, radius 14, padding 20, `blur(12px)`; hover brightens border. Head: `.card-head` (`.card-title` w/ 17px emerald svg + actions), `.card-sub` muted line |
| **Grids** | `.grid.cols-3` / `.grid.cols-2`, gap 16; `.stack` = column flex, gap 16 |
| **Stat** `.stat` | uppercase `.stat-label` (0.72rem, tracking .09em) + `.stat-value` 1.35rem mono; `.ok/.bad/.dim` variants; `.stat-detail` below |
| **Buttons** `.btn` | inset bg, strong border, radius-sm; `.primary` = emerald gradient, near-black text `#04110b`, glow shadow, `-1px` hover lift; `.blue` = blue gradient; `.danger-ghost` = red outline; `.small`; disabled at 45% opacity |
| **Icon button** `.icon-btn` | 30×30 ghost square; `.danger` hover tints red |
| **Forms** `.field` | label (muted, 600) → control → `.hint` (dim, 0.73rem). Inputs: near-black fill `rgba(8,12,20,.7)`, strong border; focus = blue border + `0 0 0 3px rgba(59,130,246,.22)` ring. Select: custom chevron svg, `appearance:none`. `.checkbox-row` uses `accent-color: var(--accent)` |
| **Switch** `.switch` | 36×20 track, 16px knob travel; checked = emerald soft bg + accent knob |
| **Row item** `.row-item` | inset well; `.name` (0.92rem/600) + `.meta` (0.78rem), right `.actions`. Hover → `--card-hover` |
| **Badge** `.badge` | mono 0.7rem pill; `.emerald` / `.blue` / `.amber` tint recipes |
| **Empty state** `.empty` | dashed `--border-strong` box, centered dim text + 26px icon |
| **Banner** `.banner` | amber default; `.info` blue, `.danger` red (same tint recipe); icon + text |
| **KV row** `.kv-row` | label muted / value mono, inset well |
| **Modal** | `.modal-backdrop` fixed blur(7px) `rgba(2,6,14,.72)`, fade+visibility transition; `.modal` opaque `--card-solid`, radius 16, max-w 560 (`.wide` 760 for dress-up), sticky head, foot right-aligned; `pre` on `#070b13` |
| **Toast** `.toast` | fixed bottom-right stack (`.toast-wrap`), `--card-solid`, emerald border; `.error` red; spring entrance `cubic-bezier(0.2,0.9,0.3,1.2)`; container `aria-live="polite"` |
| **Tool card** `.tool-card` | auto-fill grid `minmax(330px,1fr)`; mono blue `.tool-name`, 3-line clamped `.tool-desc`; search input (max 320px) + pill `.filter-btn` row |
| **Segmented** `.seg` | 2-col grid in inset well; active segment = blue soft + `#93c5fd` |
| **Skeleton** `.skeleton` | shimmer gradient sweep, 1.4s loop |

## 6. Agent showcase (3D stage)

- `.stage-hero`: min-height 450; `.stage-3d` 430px canvas layer over the stage backdrop, `-28px` bottom overlap so the robot stands over the podium.
- `.stage`: layered radial glows (emerald top `0.20`, blue bottom `0.16`) over near-black; centered `.stage-spot` spotlight halo; `.stage-podium` emerald ellipse glow; corner overlays `.stage-badge` / `.stage-name` (mono).
- Engine: three.js + RobotExpressive glTF via `static/vendor/toy.bundle.js` (esbuild bundle of `tools/toy-entry.js`; rebuild with `npm run build:vendor` — only needed when that entry changes).
- Fallback (no WebGL/JS): `.stage-avatar.fallback` renders a large emoji.
- **Dress-up workbench** (modal `.wide`): `.dress-grid` 2 cols (1 col < 700px) of `.dress-group`s — uppercase `.dress-label` + `.dress-row` of `.chip-btn` pills (optional `.swatch` dot); active = emerald tint. Trait readout uses `.trait` emerald pills.

## 7. Motion

| Animation | Where | Spec |
|---|---|---|
| View entrance | `#view-root` | anime.js `animateView` (JS-driven) |
| Orb drift | `.orb` | anime.js, slow |
| `pulse` | dirty chip dot | opacity 1→0.45→1, 2.4s infinite |
| `toast-in` | toasts | translateY(10px)+scale(.97)→none, 220ms spring |
| `shimmer` | skeletons | background-position sweep, 1.4s |
| Hover transitions | cards/rows/buttons | 140–220ms ease; primary button lifts 1px |

`prefers-reduced-motion: reduce` nukes all animation/transition durations to ~0 — the page is fully functional without motion.

## 8. Responsive breakpoints

| ≤ px | Changes |
|---|---|
| 980 | `.grid.cols-3` → 2 cols |
| 820 | Sidebar collapses to a top bar (row layout, wraps; brand text, nav labels, foot hidden); `.main` padding 18px 16px 50px; all card grids → 1 col |
| 700 | `.dress-grid` → 1 col |

## 9. Accessibility conventions

- `:focus-visible` everywhere: blue 2px outline (offset 2) on buttons/nav/icon-buttons; blue ring on inputs.
- `[hidden] { display: none !important; }` — never fight the attribute with classes.
- Modals: `role="dialog"` / `aria-modal`, labelled heads, close buttons with `aria-label`.
- Toasts `aria-live="polite"`; decorative layers (`bg-orbs`) `aria-hidden`.
- Confirm dialogs for destructive actions; unsaved-changes guard chip.
