/* ESPET dashboard API types — mirror espet-backend/config_store.py + routers. */

export interface Versions {
  user: number;
  soul: number;
  memory: number;
  device: number;
  jobs: number;
  avatar: number;
  skills: number;
}

export interface DeviceSettings {
  name: string;
  timezone: string;
  posix_tz?: string;
  sync_interval_s: number;
}

export interface Job {
  id: string;
  name: string;
  enabled: boolean;
  kind: "every" | "at";
  message: string;
  channel: "system" | "telegram";
  chat_id: string;
  delete_after_run: boolean;
  created_at?: string;
  interval_s?: number;
  at_epoch?: number;
  at_local?: string;
}

export interface Skill {
  id: string;
  name: string;
  filename: string;
  content: string;
  enabled: boolean;
  created_at?: string;
}

export interface ConfigBundle {
  versions: Versions;
  user_md: string;
  soul_md: string;
  memory_md: string;
  device: DeviceSettings;
  jobs: Job[];
  skills?: Skill[];
}

export interface AvatarState {
  mode: "auto" | "custom";
  opts: Record<string, string | number | boolean>;
  versions?: Versions;
}

export interface ServerStatus {
  connected: boolean;
  tool_count: number;
}

export interface Health {
  status: string;
  timestamp: string;
  version: string;
  mcp_servers: Record<string, ServerStatus>;
  total_tools: number;
}

export interface AuthStatus {
  authenticated: boolean;
  email?: string;
  has_refresh_token?: boolean;
  token_valid?: boolean;
  token_updated_at?: string | null;
}

export interface McpTool {
  name: string;
  description?: string;
  input_schema?: Record<string, unknown>;
  server?: string;
}

export interface MemoryItem {
  id: number;
  user_id: string;
  device_id: string;
  source_type: string;
  source_ref?: string;
  content: string;
  metadata_json?: string;
  created_at: string;
  updated_at: string;
}

export interface MemoryQueryResult {
  id: number;
  content: string;
  source_type: string;
  source_ref?: string;
  created_at: string;
  distance: number;
  similarity: number;
}

export interface MemoryQueryResponse {
  query: string;
  device_id: string;
  latency_ms: number;
  count: number;
  results: MemoryQueryResult[];
}
