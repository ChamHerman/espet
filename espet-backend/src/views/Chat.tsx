/* Chat — Desktop console via the FastAPI Cloud Gateway (/api/cloud/ws/chat).
   Routes inbound message to the ESP32 agent loop over persistent cloud_link
   under session chat_id="dashboard", keeping it completely isolated from Telegram
   conversations while sharing SOUL.md, USER.md, and MEMORY.md. */

import { Markdown } from "@lobehub/ui";
import {
  Alert,
  Avatar,
  Button,
  Image,
  Input,
  Space,
  Spin,
  Tooltip,
} from "antd";
import { createStyles } from "antd-style";
import { useCallback, useEffect, useRef, useState } from "react";
import { Icon } from "../components/Icon";
import { StatusChip } from "../components/StatusChips";
import { useApp } from "../store";

const CHAT_KEY = "espet.chat.v1";
const LOAD_TIMEOUT_MS = 60000;
const MAX_TURNS = 80;

type Role = "user" | "agent" | "system";
interface ChatMsg {
  id: string;
  role: Role;
  text: string;
  image?: string;
  ts: number;
}

const useS = createStyles(({ css }) => ({
  wrap: css`
    display: flex;
    flex-direction: column;
    height: calc(100dvh - var(--espet-nav-h) - 32px);
    min-height: 420px;
  `,
  head: css`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 10px 2px;
  `,
  list: css`
    flex: 1 1 auto;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 8px 4px 16px;
    scroll-behavior: smooth;
  `,
  row: css`
    display: flex;
    gap: 10px;
    max-width: 860px;
  `,
  rowUser: css`
    margin-left: auto;
    flex-direction: row-reverse;
  `,
  bubble: css`
    padding: 10px 14px;
    border-radius: 16px;
    line-height: 1.6;
    color: #e8e8e8;
    max-width: 78%;
    word-break: break-word;
  `,
  agentBubble: css`
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-top-left-radius: 5px;
  `,
  userBubble: css`
    background: rgba(255, 255, 255, 0.13);
    border: 1px solid rgba(255, 255, 255, 0.24);
    border-top-right-radius: 5px;
  `,
  system: css`
    align-self: center;
    font-size: 12px;
    color: #737373;
  `,
  composer: css`
    display: flex;
    gap: 10px;
    align-items: flex-end;
    padding: 12px;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.06);
    backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.1);
  `,
  logo: css`
    width: 40px;
    height: 40px;
    border-radius: 11px;
    flex: none;
  `,
}));

function load(): ChatMsg[] {
  try {
    return JSON.parse(localStorage.getItem(CHAT_KEY) || "[]");
  } catch {
    return [];
  }
}

function save(msgs: ChatMsg[]) {
  try {
    localStorage.setItem(CHAT_KEY, JSON.stringify(msgs.slice(-MAX_TURNS)));
  } catch {
    /* quota — ignore */
  }
}

function getWsChatUrl(): string {
  const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  return `${proto}//${window.location.host}/api/cloud/ws/chat`;
}

export function ChatView() {
  const { toast } = useApp();
  const { styles, cx } = useS();
  const [msgs, setMsgs] = useState<ChatMsg[]>(load);
  const [status, setStatus] = useState<"idle" | "connecting" | "open" | "closed">(
    "idle"
  );
  const [deviceOnline, setDeviceOnline] = useState(false);
  const [pending, setPending] = useState(false);
  const [draft, setDraft] = useState("");
  const wsRef = useRef<WebSocket | null>(null);
  const timeoutRef = useRef<ReturnType<typeof setTimeout>>();
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout>>();
  const idRef = useRef(0);
  const pendingRef = useRef(false);
  const listRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    listRef.current?.scrollTo({ top: listRef.current.scrollHeight });
  }, [msgs, pending]);

  const push = useCallback((m: Omit<ChatMsg, "id" | "ts">) => {
    setMsgs((prev) => {
      const next = [
        ...prev,
        { ...m, id: `${Date.now()}-${Math.random().toString(36).slice(2, 6)}`, ts: Date.now() },
      ];
      save(next);
      return next.slice(-MAX_TURNS);
    });
  }, []);

  const pushRaw = useCallback((m: ChatMsg) => {
    setMsgs((prev) => {
      if (prev.some((x) => x.id === m.id)) return prev;
      const next = [...prev, m];
      save(next);
      return next.slice(-MAX_TURNS);
    });
  }, []);

  const clearPending = useCallback(() => {
    setPending(false);
    if (timeoutRef.current) clearTimeout(timeoutRef.current);
  }, []);

  const connect = useCallback(() => {
    if (wsRef.current) {
      try {
        wsRef.current.close();
      } catch {
        /* ignore */
      }
    }
    const url = getWsChatUrl();
    setStatus("connecting");
    let ws: WebSocket;
    try {
      ws = new WebSocket(url);
    } catch {
      setStatus("closed");
      toast("Could not reach cloud chat gateway", "error");
      return;
    }
    wsRef.current = ws;

    ws.onopen = () => {
      setStatus("open");
    };

    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data);
        if (data.type === "init") {
          setDeviceOnline(!!data.online);
          if (Array.isArray(data.history) && data.history.length > 0) {
            setMsgs(data.history);
            save(data.history);
          }
        } else if (data.type === "device_status") {
          setDeviceOnline(!!data.online);
        } else if (data.type === "status" && data.status === "thinking") {
          setPending(true);
        } else if (data.type === "response") {
          clearPending();
          if (data.message) {
            pushRaw(data.message);
          } else if (typeof data.content === "string") {
            push({ role: data.role || "agent", text: data.content, image: data.image });
          }
        } else if (data.type === "message" && data.message) {
          pushRaw(data.message);
        } else if (data.type === "photo") {
          clearPending();
          push({ role: "agent", text: data.caption || "", image: data.image_url || data.image });
        } else if (data.type === "cleared") {
          setMsgs([]);
          save([]);
        }
      } catch {
        clearPending();
      }
    };

    ws.onclose = () => {
      setStatus("closed");
      clearPending();
      if (reconnectTimerRef.current) clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = setTimeout(() => {
        connect();
      }, 3000);
    };

    ws.onerror = () => {
      setStatus("closed");
    };
  }, [clearPending, push, pushRaw, toast]);

  useEffect(() => {
    connect();
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
      if (reconnectTimerRef.current) clearTimeout(reconnectTimerRef.current);
    };
  }, [connect]);

  useEffect(() => {
    pendingRef.current = pending;
  }, [pending]);

  const send = () => {
    const text = draft.trim();
    if (!text) return;
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      toast("Gateway disconnected — connecting...", "warning");
      connect();
      return;
    }
    ws.send(JSON.stringify({ type: "message", content: text }));
    push({ role: "user", text });
    setDraft("");
    setPending(true);
    const myId = Date.now();
    idRef.current = myId;
    timeoutRef.current = setTimeout(() => {
      if (pendingRef.current && idRef.current === myId) {
        clearPending();
        push({
          role: "system",
          text: "No reply within 60s — the agent may be busy (tool calls) or unreachable.",
        });
      }
    }, LOAD_TIMEOUT_MS);
  };

  const clearChat = () => {
    setMsgs([]);
    save([]);
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify({ type: "clear" }));
    }
  };

  return (
    <div className={styles.wrap}>
      <div className={styles.head}>
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <div className={`espet-logomark ${styles.logo}`}>
            <img src="/static/brand/espet_logo.jpg" alt="ESPET logo" width={40} height={40} />
          </div>
          <div>
            <div style={{ fontWeight: 600 }}>ESPET · Desktop Console</div>
            <div style={{ fontSize: 12, color: "#737373" }} className="espet-mono">
              channel: cloud_link · session: dashboard
            </div>
          </div>
          <StatusChip
            tone={
              status !== "open"
                ? "danger"
                : deviceOnline
                ? "success"
                : "warning"
            }
            pulse={status === "connecting" || (status === "open" && !deviceOnline)}
          >
            {status !== "open"
              ? status === "connecting"
                ? "Connecting…"
                : "Gateway Offline"
              : deviceOnline
              ? "ESP32 Online"
              : "ESP32 Offline (Gateway ready)"}
          </StatusChip>
        </div>
        <Space>
          {msgs.length > 0 && (
            <Button size="small" onClick={clearChat} icon={<Icon name="trash" size={14} />}>
              Clear
            </Button>
          )}
          <Tooltip title="Reconnect to cloud chat gateway">
            <Button
              size="small"
              type={status === "open" ? "default" : "primary"}
              icon={<Icon name="refresh" size={14} />}
              onClick={connect}
            >
              {status === "open" ? "Reconnect" : "Connect"}
            </Button>
          </Tooltip>
        </Space>
      </div>

      {status === "open" && !deviceOnline && (
        <Alert
          style={{ marginBottom: 8 }}
          type="warning"
          showIcon
          message="ESP32 硬件设备未连接。本地网关已就绪，请为 ESP32 通电并确保其连上 Wi-Fi。"
        />
      )}
      {status !== "open" && (
        <Alert
          style={{ marginBottom: 8 }}
          type={status === "connecting" ? "info" : "error"}
          showIcon
          message={
            status === "connecting"
              ? "正在连接到本地云网关…"
              : "与本地网关断开连接，正在尝试重新连接…"
          }
        />
      )}

      <div className={styles.list} ref={listRef}>
        {msgs.length === 0 && (
          <div className={styles.system}>
            桌面端主控台已就绪。消息将通过 Cloud Link 专属通道发送给 ESP32 Agent。
          </div>
        )}
        {msgs.map((m) =>
          m.role === "system" ? (
            <div key={m.id} className={styles.system}>
              {m.text}
            </div>
          ) : (
            <div
              key={m.id}
              className={cx(styles.row, m.role === "user" && styles.rowUser)}
            >
              <Avatar
                size={32}
                style={{
                  flex: "none",
                  background:
                    m.role === "user" ? "rgba(255,255,255,0.16)" : "linear-gradient(135deg,#f5f5f5,#a3a3a3)",
                  color: m.role === "user" ? "#f5f5f5" : "#050505",
                }}
              >
                {m.role === "user" ? <Icon name="user" size={16} /> : <Icon name="bot" size={16} />}
              </Avatar>
              <div
                className={cx(
                  styles.bubble,
                  m.role === "user" ? styles.userBubble : styles.agentBubble
                )}
              >
                {m.image && (
                  <div style={{ marginBottom: m.text ? 10 : 0 }}>
                    <Image
                      src={m.image}
                      alt="Captured photo"
                      style={{
                        maxHeight: 340,
                        maxWidth: "100%",
                        borderRadius: 12,
                        objectFit: "contain",
                        border: "1px solid rgba(255, 255, 255, 0.12)",
                        background: "rgba(0, 0, 0, 0.35)",
                      }}
                    />
                  </div>
                )}
                {m.role === "agent" ? (
                  <Markdown>{m.text}</Markdown>
                ) : (
                  <span style={{ whiteSpace: "pre-wrap" }}>{m.text}</span>
                )}
              </div>
            </div>
          )
        )}
        {pending && (
          <div className={styles.row}>
            <Avatar
              size={32}
              style={{
                flex: "none",
                background: "linear-gradient(135deg,#f5f5f5,#a3a3a3)",
                color: "#050505",
              }}
            >
              <Icon name="bot" size={16} />
            </Avatar>
            <div className={cx(styles.bubble, styles.agentBubble)}>
              <Space>
                <Spin size="small" />
                <span style={{ color: "#a3a3a3" }}>thinking…</span>
              </Space>
            </div>
          </div>
        )}
      </div>

      <div className={styles.composer}>
        <Input.TextArea
          value={draft}
          autoSize={{ minRows: 1, maxRows: 5 }}
          placeholder={status === "open" ? (deviceOnline ? "Message ESPET…" : "ESP32 离线中，消息将在设备连上后重传…") : "连接中…"}
          disabled={status !== "open"}
          onChange={(e) => setDraft(e.target.value)}
          onPressEnter={(e) => {
            if (!e.shiftKey) {
              e.preventDefault();
              send();
            }
          }}
          style={{ background: "transparent", border: "none", boxShadow: "none", color: "#e8e8e8" }}
        />
        <Button
          type="primary"
          disabled={status !== "open" || !draft.trim()}
          icon={<Icon name="send" size={15} />}
          onClick={send}
        >
          Send
        </Button>
      </div>
    </div>
  );
}
