/* App-wide store: config / health / auth caches, dirty tracking, version
   chip, toast + confirm helpers (antd App context). Views read config via
   useApp() and mutate through helpers that refresh the version chip. */

import { App as AntdApp } from "antd";
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react";
import { client, type DeviceInfo } from "./api";
import type { AuthStatus, ConfigBundle, Health } from "./types";

export type Section = "profile" | "soul" | "memory" | "device";

/** Toast semantic: success = dimmed mint, error = dimmed red,
    info/general = neutral glass (no hue). */
export type ToastKind = "success" | "error" | "info";

interface Store {
  config: ConfigBundle | null;
  health: Health | null;
  auth: AuthStatus | null;
  dirty: Partial<Record<Section, boolean>>;
  maxVersion: number | null;
  serverOnline: boolean;
  selectedDeviceId: string;
  devices: DeviceInfo[];
  setSelectedDeviceId: (id: string) => void;
  refreshDevices: () => Promise<void>;
  loadConfig: (device_id?: string) => Promise<ConfigBundle>;
  refreshAuth: () => Promise<void>;
  patchConfig: (fn: (c: ConfigBundle) => void) => void;
  setDirty: (s: Section, v: boolean) => void;
  clearAllDirty: () => void;
  anyDirty: boolean;
  toast: (msg: string, kind?: ToastKind) => void;
  confirm: (opts: {
    title: string;
    message: string;
    okLabel?: string;
    danger?: boolean;
  }) => Promise<boolean>;
}

const Ctx = createContext<Store | null>(null);

export function useApp(): Store {
  const v = useContext(Ctx);
  if (!v) throw new Error("useApp outside StoreProvider");
  return v;
}

export function StoreProvider({ children }: { children: ReactNode }) {
  const { message, modal } = AntdApp.useApp();
  const [config, setConfig] = useState<ConfigBundle | null>(null);
  const [health, setHealth] = useState<Health | null>(null);
  const [auth, setAuth] = useState<AuthStatus | null>(null);
  const [dirty, setDirtyMap] = useState<Partial<Record<Section, boolean>>>({});
  const [selectedDeviceId, setSelectedDeviceIdState] = useState<string>(
    () => localStorage.getItem("espet.selected_device_id") || ""
  );
  const [devices, setDevices] = useState<DeviceInfo[]>([]);
  const selectedDevRef = useRef(selectedDeviceId);
  selectedDevRef.current = selectedDeviceId;

  const setSelectedDeviceId = useCallback((id: string) => {
    setSelectedDeviceIdState(id);
    localStorage.setItem("espet.selected_device_id", id);
  }, []);

  const refreshDevices = useCallback(async () => {
    try {
      const res = await client.devices();
      setDevices(res.devices || []);
      const saved = localStorage.getItem("espet.selected_device_id");
      const matched = res.devices.find((d) => d.device_id === saved);
      const chosen = matched ? matched.device_id : (res.latest_device_id || "espet_s3_01");
      if (chosen !== selectedDevRef.current) {
        setSelectedDeviceIdState(chosen);
        localStorage.setItem("espet.selected_device_id", chosen);
      }
    } catch {
      /* ignore */
    }
  }, []);

  const loadConfig = useCallback(async (device_id?: string) => {
    const target = device_id || selectedDevRef.current || undefined;
    const c = await client.config(target);
    setConfig(c);
    return c;
  }, []);

  const refreshHealth = useCallback(async () => {
    try {
      setHealth(await client.health());
    } catch {
      setHealth(null);
    }
  }, []);

  const refreshAuth = useCallback(async () => {
    try {
      setAuth(await client.auth());
    } catch {
      /* backend down / not linked yet — leave as-is */
    }
  }, []);

  useEffect(() => {
    (async () => {
      let initialDev = localStorage.getItem("espet.selected_device_id") || "";
      try {
        const res = await client.devices();
        setDevices(res.devices || []);
        if (!initialDev || !res.devices.some((d) => d.device_id === initialDev)) {
          initialDev = res.latest_device_id || (res.devices[0]?.device_id ?? "espet_s3_01");
          localStorage.setItem("espet.selected_device_id", initialDev);
        }
        setSelectedDeviceIdState(initialDev);
      } catch {
        if (!initialDev) initialDev = "espet_s3_01";
      }
      loadConfig(initialDev).catch(() => setConfig(null));
    })();
    refreshHealth();
    refreshAuth();
    const t = setInterval(refreshHealth, 30000);
    return () => clearInterval(t);
  }, [loadConfig, refreshHealth, refreshAuth]);
  const patchConfig = useCallback((fn: (c: ConfigBundle) => void) => {
    setConfig((prev) => {
      if (!prev) return prev;
      const next: ConfigBundle = {
        ...prev,
        versions: { ...prev.versions },
        device: { ...prev.device },
      };
      fn(next);
      return next;
    });
  }, []);

  const setDirty = useCallback((s: Section, v: boolean) => {
    setDirtyMap((d) => ({ ...d, [s]: v }));
  }, []);
  const clearAllDirty = useCallback(() => setDirtyMap({}), []);

  const toast = useCallback(
    (msg: string, kind: ToastKind = "success") => {
      if (kind === "error") message.error(msg);
      else if (kind === "info") message.info(msg);
      else message.success(msg);
    },
    [message]
  );

  const confirm = useCallback(
    ({
      title,
      message: body,
      okLabel = "Confirm",
      danger,
    }: {
      title: string;
      message: string;
      okLabel?: string;
      danger?: boolean;
    }) =>
      new Promise<boolean>((resolve) => {
        modal.confirm({
          title,
          content: body,
          okText: okLabel,
          cancelText: "Cancel",
          okButtonProps: danger ? { danger: true } : undefined,
          centered: true,
          onOk: () => resolve(true),
          onCancel: () => resolve(false),
        });
      }),
    [modal]
  );

  const maxVersion = useMemo(
    () => (config ? Math.max(...Object.values(config.versions)) : null),
    [config]
  );

  const value: Store = {
    config,
    health,
    auth,
    dirty,
    maxVersion,
    serverOnline: !!health,
    selectedDeviceId,
    devices,
    setSelectedDeviceId,
    refreshDevices,
    loadConfig,
    refreshHealth,
    refreshAuth,
    patchConfig,
    setDirty,
    clearAllDirty,
    anyDirty: Object.values(dirty).some(Boolean),
    toast,
    confirm,
  };

  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

/* ── shared formatting helpers (ported from legacy app.js) ─────── */
export function humanInterval(s: number): string {
  if (s % 604800 === 0 && s >= 604800)
    return `${s / 604800} week${s > 604800 ? "s" : ""}`;
  if (s % 86400 === 0 && s >= 86400)
    return `${s / 86400} day${s > 86400 ? "s" : ""}`;
  if (s % 3600 === 0) return `${s / 3600} hour${s >= 7200 ? "s" : ""}`;
  return `${Math.round(s / 60)} min`;
}

export function humanBytes(n: number): string {
  return n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`;
}

export const byteLen = (s: string) => new Blob([s]).size;
