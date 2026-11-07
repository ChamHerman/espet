/* Device — identity/timezone/sync (PUT /api/ui/device), messaging-channel
   summary, the config-version pull table, and the device LAN address used by
   the Chat view to reach the ESP32 WebSocket gateway directly. */

import { Button, Input, Select } from "antd";
import { useState } from "react";
import { client, getDeviceAddr, setDeviceAddr } from "../api";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { humanInterval, useApp } from "../store";
import { Badge, Card, KV, useLayout } from "../ui";
import { TIMEZONES } from "./deviceZones";

const SYNC_INTERVALS = [60, 300, 900, 1800, 3600];

export function DeviceView() {
  const { config, patchConfig, setDirty, toast } = useApp();
  const { styles } = useLayout();
  const dev = config?.device;
  const [name, setName] = useState(dev?.name ?? "");
  const [tz, setTz] = useState(dev?.timezone ?? "");
  const [interval, setIntervalS] = useState(dev?.sync_interval_s ?? 300);
  const [addr, setAddr] = useState(getDeviceAddr());
  const [savedAddr, setSavedAddr] = useState(getDeviceAddr());
  if (!config || !dev) return null;

  const dirty =
    name.trim() !== dev.name ||
    tz !== dev.timezone ||
    interval !== dev.sync_interval_s;

  const save = async () => {
    try {
      const res = await client.saveDevice({
        name: name.trim(),
        timezone: tz,
        sync_interval_s: interval,
      });
      patchConfig((c) => {
        c.device = res.device;
        c.versions.device = res.versions.device;
      });
      setDirty("device", false);
      toast("Device settings saved — device will pull them");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  const versionRows = [
    { k: "USER.md — profile", v: `v${config.versions.user}` },
    { k: "SOUL.md — personality", v: `v${config.versions.soul}` },
    { k: "MEMORY.md — memory", v: `v${config.versions.memory}` },
    { k: "cron.json — schedules", v: `v${config.versions.jobs}` },
    { k: "skills/*.md — playbooks", v: `v${config.versions.skills}` },
    { k: "Timezone (TZ)", v: dev.posix_tz || "UTC0" },
  ];

  const channels = [
    {
      name: "Hybrid Cloud Gateway & LLM Relay",
      meta: "Backend polls Telegram and injects LLM API key; device connects via persistent WebSocket. Configure at /cloud.",
      badge: "Cloud Mode (/cloud)",
      tone: "success" as const,
    },
    {
      name: "Telegram (Direct)",
      meta: "Bot token stored on the device (NVS — set via serial CLI). Cron jobs and the agent can deliver messages to any chat ID.",
      badge: "Direct Mode",
      tone: "default" as const,
    },
    {
      name: "Feishu / Lark",
      meta: "App credentials stored on the device (NVS). Replies can be routed to Feishu chats.",
      badge: "On device",
      tone: "success" as const,
    },
    {
      name: "Google (Gmail · Calendar · Drive)",
      meta: "Managed by this backend — link or relink the account from the Home page.",
      badge: "Backend",
      tone: "default" as const,
    },
    {
      name: "Live chat (this dashboard)",
      meta: "Browser connects straight to the device WebSocket gateway — a transparent transcript of prompts and replies.",
      badge: "Browser ⇄ Device",
      tone: "default" as const,
    },
  ];

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="cpu" /> Device settings
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="device" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              v{config.versions.device}
            </span>
          </div>
        </div>
        <div className={`${styles.grid} ${styles.cols2}`} style={{ marginBottom: 14 }}>
          <div>
            <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
              Device name
            </div>
            <Input
              maxLength={31}
              value={name}
              onChange={(e) => {
                setName(e.target.value);
                setDirty(
                  "device",
                  e.target.value.trim() !== dev.name ||
                    tz !== dev.timezone ||
                    interval !== dev.sync_interval_s
                );
              }}
            />
            <div style={{ fontSize: 12, color: "#737373", marginTop: 4 }}>
              Shown in the dashboard header cards. Max 31 chars.
            </div>
          </div>
          <div>
            <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
              Timezone
            </div>
            <Select
              style={{ width: "100%" }}
              value={tz}
              onChange={(v) => {
                setTz(v);
                setDirty(
                  "device",
                  name.trim() !== dev.name || v !== dev.timezone || interval !== dev.sync_interval_s
                );
              }}
              options={[
                ...TIMEZONES.map((t) => ({ value: t, label: t })),
                ...(TIMEZONES.includes(dev.timezone)
                  ? []
                  : [{ value: dev.timezone, label: dev.timezone }]),
              ]}
            />
            <div style={{ fontSize: 12, color: "#737373", marginTop: 4 }}>
              Browser is in {Intl.DateTimeFormat().resolvedOptions().timeZone}.
              Applied on the device via TZ.
            </div>
          </div>
        </div>
        <div style={{ marginBottom: 6, maxWidth: 420 }}>
          <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
            Config sync interval
          </div>
          <Select
            style={{ width: "100%" }}
            value={interval}
            onChange={(v) => {
              setIntervalS(v);
              setDirty(
                "device",
                name.trim() !== dev.name || tz !== dev.timezone || v !== dev.sync_interval_s
              );
            }}
            options={[
              ...SYNC_INTERVALS.map((s) => ({ value: s, label: `Every ${humanInterval(s)}` })),
              ...(SYNC_INTERVALS.includes(dev.sync_interval_s)
                ? []
                : [
                    {
                      value: dev.sync_interval_s,
                      label: `Every ${humanInterval(dev.sync_interval_s)} (custom)`,
                    },
                  ]),
            ]}
          />
          <div style={{ fontSize: 12, color: "#737373", marginTop: 4 }}>
            How often the ESP32 pulls this config (it also pulls once at boot).
          </div>
        </div>
        <div style={{ display: "flex", justifyContent: "flex-end", marginTop: 14 }}>
          <Button
            type="primary"
            disabled={!dirty}
            icon={<Icon name="save" size={15} />}
            onClick={save}
          >
            Save device settings
          </Button>
        </div>
      </Card>

      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="message" /> Live-chat device address
          </div>
          <Badge tone={addr === savedAddr ? "default" : "warning"}>
            {addr === savedAddr ? "Saved Locally" : "Unsaved (This Browser)"}
          </Badge>
        </div>
        <p className={styles.cardSub}>
          The Chat view opens a WebSocket straight to the ESP32 gateway at
          this address (port 18789). It is stored only in this browser — no
          backend involvement.
        </p>
        <div style={{ display: "flex", gap: 10, alignItems: "flex-start", flexWrap: "wrap" }}>
          <Input
            className="espet-mono"
            style={{ maxWidth: 360 }}
            value={addr}
            placeholder="espet.local or 192.168.1.42"
            onChange={(e) => setAddr(e.target.value)}
            onPressEnter={() => {
              setDeviceAddr(addr);
              setSavedAddr(addr.trim());
              toast("Device address saved — Chat will use it");
            }}
          />
          <Button
            type="primary"
            onClick={() => {
              setDeviceAddr(addr);
              setSavedAddr(addr.trim());
              toast("Device address saved — Chat will use it");
            }}
          >
            Save address
          </Button>
        </div>
        <div className={styles.sectionNote} style={{ marginTop: 12 }}>
          <Icon name="info" size={16} />
          <span>
            Don’t know the IP? Run <b>espet&gt; wifi_status</b> on the serial
            console (UART, 115200 baud). The gateway also answers on{" "}
            <span className="espet-mono">espet.local</span> when your network
            resolves mDNS.
          </span>
        </div>
      </Card>

      <div className={`${styles.grid} ${styles.cols2}`}>
        <Card>
          <div className={styles.cardHead}>
            <div className={styles.cardTitle}>
              <Icon name="send" /> Messaging channels
            </div>
            <a
              href="/cloud"
              style={{
                fontSize: 12,
                color: "#93c5fd",
                textDecoration: "none",
                padding: "3px 10px",
                borderRadius: 999,
                border: "1px solid rgba(147,197,253,0.3)",
                background: "rgba(147,197,253,0.08)",
              }}
            >
              Configure Cloud Gateway ↗
            </a>
          </div>
          <div style={{ display: "flex", flexDirection: "column", gap: 9 }}>
            {channels.map((c) => (
              <div className={styles.rowItem} key={c.name}>
                <div className={styles.grow}>
                  <div className={styles.name}>{c.name}</div>
                  <div className={styles.meta}>{c.meta}</div>
                </div>
                <Badge tone={c.tone}>{c.badge}</Badge>
              </div>
            ))}
          </div>
        </Card>

        <Card>
          <div className={styles.cardHead}>
            <div className={styles.cardTitle}>
              <Icon name="refresh" /> What the device pulls
            </div>
          </div>
          <KV rows={versionRows} />
          <div className={styles.sectionNote} style={{ marginTop: 12 }}>
            <Icon name="shield" size={16} />
            <span>
              Transport is plain HTTP on your LAN with a shared device API key
              (Bearer). Serve over HTTPS before exposing beyond your network.
            </span>
          </div>
        </Card>
      </div>
    </div>
  );
}
