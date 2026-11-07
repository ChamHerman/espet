/* Cloud Gateway — Hybrid Mode configuration:
   Telegram bot poller, server-side LLM relay, and chat bindings. */

import { Button, Input, Segmented, Space, Typography } from "antd";
import { createStyles } from "antd-style";
import { useCallback, useEffect, useState } from "react";
import { Icon } from "../components/Icon";
import { StatusChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Badge, Card, useLayout } from "../ui";

const API_KEY = "espet_secret_device_key_2026";

async function fetchJSON(url: string, opts: RequestInit = {}) {
  const res = await fetch(url, {
    ...opts,
    headers: {
      "Content-Type": "application/json",
      Authorization: `Bearer ${API_KEY}`,
      ...((opts.headers as Record<string, string>) || {}),
    },
  });
  if (!res.ok) {
    let err = res.statusText;
    try {
      const b = await res.json();
      err = b.detail || b.error || err;
    } catch {}
    throw new Error(err);
  }
  return res.json();
}

const useS = createStyles(({ css }) => ({
  stack: css`
    display: flex;
    flex-direction: column;
    gap: 20px;
  `,
  statusGrid: css`
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 14px;
    margin-bottom: 8px;
  `,
  statusBox: css`
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    padding: 14px 16px;
  `,
  statusLabel: css`
    font-size: 11px;
    color: #737373;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-weight: 500;
  `,
  statusVal: css`
    font-size: 14px;
    font-weight: 500;
    margin-top: 6px;
    display: flex;
    align-items: center;
    gap: 8px;
  `,
  formGroup: css`
    margin-bottom: 16px;
    &:last-child {
      margin-bottom: 0;
    }
  `,
  fieldLabel: css`
    display: block;
    font-size: 13px;
    font-weight: 500;
    margin-bottom: 6px;
    color: #f5f5f5;
  `,
  fieldHint: css`
    font-size: 12px;
    color: #737373;
    margin-top: 5px;
  `,
  toggleBox: css`
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 18px;
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    margin-bottom: 18px;
    gap: 16px;
  `,
  btnRow: css`
    display: flex;
    gap: 10px;
    align-items: center;
    justify-content: flex-end;
    margin-top: 18px;
  `,
  table: css`
    width: 100%;
    border-collapse: collapse;
    margin-top: 12px;
    font-size: 13px;
    th,
    td {
      text-align: left;
      padding: 10px 12px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    }
    th {
      color: #737373;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      font-weight: 600;
    }
  `,
  unbindBtn: css`
    color: #eda7a7;
    background: none;
    border: none;
    padding: 4px 8px;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: opacity 0.15s;
    &:hover {
      text-decoration: underline;
      opacity: 0.85;
    }
  `,
  bindForm: css`
    display: grid;
    grid-template-columns: 1fr 1fr auto;
    gap: 10px;
    margin-top: 14px;
    @media (max-width: 640px) {
      grid-template-columns: 1fr;
    }
  `,
}));

export function CloudView() {
  const { styles } = useS();
  const layout = useLayout();
  const { toast } = useApp();

  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [testingTg, setTestingTg] = useState(false);
  const [testingLlm, setTestingLlm] = useState(false);

  const [tgEnabled, setTgEnabled] = useState(false);
  const [tgToken, setTgToken] = useState("");
  const [llmUrl, setLlmUrl] = useState("https://api.openai.com/v1");
  const [llmKey, setLlmKey] = useState("");
  const [llmModel, setLlmModel] = useState("");

  const [pollerRunning, setPollerRunning] = useState(false);
  const [devices, setDevices] = useState<string[]>([]);
  const [bindings, setBindings] = useState<Record<string, string>>({});

  const [newChatId, setNewChatId] = useState("");
  const [newDeviceId, setNewDeviceId] = useState("");

  const loadData = useCallback(async () => {
    try {
      const cfg = await fetchJSON("/api/cloud/gateway-config");
      setTgEnabled(!!cfg.cloud_gateway_enabled);
      setTgToken(cfg.telegram_bot_token || "");
      setLlmUrl(cfg.llm_base_url || "https://api.openai.com/v1");
      setLlmKey(cfg.llm_api_key || "");
      setLlmModel(cfg.llm_model || "");
      setPollerRunning(!!cfg.poller_running);

      const devRes = await fetchJSON("/api/cloud/devices");
      setDevices(devRes.connected || []);

      const bindRes = await fetchJSON("/api/cloud/bindings");
      setBindings(bindRes.bindings || {});
    } catch (err: any) {
      toast("Failed to load cloud gateway: " + err.message, "error");
    } finally {
      setLoading(false);
    }
  }, [toast]);

  useEffect(() => {
    loadData();
  }, [loadData]);

  const onTestTg = async () => {
    const token = tgToken.trim();
    if (!token) {
      toast("Enter a bot token first", "error");
      return;
    }
    setTestingTg(true);
    try {
      const res = await fetchJSON("/api/cloud/test-telegram", {
        method: "POST",
        body: JSON.stringify({ token }),
      });
      if (res.ok && res.bot) {
        toast(`Token valid! Bot: @${res.bot.username} (${res.bot.first_name})`, "success");
      } else {
        toast("Telegram check failed: " + (res.error || "unknown error"), "error");
      }
    } catch (err: any) {
      toast("Telegram test failed: " + err.message, "error");
    } finally {
      setTestingTg(false);
    }
  };

  const onTestLlm = async () => {
    const key = llmKey.trim();
    const url = llmUrl.trim();
    const model = llmModel.trim();
    if (!key || !url) {
      toast("Enter API key and Base URL first", "error");
      return;
    }
    setTestingLlm(true);
    try {
      const res = await fetchJSON("/api/cloud/test-llm", {
        method: "POST",
        body: JSON.stringify({ api_key: key, base_url: url, model }),
      });
      if (res.ok) {
        toast(`LLM OK! [${res.model}] replied: "${res.reply}"`, "success");
      } else {
        toast("LLM check failed: " + (res.error || `HTTP ${res.status}`), "error");
      }
    } catch (err: any) {
      toast("LLM test failed: " + err.message, "error");
    } finally {
      setTestingLlm(false);
    }
  };

  const onSaveConfig = async () => {
    setSaving(true);
    try {
      const payload = {
        cloud_gateway_enabled: tgEnabled,
        telegram_bot_token: tgToken.trim(),
        llm_base_url: llmUrl.trim(),
        llm_api_key: llmKey.trim(),
        llm_model: llmModel.trim(),
      };
      await fetchJSON("/api/cloud/gateway-config", {
        method: "PUT",
        body: JSON.stringify(payload),
      });
      toast("Gateway & LLM configuration saved!", "success");
      await loadData();
    } catch (err: any) {
      toast("Save failed: " + err.message, "error");
    } finally {
      setSaving(false);
    }
  };

  const onAddBind = async () => {
    const cid = newChatId.trim();
    const did = newDeviceId.trim();
    if (!cid || !did) {
      toast("Enter both Chat ID and Device ID", "error");
      return;
    }
    try {
      await fetchJSON("/api/cloud/bind", {
        method: "POST",
        body: JSON.stringify({ chat_id: cid, device_id: did }),
      });
      setNewChatId("");
      setNewDeviceId("");
      toast(`Bound chat ${cid} → ${did}`, "success");
      await loadData();
    } catch (err: any) {
      toast("Bind failed: " + err.message, "error");
    }
  };

  const onUnbind = async (cid: string) => {
    try {
      await fetchJSON("/api/cloud/unbind", {
        method: "POST",
        body: JSON.stringify({ chat_id: String(cid) }),
      });
      toast(`Unbound chat ${cid}`, "success");
      await loadData();
    } catch (err: any) {
      toast("Unbind failed: " + err.message, "error");
    }
  };

  const bindingKeys = Object.keys(bindings);

  return (
    <div className={styles.stack}>
      {/* LIVE STATUS */}
      <Card>
        <div className={layout.styles.cardHead}>
          <div className={layout.styles.cardTitle}>
            <Icon name="server" size={16} /> Live Status
          </div>
          <StatusChip tone={pollerRunning ? "success" : "neutral"} pulse={pollerRunning}>
            {pollerRunning ? "Gateway Active" : "Gateway Idle"}
          </StatusChip>
        </div>
        <p className={layout.styles.cardSub}>
          Current runtime state of the gateway background worker and device WebSocket links.
        </p>

        <div className={styles.statusGrid}>
          <div className={styles.statusBox}>
            <div className={styles.statusLabel}>Telegram Poller</div>
            <div className={styles.statusVal}>
              <Badge tone={pollerRunning ? "success" : "default"}>
                {pollerRunning ? "Active · Polling" : "Stopped / Disabled"}
              </Badge>
            </div>
          </div>
          <div className={styles.statusBox}>
            <div className={styles.statusLabel}>Connected ESP32 Devices</div>
            <div className={styles.statusVal}>
              <Badge tone={devices.length > 0 ? "success" : "default"}>
                {devices.length > 0
                  ? `${devices.length} online (${devices.join(", ")})`
                  : "0 online (device disconnected)"}
              </Badge>
            </div>
          </div>
          <div className={styles.statusBox}>
            <div className={styles.statusLabel}>Chat ⇄ Device Bindings</div>
            <div className={styles.statusVal}>
              <Badge tone={bindingKeys.length > 0 ? "success" : "default"}>
                {bindingKeys.length} bound
              </Badge>
            </div>
          </div>
        </div>
      </Card>

      {/* TELEGRAM BOT GATEWAY */}
      <Card>
        <div className={layout.styles.cardHead}>
          <div className={layout.styles.cardTitle}>
            <Icon name="bot" size={16} /> Telegram Bot Gateway
          </div>
        </div>
        <p className={layout.styles.cardSub}>
          The server long-polls Telegram on this token and relays messages over WebSocket to the bound ESP32.
          The device never creates Telegram HTTPS sockets.
        </p>

        <div className={styles.toggleBox}>
          <div>
            <div style={{ fontWeight: 500, fontSize: 14, color: "#f5f5f5" }}>
              Enable Cloud Telegram Gateway
            </div>
            <div className={styles.fieldHint}>When off, the backend does not poll Telegram.</div>
          </div>
          <Segmented
            options={[
              { label: "Disabled", value: false },
              { label: "Enabled", value: true },
            ]}
            value={tgEnabled}
            onChange={(v) => setTgEnabled(Boolean(v))}
          />
        </div>

        <div className={styles.formGroup}>
          <label className={styles.fieldLabel}>Telegram Bot Token</label>
          <Input.Password
            value={tgToken}
            onChange={(r) => setTgToken(r.target.value)}
            placeholder="123456789:ABCdefGhIJKlmNoPQRstuVWXyz"
            className="espet-mono"
          />
          <div className={styles.fieldHint}>
            From @BotFather. Saved on server — never transmitted to ESP32 firmware.
          </div>
        </div>

        <div className={styles.btnRow}>
          <Button
            onClick={onTestTg}
            loading={testingTg}
            icon={<Icon name="check" size={14} />}
          >
            Test Bot Token
          </Button>
        </div>
      </Card>

      {/* SERVER-SIDE LLM RELAY */}
      <Card>
        <div className={layout.styles.cardHead}>
          <div className={layout.styles.cardTitle}>
            <Icon name="spark" size={16} /> Server-Side LLM Relay
          </div>
        </div>
        <p className={layout.styles.cardSub}>
          The ESP32 agent builds chat completion prompts; the backend injects the API key and forwards
          them to the provider. The device never holds the LLM key.
        </p>

        <div className={styles.formGroup}>
          <label className={styles.fieldLabel}>API Base URL</label>
          <Input
            value={llmUrl}
            onChange={(r) => setLlmUrl(r.target.value)}
            placeholder="https://api.openai.com/v1"
            className="espet-mono"
          />
          <div className={styles.fieldHint}>
            OpenAI-compatible endpoint (OpenAI, DeepSeek, OpenRouter, Ollama, LM Studio, vLLM).
          </div>
        </div>

        <div className={styles.formGroup}>
          <label className={styles.fieldLabel}>API Key</label>
          <Input.Password
            value={llmKey}
            onChange={(r) => setLlmKey(r.target.value)}
            placeholder="sk-…"
            className="espet-mono"
          />
          <div className={styles.fieldHint}>
            Stored safely on this server. Never shared with the microcontroller.
          </div>
        </div>

        <div className={styles.formGroup}>
          <label className={styles.fieldLabel}>Model Override (optional)</label>
          <Input
            value={llmModel}
            onChange={(r) => setLlmModel(r.target.value)}
            placeholder="gpt-4o-mini, deepseek-chat, qwen/qwen3.8-omni-flash:free…"
            className="espet-mono"
          />
          <div className={styles.fieldHint}>
            If left empty, the model requested by the device firmware is preserved.
          </div>
        </div>

        <div className={styles.btnRow}>
          <Button
            onClick={onTestLlm}
            loading={testingLlm}
            icon={<Icon name="spark" size={14} />}
          >
            Test LLM Connection
          </Button>
        </div>
      </Card>

      {/* TELEGRAM CHAT ⇄ DEVICE BINDINGS */}
      <Card>
        <div className={layout.styles.cardHead}>
          <div className={layout.styles.cardTitle}>
            <Icon name="send" size={16} /> Telegram Chat ⇄ Device Bindings
          </div>
        </div>
        <p className={layout.styles.cardSub}>
          Incoming Telegram messages from a chat ID are forwarded only to the matching ESP32 device ID.
          Auto-binds on the first message if exactly one device is online.
        </p>

        <table className={styles.table}>
          <thead>
            <tr>
              <th>Telegram Chat ID</th>
              <th>ESP32 Device ID</th>
              <th style={{ width: 80 }}>Action</th>
            </tr>
          </thead>
          <tbody>
            {bindingKeys.length === 0 ? (
              <tr>
                <td colSpan={3} style={{ color: "#737373", textAlign: "center", padding: 24 }}>
                  No bindings registered yet.
                </td>
              </tr>
            ) : (
              bindingKeys.map((k) => (
                <tr key={k}>
                  <td className="espet-mono">{k}</td>
                  <td className="espet-mono">{bindings[k]}</td>
                  <td>
                    <button className={styles.unbindBtn} onClick={() => onUnbind(k)}>
                      Unbind
                    </button>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>

        <div className={styles.bindForm}>
          <Input
            value={newChatId}
            onChange={(r) => setNewChatId(r.target.value)}
            placeholder="Chat ID (e.g. 123456789)"
            className="espet-mono"
          />
          <Input
            value={newDeviceId}
            onChange={(r) => setNewDeviceId(r.target.value)}
            placeholder="Device ID (e.g. espet-device)"
            className="espet-mono"
          />
          <Button onClick={onAddBind} icon={<Icon name="plus" size={14} />}>
            Bind
          </Button>
        </div>
      </Card>

      {/* BOTTOM ACTIONS */}
      <div className={styles.btnRow} style={{ marginTop: 10 }}>
        <Button onClick={loadData} icon={<Icon name="refresh" size={14} />}>
          Reload
        </Button>
        <Button
          type="primary"
          onClick={onSaveConfig}
          loading={saving}
          icon={<Icon name="save" size={14} />}
          style={{ paddingInline: 26 }}
        >
          Save Configuration &amp; Restart Gateway
        </Button>
      </div>

      <div className={layout.styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>
          Hybrid Mode keeps secrets and heavy network polling on your local server, while the ESP32
          focuses on real-time voice, hardware control, and local sensors over the lightweight LAN
          WebSocket protocol.
        </span>
      </div>
    </div>
  );
}
