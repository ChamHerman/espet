/* REST client — same endpoints + Bearer device key as the legacy dashboard.
   Single-user LAN scope: this page is served by the backend itself, so the
   key is embedded (mirrors MIMI_BACKEND_API_KEY / DEVICE_API_KEY). */

import type {
  AuthStatus,
  AvatarState,
  ConfigBundle,
  Health,
  Job,
  McpTool,
  MemoryItem,
  MemoryQueryResponse,
  Skill,
} from "./types";

export const API_KEY = "espet_secret_device_key_2026";

export async function api<T>(path: string, options: RequestInit = {}): Promise<T> {
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
    try {
      const body = await res.json();
      detail = body.detail || detail;
    } catch {
      /* non-JSON error body */
    }
    throw new Error(detail);
  }
  return res.json() as Promise<T>;
}

const json = (method: string, body: unknown): RequestInit => ({
  method,
  body: JSON.stringify(body),
});

export const client = {
  // reads
  config: () => api<ConfigBundle>("/api/config"),
  health: () => api<Health>("/health"),
  auth: () => api<AuthStatus>("/auth/status"),
  tools: () => api<{ tools: McpTool[] }>("/api/tools"),
  avatar: () => api<AvatarState>("/api/ui/avatar"),
  skills: () => api<{ skills: Skill[] }>("/api/ui/skills"),

  // writes — every response echoes the fresh `versions` map (config_router)
  saveProfile: (user_md: string) =>
    api<ConfigBundle>("/api/ui/profile", json("PUT", { user_md })),
  saveSoul: (soul_md: string) =>
    api<ConfigBundle>("/api/ui/soul", json("PUT", { soul_md })),
  saveMemory: (memory_md: string) =>
    api<ConfigBundle>("/api/ui/memory", json("PUT", { memory_md })),
  saveDevice: (d: { name: string; timezone: string; sync_interval_s: number }) =>
    api<ConfigBundle>("/api/ui/device", json("PUT", d)),
  saveAvatar: (mode: string, opts: Record<string, unknown>) =>
    api<AvatarState>("/api/ui/avatar", json("PUT", { mode, opts })),

  createJob: (p: Partial<Job>) =>
    api<ConfigBundle>("/api/ui/jobs", json("POST", p)),
  updateJob: (id: string, p: Partial<Job>) =>
    api<ConfigBundle>(`/api/ui/jobs/${id}`, json("PUT", p)),
  deleteJob: (id: string) =>
    api<ConfigBundle>(`/api/ui/jobs/${id}`, { method: "DELETE" }),

  createSkill: (p: Partial<Skill>) =>
    api<{ skills: Skill[] }>("/api/ui/skills", json("POST", p)),
  updateSkill: (id: string, p: Partial<Skill>) =>
    api<{ skills: Skill[] }>(`/api/ui/skills/${id}`, json("PUT", p)),
  deleteSkill: (id: string) =>
    api<{ skills: Skill[] }>(`/api/ui/skills/${id}`, { method: "DELETE" }),

  reconnectMcp: () => api("/api/mcp/reconnect", json("POST", {})),

  // Long-Term Memory (SQLite-Vec RAG)
  listMemories: (p?: { category?: string; search?: string; limit?: number; offset?: number; device_id?: string }) => {
    const q = new URLSearchParams();
    if (p?.category && p.category !== "all") q.set("category", p.category);
    if (p?.search) q.set("search", p.search);
    if (p?.limit) q.set("limit", String(p.limit));
    if (p?.offset) q.set("offset", String(p.offset));
    if (p?.device_id) q.set("device_id", p.device_id);
    const qs = q.toString();
    return api<{ items: MemoryItem[]; total: number }>(`/api/memory/list${qs ? `?${qs}` : ""}`);
  },
  createMemoryItem: (p: { content: string; category?: string; device_id?: string }) =>
    api<{ ok: boolean; id: number; item: MemoryItem }>("/api/memory/item", json("POST", p)),
  updateMemoryItem: (id: number, p: { content: string; category?: string }) =>
    api<{ ok: boolean; id: number; item: MemoryItem }>(`/api/memory/item/${id}`, json("PUT", p)),
  deleteMemoryItem: (id: number) =>
    api<{ ok: boolean; deleted: boolean; id: number }>(`/api/memory/item/${id}`, { method: "DELETE" }),
  queryMemory: (query: string, top_k = 3, threshold = 0.28, device_id = "espet_s3_01") =>
    api<MemoryQueryResponse>("/api/memory/query", json("POST", { query, top_k, threshold, device_id })),
  syncFromMemoryMd: (device_id = "espet_s3_01") =>
    api<{ ok: boolean; indexed: number; total_lines: number }>(`/api/memory/sync-from-md?device_id=${device_id}`, json("POST", {})),
};

/* Device WebSocket gateway address for the direct-chat view (localStorage;
   the ESP32 gateway has no cross-origin gate, so the browser talks to it
   directly — see ws_server.h protocol). Port fixed at 18789 in firmware. */
export const WS_GATEWAY_PORT = 18789;
export const DEVICE_ADDR_KEY = "espet.deviceAddr";

export function getDeviceAddr(): string {
  return localStorage.getItem(DEVICE_ADDR_KEY) || "espet.local";
}
export function setDeviceAddr(v: string) {
  localStorage.setItem(DEVICE_ADDR_KEY, v.trim());
}
export function deviceWsUrl(): string {
  const a = getDeviceAddr();
  const host = /^wss?:\/\//.test(a) ? a.replace(/^wss?:\/\//, "") : a;
  const hasPort = /:\d+$/.test(host);
  return `ws://${host}${hasPort ? "" : `:${WS_GATEWAY_PORT}`}/`;
}
