/* Home — the landing surface for new ESPET owners. Answers "is my setup
   healthy?" at a glance: an onboarding checklist, stat cards, charts derived
   from existing APIs (no new backend), the device summary and Google OAuth. */

import { Google } from "@lobehub/icons";
import { AreaChart, DonutChart } from "@lobehub/charts";
import { Button, Progress, Statistic } from "antd";
import { createStyles } from "antd-style";
import { useCallback, useEffect, useMemo, useState } from "react";
import { client } from "../api";
import { Icon, type IconName } from "../components/Icon";
import { humanInterval, useApp } from "../store";
import { Badge, Card, KV, useLayout } from "../ui";

const useS = createStyles(({ css }) => ({
  hero: css`
    display: flex;
    flex-direction: column;
    gap: 6px;
    padding: 26px 24px;
  `,
  hello: css`
    font-size: 1.5rem;
    font-weight: 700;
    letter-spacing: -0.2px;
  `,
  statIcon: css`
    display: grid;
    place-items: center;
    width: 34px;
    height: 34px;
    border-radius: 10px;
    color: #f5f5f5;
    background: rgba(255, 255, 255, 0.1);
    flex: none;
  `,
  statHead: css`
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 8px;
    color: #a3a3a3;
    font-size: 0.82rem;
    font-weight: 500;
    text-transform: uppercase;
    letter-spacing: 0.4px;
  `,
  checkRow: css`
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 11px 14px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
  `,
  tick: css`
    width: 26px;
    height: 26px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    flex: none;
    color: #050505;
    background: #f5f5f5;
  `,
  untick: css`
    width: 26px;
    height: 26px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    flex: none;
    color: #a3a3a3;
    border: 1.5px dashed rgba(163,163,163, 0.5);
  `,
}));

/* monochrome chart palette (white → graphite ramp) */
const CHART_GRAYSCALE = ["#fafafa", "#d4d4d4", "#a3a3a3", "#737373", "#525252"];

/* Client-side health sampler: records total-tool count every 30 s into a
   localStorage ring so the Home sparkline has a real series without a backend
   metrics endpoint (that lives in the next plan). */
const HEALTH_KEY = "espet.healthHistory";
function sampleHealth(total: number) {
  try {
    const raw = localStorage.getItem(HEALTH_KEY);
    const arr: { t: number; v: number }[] = raw ? JSON.parse(raw) : [];
    const now = Date.now();
    if (!arr.length || now - arr[arr.length - 1].t > 20000) {
      arr.push({ t: now, v: total });
      while (arr.length > 60) arr.shift();
      localStorage.setItem(HEALTH_KEY, JSON.stringify(arr));
    }
    return arr;
  } catch {
    return [];
  }
}

export function HomeView() {
  const { config, health, auth, refreshHealth, refreshAuth, toast, patchConfig } =
    useApp();
  const { styles } = useLayout();
  const { styles: s } = useS();
  const [polling, setPolling] = useState(false);
  const [series, setSeries] = useState<{ t: number; v: number }[]>([]);

  useEffect(() => {
    if (health) setSeries(sampleHealth(health.total_tools));
  }, [health]);

  const go = (hash: string) => {
    location.hash = hash;
  };

  const loginWithGoogle = useCallback(async () => {
    let seenUpdatedAt: string | null | undefined;
    try {
      seenUpdatedAt = (await client.auth()).token_updated_at || null;
    } catch {
      /* backend down */
    }
    const popup = window.open(
      "/auth/google",
      "esp_google_oauth",
      "width=520,height=640"
    );
    if (!popup) {
      toast("Popup blocked — allow popups, or open /auth/google directly", "error");
      return;
    }
    toast("Waiting for Google login in the popup…", "info");
    setPolling(true);
    const started = Date.now();
    const timer = setInterval(async () => {
      try {
        const st = await client.auth();
        const changed =
          st.token_updated_at && st.token_updated_at !== seenUpdatedAt;
        if (st.authenticated && (seenUpdatedAt === null || changed)) {
          clearInterval(timer);
          setPolling(false);
          toast("Google linked ✓ — reconnecting MCP servers…");
          try {
            await client.reconnectMcp();
          } catch {
            /* non-fatal */
          }
          await refreshHealth();
          await refreshAuth();
        } else if (Date.now() - started > 180000) {
          clearInterval(timer);
          setPolling(false);
          toast("Google login timed out — try again", "error");
        }
      } catch {
        /* transient — keep polling */
      }
    }, 1000);
  }, [toast, refreshHealth, refreshAuth]);

  const dev = config?.device;
  const jobs = config?.jobs ?? [];
  const recurring = jobs.filter((j) => j.kind === "every").length;
  const servers = Object.entries(health?.mcp_servers ?? {});
  const connectedServers = servers.filter(([, v]) => v.connected);

  const donutData = connectedServers.map(([name, v]) => ({
    name: name.replace("google-", "Google "),
    value: v.tool_count,
  }));

  const checklist = useMemo(() => {
    const items: {
      label: string;
      done: boolean;
      hint: string;
      action?: () => void;
      icon: IconName;
    }[] = [
      {
        label: "Backend running",
        done: !!health,
        hint: "The FastAPI companion service on this network.",
        icon: "server",
      },
      {
        label: "Google account linked",
        done: !!auth?.authenticated,
        hint: "Enables Gmail, Calendar, Docs and Drive tools.",
        action: auth?.authenticated ? undefined : loginWithGoogle,
        icon: "key",
      },
      {
        label: "Agent personalised",
        done: !!config && config.versions.soul > 1,
        hint: "Give your agent a personality on the Assistant page.",
        action: () => go("#personality"),
        icon: "sparkles",
      },
      {
        label: "Device address saved",
        done: !!localStorage.getItem("espet.deviceAddr"),
        hint: "Lets the dashboard reach the device's live chat.",
        action: () => go("#device"),
        icon: "cpu",
      },
      {
        label: "Cloud Gateway & LLM configured",
        done: false,
        hint: "Hybrid mode: Telegram polling & LLM relay on the server.",
        action: () => { window.location.href = "/cloud"; },
        icon: "bot",
      },
    ];
    return items;
  }, [health, auth, config, loginWithGoogle]);

  const doneCount = checklist.filter((c) => c.done).length;

  if (!config)
    return (
      <Card>
        <div className={styles.empty}>
          <Icon name="dashboard" size={28} />
          <div>{health ? "Loading configuration…" : "Backend unreachable"}</div>
        </div>
      </Card>
    );

  return (
    <div className={styles.stack}>
      <Card className={s.hero}>
        <div className={s.hello}>
          {dev?.name || "ESPET"}{" "}
          <span style={{ color: "#a3a3a3" }}>is {config.device.posix_tz ? "online & configured" : "ready to configure"}.</span>
        </div>
        <div style={{ color: "#a3a3a3" }}>
          Welcome to your ESPET Control Center — set your agent up in a few
          steps, or explore the menus above.
        </div>
      </Card>

      <div className={`${styles.grid} ${styles.cols3}`}>
        <Card>
          <div className={s.statHead}>
            <span className={s.statIcon}>
              <Icon name="server" size={18} />
            </span>
            Backend
          </div>
          <Statistic
            value={health ? "Healthy" : "Down"}
            valueStyle={{ color: health ? "#8fd9ae" : "#eda7a7", fontSize: 26 }}
          />
          <div style={{ color: "#737373", fontSize: 13, marginTop: 4 }}>
            {health ? `${health.total_tools} MCP tools · ${servers.length} servers` : "Unreachable"}
          </div>
        </Card>
        <Card>
          <div className={s.statHead}>
            <span className={s.statIcon}>
              <Icon name="key" size={18} />
            </span>
            Google account
          </div>
          <Statistic
            value={auth?.authenticated ? "Linked" : "Not Linked"}
            valueStyle={{
              color: auth?.authenticated ? "#8fd9ae" : "#a3a3a3",
              fontSize: 26,
            }}
          />
          <div style={{ color: "#737373", fontSize: 13, marginTop: 4, minHeight: 18 }}>
            {auth?.authenticated ? auth.email || "Google Account" : "Link to enable Gmail, Calendar, Drive"}
          </div>
          <div style={{ marginTop: 10 }}>
            {auth?.authenticated ? (
              <Button
                size="small"
                icon={<Google size={14} />}
                onClick={loginWithGoogle}
              >
                Re-link
              </Button>
            ) : (
              <Button
                type="primary"
                size="small"
                loading={polling}
                icon={<Google size={14} />}
                onClick={loginWithGoogle}
              >
                Login with Google
              </Button>
            )}
          </div>
        </Card>
        <Card>
          <div className={s.statHead}>
            <span className={s.statIcon}>
              <Icon name="calendar" size={18} />
            </span>
            Scheduled jobs
          </div>
          <Statistic value={jobs.length} valueStyle={{ fontSize: 26 }} />
          <div style={{ color: "#737373", fontSize: 13, marginTop: 4 }}>
            {recurring} recurring · {jobs.length - recurring} one-time
          </div>
        </Card>
      </div>

      {/* Onboarding checklist */}
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="check" /> Get started
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
            <span style={{ color: "#737373", fontSize: 13 }}>
              {doneCount}/{checklist.length}
            </span>
            <Progress
              percent={Math.round((doneCount / checklist.length) * 100)}
              size="small"
              style={{ width: 120 }}
            />
          </div>
        </div>
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          {checklist.map((c) => (
            <div className={s.checkRow} key={c.label}>
              <span className={c.done ? s.tick : s.untick}>
                {c.done ? <Icon name="check" size={15} /> : <Icon name={c.icon} size={14} />}
              </span>
              <div className={styles.grow}>
                <div className={styles.name}>{c.label}</div>
                <div className={styles.meta}>{c.hint}</div>
              </div>
              {!c.done && c.action && (
                <Button size="small" onClick={c.action}>
                  Set up
                </Button>
              )}
            </div>
          ))}
        </div>
      </Card>

      {/* Device + versions */}
      <div className={`${styles.grid} ${styles.cols2}`}>
        <Card>
          <div className={styles.cardHead}>
            <div className={styles.cardTitle}>
              <Icon name="cpu" /> {dev?.name || "Device"}
            </div>
            <Badge tone="success">pulls every {humanInterval(dev?.sync_interval_s ?? 300)}</Badge>
          </div>
          <KV
            rows={[
              { k: "Timezone", v: dev?.timezone ?? "—" },
              { k: "POSIX TZ on device", v: dev?.posix_tz ?? "UTC0" },
              { k: "Boot behavior", v: "pull once on startup" },
            ]}
          />
        </Card>
        <Card>
          <div className={styles.cardHead}>
            <div className={styles.cardTitle}>
              <Icon name="server" /> Google MCP servers
            </div>
            <Badge>
              {connectedServers.length}/{servers.length} connected
            </Badge>
          </div>
          {servers.length ? (
            <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
              {servers.map(([name, s]) => {
                let displayName = name.replace("google-", "Google ");
                displayName = displayName.charAt(0).toUpperCase() + displayName.slice(1);
                return (
                  <div
                    key={name}
                    style={{
                      display: "flex",
                      alignItems: "center",
                      justifyContent: "space-between",
                      padding: "9px 12px",
                      borderRadius: 10,
                      background: "rgba(255, 255, 255, 0.03)",
                      border: "1px solid rgba(255, 255, 255, 0.06)",
                    }}
                  >
                    <div style={{ display: "flex", alignItems: "center", gap: 9 }}>
                      <span
                        style={{
                          width: 7,
                          height: 7,
                          borderRadius: "50%",
                          background: s.connected ? "#8fd9ae" : "#eda7a7",
                          boxShadow: s.connected ? "0 0 6px rgba(143,217,174,0.5)" : "none",
                        }}
                      />
                      <span style={{ fontWeight: 500, fontSize: 13.5, color: "#f5f5f5" }}>
                        {displayName}
                      </span>
                    </div>
                    <span style={{ fontSize: 12.5, color: "#a3a3a3" }}>
                      {s.tool_count} tools
                    </span>
                  </div>
                );
              })}
            </div>
          ) : (
            <div className={styles.empty}>
              <Icon name="server" size={26} />
              <div>
                {auth?.authenticated
                  ? "No MCP servers reported yet — try Re-link."
                  : "Link Google to load Gmail, Calendar, Docs & Drive."}
              </div>
            </div>
          )}
        </Card>
      </div>

      {/* Health sparkline */}
      {series.length > 2 && (
        <Card>
          <div className={styles.cardHead}>
            <div className={styles.cardTitle}>
              <Icon name="spark" /> Available MCP tools (this session)
            </div>
            <span style={{ color: "#737373", fontSize: 12 }}>
              sampled client-side every 30s
            </span>
          </div>
          <AreaChart
            data={series.map((p) => ({
              t: new Date(p.t).toLocaleTimeString([], {
                hour: "2-digit",
                minute: "2-digit",
              }),
              tools: p.v,
            }))}
            index="t"
            categories={["tools"]}
            colors={CHART_GRAYSCALE}
            height={160}
          />
        </Card>
      )}
    </div>
  );
}
