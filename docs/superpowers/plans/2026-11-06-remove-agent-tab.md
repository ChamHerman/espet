# Remove #agent Tab and 3D Avatar Customization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the `#agent` tab and 3D companion customization workbench from the ESPET control center dashboard navigation and SPA bundle, redirecting direct `#agent` visits to `#overview`.

**Architecture:** Update route declarations in TypeScript source (`routes.ts`, `App.tsx`) and the corresponding compiled frontend assets (`chunk-UXN76GPX.js`, `index.js`). Remove the 3D toy viewer script from `index.html` and the `#agent` link from `cloud.html`. Update project documentation (`.agent/SYSTEM_OVERVIEW.md`).

**Tech Stack:** TypeScript, React 19, HTML5, Python (FastAPI backend).

## Global Constraints
- Do not break existing routes: `overview`, `cloud`, `chat`, `camera`, `assistant`, `system`.
- Direct navigation to `#agent` must gracefully fall back to `#overview`.
- Follow the Session Memory Rule in `AGENTS.md`: update `.agent/SYSTEM_OVERVIEW.md` with recent changes.

---

### Task 1: Update Frontend Route Declarations and Bundled SPA Navigation

**Files:**
- Modify: `espet-backend/src/routes.ts:16-57`
- Modify: `espet-backend/src/App.tsx:16-24`
- Modify: `espet-backend/static/vendor/app/chunks/chunk-UXN76GPX.js`
- Modify: `espet-backend/static/vendor/app/index.js`

**Interfaces:**
- Consumes: SPA navigation router
- Produces: `PRIMARY` list without `agent` view; router mapping without `agent`

- [ ] **Step 1: Write verification script to test route table and chunk contents**

Create temporary test script `espet-backend/tests/verify_no_agent.cjs`:
```javascript
const fs = require('fs');
const path = require('path');

const chunkFile = path.join(__dirname, '../static/vendor/app/chunks/chunk-UXN76GPX.js');
const indexFile = path.join(__dirname, '../static/vendor/app/index.js');
const routesFile = path.join(__dirname, '../src/routes.ts');
const appFile = path.join(__dirname, '../src/App.tsx');

const chunk = fs.readFileSync(chunkFile, 'utf8');
const routes = fs.readFileSync(routesFile, 'utf8');
const app = fs.readFileSync(appFile, 'utf8');
const index = fs.readFileSync(indexFile, 'utf8');

if (chunk.includes('id:"agent"')) {
  console.error("FAIL: chunk-UXN76GPX.js still contains id:\"agent\"");
  process.exit(1);
}
if (routes.includes('id: "agent"')) {
  console.error("FAIL: routes.ts still contains id: \"agent\"");
  process.exit(1);
}
if (app.includes('agent: lazy(')) {
  console.error("FAIL: App.tsx still contains agent view lazy loader");
  process.exit(1);
}
if (index.includes('agent:(0,n.lazy)')) {
  console.error("FAIL: index.js still registers agent view");
  process.exit(1);
}

console.log("PASS: route tables and bundles do not contain agent view");
```

- [ ] **Step 2: Run verification script to confirm it fails**

Run: `node espet-backend/tests/verify_no_agent.cjs`
Expected: FAIL (contains agent view)

- [ ] **Step 3: Modify `routes.ts`, `App.tsx`, `chunk-UXN76GPX.js`, and `index.js`**

1. In `espet-backend/src/routes.ts`:
Remove:
```typescript
  {
    id: "agent",
    label: "Agent",
    icon: "bot",
    title: "Agent",
    subtitle:
      "Your agent, styled by its own settings — personality, memory and schedules shape the look.",
  },
```
from `PRIMARY`.

2. In `espet-backend/src/App.tsx`:
Remove `agent: lazy(() => import("./views/Agent").then((m) => ({ default: m.AgentView }))),` from `views`.

3. In `espet-backend/static/vendor/app/chunks/chunk-UXN76GPX.js`:
Remove `{id:"agent",label:"Agent",icon:"bot",title:"Agent",subtitle:"Your agent, styled by its own settings \\u2014 personality, memory and schedules shape the look."},` from `var m=[...]`.

4. In `espet-backend/static/vendor/app/index.js`:
Remove `agent:(0,n.lazy)(()=>import("./chunks/Agent-G4JAZ3H4.js").then(e=>({default:e.AgentView}))),` from `q={...}`.

- [ ] **Step 4: Run verification script to confirm it passes**

Run: `node espet-backend/tests/verify_no_agent.cjs`
Expected: PASS

- [ ] **Step 5: Clean up verification script**

Run: `rm espet-backend/tests/verify_no_agent.cjs`

---

### Task 2: Remove Agent Link from Static HTML Templates and Eliminate Toy Bundle Script

**Files:**
- Modify: `espet-backend/static/cloud.html:301-304`
- Modify: `espet-backend/static/index.html:45-48`

- [ ] **Step 1: Write test to verify static HTML templates do not reference `#agent` or `toy.bundle.js`**

```javascript
// Test check in node:
const fs = require('fs');
const cloud = fs.readFileSync('espet-backend/static/cloud.html', 'utf8');
const index = fs.readFileSync('espet-backend/static/index.html', 'utf8');
if (cloud.includes('/#agent')) throw new Error('cloud.html still links to /#agent');
if (index.includes('toy.bundle.js')) throw new Error('index.html still loads toy.bundle.js');
```

- [ ] **Step 2: Update `espet-backend/static/cloud.html`**

Remove the `/#agent` link element:
```html
      <a href="/#agent" class="espet-nav-link">
        <svg width="17" height="17" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 8V4H8"></path><rect width="16" height="12" x="4" y="8" rx="2"></rect><path d="M2 14h2"></path><path d="M20 14h2"></path><path d="M15 13v2"></path><path d="M9 13v2"></path></svg>
        <span>Agent</span>
      </a>
```

- [ ] **Step 3: Update `espet-backend/static/index.html`**

Remove:
```html
  <!-- 3D companion viewer (framework-agnostic global consumed by <ToyAvatar>) -->
  <script src="/static/vendor/toy.bundle.js?v=2"></script>
```
And bump cache-buster query version on `index.js` to `?v=noagent1`.

- [ ] **Step 4: Verify test passes**

Run node check to ensure neither file retains `#agent` or `toy.bundle.js`.

---

### Task 3: Update Documentation (.agent/SYSTEM_OVERVIEW.md)

**Files:**
- Modify: `.agent/SYSTEM_OVERVIEW.md`

- [ ] **Step 1: Update Dashboard Views table in `.agent/SYSTEM_OVERVIEW.md`**

Remove the `**Agent**` row from Section 9 (Dashboard SPA) view table.
Update recent changes log at the bottom:
`- 2026-11-06 — Removed the #agent 3D companion tab and avatar customizer from the dashboard navigation (files: espet-backend/src/routes.ts, espet-backend/src/App.tsx, espet-backend/static/cloud.html, espet-backend/static/index.html, static vendor app chunks, .agent/SYSTEM_OVERVIEW.md)`

- [ ] **Step 2: Verify git diff of `.agent/SYSTEM_OVERVIEW.md`**

---

### Task 4: Run E2E Test Suite and Verify System Health

**Files:**
- Test: `espet-backend/tests/test_cloud_gateway.py`

- [ ] **Step 1: Run the backend test suite**

Run: `cd espet-backend && source venv/bin/activate && python tests/test_cloud_gateway.py`
Expected: 10/10 test cases passed.

- [ ] **Step 2: Verify hash route fallback in Node**

Verify that `parseRoute("agent")` resolves to `{ view: "overview" }`.
