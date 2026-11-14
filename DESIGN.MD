# ESPET Dashboard — Design System (DESIGN.MD)

**The single source of truth for the frontend's visual language.** Every UI
change to `espet-backend/src/` must follow these tokens — and **must update this
file in the same change** when a token, pattern, or component rule evolves
(mandated in AGENTS.md).

Implementation lives in: `src/global.css` (canvas, orbs, buttons, utilities),
`src/theme.tsx` (antd tokens + `ESPET_COLORS`), `src/ui.tsx` (glass primitives,
badges, layout), `src/components/` (GlassNav, SectionRail, StatusChips,
MarkdownField/SourceField), `static/index.html` (boot splash, favicon, fonts),
`static/cloud.html` (Cloud Gateway), `static/stt.html` (Mic & STT Tester),
`docs/ble_prov.html` (BLE Wi-Fi Provisioning, mirrored to `static/ble_prov.html`).
Built by `npm run build:app` → `static/vendor/app/` (cache-bust `?v=<hash>`
stamped automatically by the build — never bump it by hand).

---

## 1. Design language

- **Monochrome canvas**: black + white for every surface, tone, chart and
  accent — brightness, never hue. Exactly TWO chromatic exceptions:
  the **dim light-blue accent family** (primary buttons, checked toggles,
  selected segments/radio buttons, text selection) and **semantic status
  colors** (dimmed mint / red / yellow — see §2).
- Dark-first glassmorphism, density ~7/10: translucent white-on-black glass
  surfaces floating over a moving dimmed-white gradient canvas.
- Motion is springy but restrained; always progressive enhancement —
  `prefers-reduced-motion` disables orbs, springs, and pulses.
- Brand mark: `static/brand/espet_logo.jpg` (black-background robot badge —
  blends into the canvas). 46 px nav / 40 px Chat header / 128 px boot splash /
  favicon. Never pair a stroked `Icon` with a light Avatar gradient without
  setting a dark `color` — antd Avatar's default icon color is white.

## 2. Color tokens

### Canvas & glass

| Token | Value | Use |
|---|---|---|
| `--espet-bg` | `#050505` | Page background (near-black) |
| `--espet-elevated` | `#101010` | antd `colorBgContainer` |
| Glass fill | `rgba(255,255,255,0.055)` | cards, nav pill, dock (`blur(12-20px) saturate(1.1)`) |
| Glass soft | `rgba(255,255,255,0.035)` | inset wells, rows |
| Hairline border | `rgba(255,255,255,0.09-0.12)` | every glass edge |
| Top highlight | `inset 0 1px 0 rgba(255,255,255,0.1)` | nav/dock depth |
| Floating shadow | `0 10px 40px rgba(0,0,0,0.55)` | nav, dock, bubbles |
| Solid bubble fill | `#0b0b0b` | rail dots (opaque so the line stops at them) |

### Text ramp (pure neutral grays — no slate/blue tint)

| Token | Value | Use |
|---|---|---|
| text | `#f5f5f5` | primary |
| text-muted | `#a3a3a3` | secondary, labels |
| text-dim | `#737373` | hints, footer credit |
| on-active (dark) | `#050505` | icon on white-filled active bubble/chip |

### Semantic status tones (dimmed hues — the sanctioned exception)

Success / live / online / complete paths are **dimmed mint green**; failure /
offline / disconnected paths are **dimmed red**; warnings and anything needing
user attention are **dimmed yellow**; general / informational messages stay
glass black + white. Status text is always **Title Case** ("Not Connected",
"Backend Online", "Unsaved Changes").

| Meaning | Text/dot | Glass bg | Border |
|---|---|---|---|
| Success (live, online, done) | `#8fd9ae` mint | `rgba(143,217,174,0.10)` | `rgba(143,217,174,0.28)` |
| Warning / attention | `#e6cf94` yellow | `rgba(230,207,148,0.10)` | `rgba(230,207,148,0.28)` |
| Danger (failed, offline) | `#eda7a7` red | `rgba(237,167,167,0.10)` | `rgba(237,167,167,0.28)` |
| Neutral / general | `#d4d4d4` | `rgba(255,255,255,0.06)` | `rgba(255,255,255,0.12)` |

antd tokens mirror these in `theme.tsx` (`colorSuccess/colorWarning/colorError`
= the hues above, `colorInfo` stays gray `#d4d4d4`) so Badge dots, Alerts,
Switch danger buttons and toasts inherit them. Toast kinds: `toast(msg,
"success" | "error" | "info")` in `store.tsx` — in-progress messages are
`"info"` (neutral glass), never fake-green.

Non-status decoration (charts, trait tags, meta badges) remains monochrome —
chart palette `["#fafafa","#d4d4d4","#a3a3a3","#737373","#525252"]`.
**Dim light-blue accent family** (the only other hue):
- Primary button: gradient `rgba(96,165,250,0.22 → 147,197,253,0.14)`, border
  `rgba(147,197,253,0.35)`, text `#dbeafe`.
- Checked `Switch`: gradient `rgba(96,165,250,0.55 → 147,197,253,0.4)`
  (global CSS, `.ant-switch-checked`).
- Selected `Segmented` item / solid `Radio.Button`: bg
  `rgba(147,197,253,0.22)`, text `#dbeafe` (antd component tokens).
- Text selection `::selection`: `rgba(147,197,253,0.35)` + `#f0f7ff`.
No other hues, no slate/blue-tinted grays; never antd preset
`Tag color="blue"` etc.

## 3. Typography

| Role | Font | Notes |
|---|---|---|
| All UI + rendered content | **Outfit** (Google Fonts 300-700) | incl. markdown **previews** (Preview tab) |
| Raw markdown only | **JetBrains Mono** 13px | Edit-tab textareas, SourceField; antd `fontFamilyCode` |
| Technical values | JetBrains Mono via `.espet-mono` | versions, bytes, addresses, file names |

Rule: *if the user is looking at markdown source, it's mono; if they're reading
rendered content, it's Outfit.* Body `15px / 1.55`. No other fonts.

## 4. Layout

- Content column: max-width **1080 px**, centered, `padding 22px 22px 90px`.
- Floating nav pill: fixed `top 14px`, **centered with `width: fit-content`**
  (`left/right: 16px; margin-inline: auto`), height 70, radius 20 — brand +
  items only, no empty stretch, no status chips. Reserve space with
  `--espet-nav-h` (98 px desktop / 88 px mobile) — the GlassNav spacer, Chat
  height, and section `scroll-margin-top` all consume it.
- **Scroll owns `html`, never body**: `html { overflow-x: clip;
  overflow-y: auto }`, `body { overflow-x: clip }`, `#root { min-height: 100% }`
  (no `height: 100%`). Explicit html overflow stops body→viewport overflow
  propagation, so antd Modal's body scroll-lock can't reset `window.scrollY`
  (the "modal opens, page jumps to top" bug). Never `overflow-x: hidden`
  either (it would make body a scroll container and break `window.scrollY`).
- Section rail (Assistant/System pages): fixed right 14, vertical center;
  solid 36 px bubbles (32 px mobile) on a 2 px white gradient line passing
  through their centres; label pill collapsed to zero width except the active
  bubble, slides open on hover — the rail never crowds content.
- **StatusDock**: fixed bottom-right (14/14) glass chips — config version
  (neutral glass, mint dot) + backend health (mint/red). Desktop only; mobile
  keeps the compact chip row in the top brand pill. "Unsaved Changes" shows on
  the **container being edited** (card header), never in the nav.
- Radius scale: 999 (pills/chips) · 20-22 (nav, dock) · 16 (cards, frames) ·
  12-14 (inputs, sections; antd `borderRadius: 14`).
- Spacing: card padding 20, stacks 18-30, grids 16.
- Control height: global `controlHeight: 40` — inputs, selects, segmented and
  buttons all share one 40 px middle size (so `addonBefore` groups line up
  with their Save button); small controls use `controlHeightSM`.
- Breakpoints: single nav breakpoint at **820 px** (top pill ↔ brand pill +
  bottom dock); content grids collapse at 720/860.

## 5. Motion (`motion` — "motion/react")

- Nav: spring entrance (`stiffness 260, damping 22`), scroll shrink
  `scale 0.97`, active pill magic-move via shared `layoutId`.
- Items: hover `scale 1.05 / y -1`, tap `0.94`; brand wiggle `scale 1.12
  rotate -4`.
- Buttons: hover lift `translateY(-2) scale(1.02)` (cubic-bezier
  `.34,1.56,.64,1`), press `0.98`.
- Orbs: 4 dimmed-white radials, closed multi-waypoint viewport paths, 60-95 s.
- Reduced motion: springs → `duration: 0`; orbs/pulses off.

## 6. Components

- **Card** (`ui.tsx`): glass fill + hairline + radius 16; `hover` prop adds the
  lift + brighter border.
- **Badge/Chip**: pill, 13 px, semantic tone (`ui.tsx` `Badge` tone
  `default/success/warning/danger`; 12 px meta badges). **`StatusChip`**
  (`StatusChips.tsx`): the 13 px glass pill (7/15 padding) for live status —
  built-in semantic dot, optional `pulse`; Title Case text. **`SectionDirtyChip`**:
  yellow pulsing "Unsaved Changes" chip rendered in the edited card's header
  (sections `soul/profile/memory/device`).
- **Primary button**: antd/lobe `Button type="primary"` — global CSS paints the
  dim-blue gradient; never hand-roll a colored button.
- **Toggles & selection**: checked `Switch`, selected `Segmented` item and
  solid `Radio.Button` all take the dim-blue accent family (§2); selection
  states must never be grey-with-white-text. Switch labels keep an 8 px gap.
- **Markdown fields — ONE unified editor**: `SourceField` (components/
  SourceField.tsx) everywhere. "Content (markdown)" caption above small
  antd Tabs **Edit | Preview** (Edit default): Edit = exact-byte JetBrains Mono
  textarea (16/14 padding, mono 13px), Preview = lobe `Markdown` in Outfit;
  live byte counter with `byteLimit`. `canonical()` lives here too. No
  Segmented "View/Edit" anywhere.
- **Switch + label**: never a bare text node next to `<Switch />` — wrap the
  label: `<Switch />{" "}<span style={{ marginLeft: 8 }}>Label</span>` (or a
  flex row with gap, as `rowItem` does).
- **SectionRail**: see §4.
- **Chat media / photo bubbles**: Images rendered inside message bubbles via antd `<Image />` with `maxHeight: 340`, `borderRadius: 12`, hairline border `rgba(255,255,255,0.12)`, and dark well background `rgba(0,0,0,0.35)`; native lightbox preview with zoom/rotate controls on click.
- **StatusChips**: `StatusDock` (bottom-right) + `StatusChips` (mobile top bar)
  + `StatusChip`/`SectionDirtyChip` primitives. Nothing status-related in the
  nav.
- **Footer**: plain text only — no background, no border. Centered — brand
  `#a3a3a3` · separator `#3f3f46` · credit `#737373`.
- **Toasts/confirms**: antd `App` context + store `toast(msg, kind)` /
  `confirm()` only; kind = success (mint) / error (red) / info (glass).

## 7. Wording

- The product is **ESPET** everywhere user-facing (copy, defaults, persona).
- **MimiClaw appears only as the footer credit.** Never introduce it as a label.
- Voice: direct, second person, no marketing fluff.

## 8. Honesty rules

- No fake data: charts render only from real endpoints/localStorage samples;
  unavailable device features (e.g. the camera endpoint until firmware ships
  it) show a clear offline state, never a placeholder frame.
- Destructive actions go through `confirm()`; dirty state guards navigation.
