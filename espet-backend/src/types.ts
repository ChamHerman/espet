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
