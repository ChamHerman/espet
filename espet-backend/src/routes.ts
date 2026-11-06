/* Route table — hash router. Home / Cloud / Chat / Camera are primary pages; Assistant
   and System are single scroll pages whose sections (the old standalone views)
   are reached via the right-side bubble rail or deep links like
   #assistant/memory (bare #memory still works as an alias). */

import type { IconName } from "./components/Icon";

export interface ViewMeta {
  id: string;
  label: string;
  icon: IconName;
  title: string;
  subtitle: string;
}

export const PRIMARY: ViewMeta[] = [
  {
    id: "overview",
    label: "Home",
    icon: "dashboard",
    title: "Overview",
    subtitle:
      "Live status of the backend, Google services, and the device configuration.",
  },
  {
    id: "cloud",
    label: "Cloud",
    icon: "server",
    title: "Cloud Gateway",
    subtitle:
      "Hybrid Mode configuration: Telegram bot poller, server-side LLM relay, and chat bindings.",
  },
  {
    id: "chat",
    label: "Chat",
    icon: "message",
    title: "Chat",
    subtitle:
      "Talk to ESPET directly over the device WebSocket gateway — a transparent log of what you asked and what it did.",
  },
  {
    id: "camera",
    label: "Camera",
    icon: "camera",
    title: "Camera",
    subtitle:
      "Live view from the ESP32-S3 camera — snapshots polled straight from the device over your LAN.",
  },
];

export const ASSISTANT: ViewMeta[] = [
  {
    id: "personality",
    label: "Personality",
    icon: "sparkles",
    title: "Personality",
    subtitle:
      "Character and behavior rules — synced to the device as SOUL.md.",
  },
  {
    id: "profile",
    label: "Profile",
    icon: "user",
    title: "Profile",
    subtitle:
      "Who ESPET is assisting — synced to the device as USER.md.",
  },
  {
    id: "memory",
    label: "Memory",
    icon: "database",
    title: "Memory",
    subtitle:
      "Long-term memory — synced to the device as MEMORY.md.",
  },
  {
    id: "schedule",
    label: "Schedule",
    icon: "calendar",
    title: "Schedule",
    subtitle:
      "Proactive jobs: briefings and reminders the device runs on its own.",
  },
  {
    id: "skills",
    label: "Skills",
    icon: "book",
    title: "Skills",
    subtitle:
      "Reusable playbooks the agent can follow — synced to the device as markdown files.",
  },
];

export const SYSTEM: ViewMeta[] = [
  {
    id: "device",
    label: "Device",
    icon: "cpu",
    title: "Device",
    subtitle: "Device identity, timezone, and sync settings.",
  },
  {
    id: "tools",
    label: "Tools",
    icon: "box",
    title: "MCP Tools",
    subtitle:
      "Remote tools discovered from the backend's connected MCP servers.",
  },
];

/* The two grouped pages. Their `items` are the stacked sections, in order. */
export const ASSISTANT_PAGE: ViewMeta & { items: ViewMeta[] } = {
  id: "assistant",
  label: "Assistant",
  icon: "sparkles",
  title: "Assistant",
  subtitle:
    "Everything that shapes your agent — scroll down through each area, or jump with the bubbles on the right.",
  items: ASSISTANT,
};

export const SYSTEM_PAGE: ViewMeta & { items: ViewMeta[] } = {
  id: "system",
  label: "System",
  icon: "cpu",
  title: "System",
  subtitle: "Device settings and the connected MCP tools.",
  items: SYSTEM,
};

export const PAGES: (ViewMeta & { items: ViewMeta[] })[] = [
  ASSISTANT_PAGE,
  SYSTEM_PAGE,
];

export const ALL_VIEWS: ViewMeta[] = [
  ...PRIMARY,
  ...PAGES,
  ...ASSISTANT,
  ...SYSTEM,
];

export function viewMeta(id: string): ViewMeta {
  return (
    ALL_VIEWS.find((v) => v.id === id) ||
    ALL_VIEWS[0]
  );
}

export function isValidView(id: string): boolean {
  return ALL_VIEWS.some((v) => v.id === id);
}

/** Resolve a hash ("assistant", "assistant/memory", "memory", "overview"…)
 *  into a page view + optional section. Bare section slugs alias to their page. */
export function parseRoute(h: string): { view: string; section?: string } {
  const [base, ...rest] = h.split("/");
  const raw = rest.join("/");
  const page = PAGES.find((p) => p.id === base);
  if (page) {
    const section = page.items.some((i) => i.id === raw) ? raw : undefined;
    return { view: page.id, section };
  }
  const owner = PAGES.find((p) => p.items.some((i) => i.id === base));
  if (owner) return { view: owner.id, section: base };
  return { view: isValidView(base) ? base : "overview" };
}
