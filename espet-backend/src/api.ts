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
export interface DeviceInfo {
  device_id: string;
  name?: string;
  online: boolean;
  last_seen: string;
  ip?: string;
  fw?: string;
  is_latest?: boolean;
}

export interface CameraStatus {
  device_id: string;
  streaming: boolean;
  fps: number;
  target_fps: number;
  viewer_count: number;
  frame_count: number;
  frame_ts: number;
  last_frame_age_ms: number;
  inference_ms: number;
  detections: { label?: string; confidence?: number; bbox?: number[] }[];
  cv_backend: string;
  mode: "raw" | "processed" | string;
  has_frame: boolean;
  owner_detected: boolean;
  owner_last_seen_ts: number;
  owner_similarity: number;
  owner_face_enrolled: boolean;
  owner_face_error?: string | null;
  owner_welcome_last_ts: number;
}

export interface OwnerFaceStatus {
  ok?: boolean;
  device_id: string;
  enabled: boolean;
  installed: boolean;
  enrolled: boolean;
  enrolled_at?: number | null;
  image_sha256?: string | null;
  has_preview?: boolean;
  threshold: number;
  watch_enabled: boolean;
  watch_interval_s: number;
  watch_sample_s: number;
  watch_fps: number;
  error?: string | null;
}


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
  devices: () => api<{ devices: DeviceInfo[]; latest_device_id: string }>("/api/devices"),
  syncNow: (device_id: string) =>
    api<{ ok: boolean; device_id: string; pushed: boolean }>("/api/config/sync-now", json("POST", { device_id })),
  config: (device_id?: string) =>
    api<ConfigBundle>(`/api/config${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`),
  configVersion: (device_id?: string) =>
    api<{ versions: Record<string, number>; device: Record<string, unknown> }>(
      `/api/config/version${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`
    ),
  health: () => api<Health>("/health"),
  auth: () => api<AuthStatus>("/auth/status"),
  tools: () => api<{ tools: McpTool[] }>("/api/tools"),
  avatar: () => api<AvatarState>("/api/ui/avatar"),
  skills: (device_id?: string) =>
    api<{ skills: Skill[] }>(`/api/ui/skills${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`),

  // writes — every response echoes the fresh `versions` map (config_router)
  saveProfile: (user_md: string, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/profile${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { user_md, device_id })),
  saveSoul: (soul_md: string, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/soul${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { soul_md, device_id })),
  saveMemory: (memory_md: string, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/memory${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { memory_md, device_id })),
  saveDevice: (d: { name: string; timezone: string; sync_interval_s: number }, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/device${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { ...d, device_id })),
  saveAvatar: (mode: string, opts: Record<string, unknown>) =>
    api<AvatarState>("/api/ui/avatar", json("PUT", { mode, opts })),

  createJob: (p: Partial<Job>, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/jobs${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("POST", { ...p, device_id })),
  updateJob: (id: string, p: Partial<Job>, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/jobs/${id}${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { ...p, device_id })),
  deleteJob: (id: string, device_id?: string) =>
    api<ConfigBundle>(`/api/ui/jobs/${id}${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, { method: "DELETE" }),

  createSkill: (p: Partial<Skill>, device_id?: string) =>
    api<{ skills: Skill[] }>(`/api/ui/skills${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("POST", { ...p, device_id })),
  updateSkill: (id: string, p: Partial<Skill>, device_id?: string) =>
    api<{ skills: Skill[] }>(`/api/ui/skills/${id}${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, json("PUT", { ...p, device_id })),
  deleteSkill: (id: string, device_id?: string) =>
    api<{ skills: Skill[] }>(`/api/ui/skills/${id}${device_id ? `?device_id=${encodeURIComponent(device_id)}` : ""}`, { method: "DELETE" }),

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
  autoOrganizeMemory: (device_id = "espet_s3_01") =>
    api<{ ok: boolean; cleaned_actions: number; device_id: string }>(`/api/memory/auto-organize?device_id=${encodeURIComponent(device_id)}`, json("POST", {})),

  cameraStatus: (device_id = "espet_s3_01") =>
    api<CameraStatus>(`/api/camera/status?device_id=${encodeURIComponent(device_id)}`),
  startCameraStream: (p: { device_id?: string; fps?: number }) =>
    api<{ ok: boolean; device_id: string; fps: number; pushed: boolean }>("/api/camera/stream/start", json("POST", p)),
  stopCameraStream: (device_id = "espet_s3_01") =>
    api<{ ok: boolean; device_id: string; pushed: boolean }>("/api/camera/stream/stop", json("POST", { device_id })),
  setCameraConfig: (p: { device_id?: string; mode?: "raw" | "processed"; cv_backend?: string }) =>
    api<CameraStatus & { ok: boolean }>("/api/camera/config", json("PUT", p)),
  ownerFaceStatus: (device_id = "espet_s3_01") =>
    api<OwnerFaceStatus>(`/api/camera/owner-face/status?device_id=${encodeURIComponent(device_id)}`),
  deleteOwnerFace: (device_id = "espet_s3_01") =>
    api<OwnerFaceStatus & { deleted: boolean }>(`/api/camera/owner-face?device_id=${encodeURIComponent(device_id)}`, { method: "DELETE" }),
  testOwnerDetection: (device_id = "espet_s3_01") =>
    api<{ ok: boolean; device_id: string; detected: boolean; action_sent: boolean; error?: string; owner_similarity?: number }>("/api/camera/owner-face/test-detect", json("POST", { device_id })),
};

export async function uploadOwnerFace(device_id: string, file: File): Promise<OwnerFaceStatus> {
  const form = new FormData();
  form.append("file", file);
  const res = await fetch(`/api/camera/owner-face?device_id=${encodeURIComponent(device_id)}`, {
    method: "POST",
    headers: { Authorization: `Bearer ${API_KEY}` },
    body: form,
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
  return res.json() as Promise<OwnerFaceStatus>;
}

export function cameraStreamUrl(device_id = "espet_s3_01", mode: "raw" | "processed" = "processed", fps = 8): string {
  const q = new URLSearchParams({ device_id, mode, fps: String(fps), key: API_KEY });
  return `/api/camera/stream?${q.toString()}`;
}

export function cameraSnapshotUrl(device_id = "espet_s3_01", mode: "raw" | "processed" = "processed"): string {
  const q = new URLSearchParams({ device_id, mode, key: API_KEY, t: String(Date.now()) });
  return `/api/camera/snapshot?${q.toString()}`;
}

export function ownerFacePreviewUrl(device_id = "espet_s3_01", rev = 0): string {
  const q = new URLSearchParams({ device_id, key: API_KEY, t: String(rev || Date.now()) });
  return `/api/camera/owner-face/preview?${q.toString()}`;
}

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
