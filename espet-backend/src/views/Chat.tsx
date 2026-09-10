/* Chat — browser ⇄ device, direct to the ESP32 WebSocket gateway (port 18789).
   The firmware routes inbound {"type":"message","content","chat_id"} into the
   agent loop and echoes the final reply back as {"type":"response",...} at
   mimi.c:143, so this is genuinely bidirectional with zero backend/firmware
   changes. Transcript persists to localStorage (a backend relay + shared
   history is the durable version — see .agent/NEXT_PLAN_BACKEND.MD). */

import { Markdown } from "@lobehub/ui";
import {
  Alert,
  Avatar,
  Button,
  Input,
  Space,
  Spin,
  Tooltip,
} from "antd";
import { createStyles } from "antd-style";
import { useCallback, useEffect, useRef, useState } from "react";
import { deviceWsUrl, getDeviceAddr } from "../api";
import { Icon } from "../components/Icon";
import { StatusChip } from "../components/StatusChips";
import { useApp } from "../store";

const CHAT_KEY = "espet.chat.v1";
const CHAT_ID = "dashboard";
const LOAD_TIMEOUT_MS = 60000;
const MAX_TURNS = 80;

type Role = "user" | "agent" | "system";
interface ChatMsg {
  id: string;
  role: Role;
  text: string;
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

export function ChatView() {
  const { toast } = useApp();
  const { styles, cx } = useS();
  const [msgs, setMsgs] = useState<ChatMsg[]>(load);
  const [status, setStatus] = useState<"idle" | "connecting" | "open" | "closed">(
    "idle"
  );
  const [pending, setPending] = useState(false);
  const [draft, setDraft] = useState("");
  const wsRef = useRef<WebSocket | null>(null);
  const timeoutRef = useRef<ReturnType<typeof setTimeout>>();
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

  const clearPending = () => {
    setPending(false);
    if (timeoutRef.current) clearTimeout(timeoutRef.current);
  };

  const connect = useCallback(() => {
    wsRef.current?.close();
    const url = deviceWsUrl();
    setStatus("connecting");
    let ws: WebSocket;
    try {
      ws = new WebSocket(url);
    } catch {
      setStatus("closed");
      toast("Could not reach device gateway", "error");
      return;
    }
    wsRef.current = ws;
    ws.onopen = () => setStatus("open");
    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data);
        if (data.type === "response" && typeof data.content === "string") {
          clearPending();
          push({ role: "agent", text: data.content });
        }
      } catch {
        clearPending();
        push({ role: "agent", text: String(ev.data ?? "") });
      }
    };
    ws.onclose = () => {
      setStatus("closed");
      clearPending();
    };
    ws.onerror = () => setStatus("closed");
  }, [push, toast]);

  useEffect(() => {
    return () => {
      wsRef.current?.close();
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    };
  }, []);

  useEffect(() => {
    pendingRef.current = pending;
  }, [pending]);

  const send = () => {
    const text = draft.trim();
    if (!text) return;
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      toast("Device offline — connect first (top right)", "error");
      return;
    }
    ws.send(JSON.stringify({ type: "message", content: text, chat_id: CHAT_ID }));
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
  };

  const addr = getDeviceAddr();

  return (
    <div className={styles.wrap}>
      <div className={styles.head}>
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <div className={`espet-logomark ${styles.logo}`}>
            <img src="/static/brand/espet_logo.jpg" alt="ESPET logo" width={40} height={40} />
          </div>
          <div>
            <div style={{ fontWeight: 600 }}>ESPET · live chat</div>
            <div style={{ fontSize: 12, color: "#737373" }} className="espet-mono">
              {addr}
            </div>
          </div>
          <StatusChip
            tone={
              status === "open"
                ? "success"
                : status === "connecting"
                ? "warning"
                : status === "closed"
                ? "danger"
                : "neutral"
            }
            pulse={status === "connecting"}
          >
            {status === "open"
              ? "Connected"
              : status === "connecting"
              ? "Connecting…"
              : status === "closed"
              ? "Offline"
              : "Not Connected"}
          </StatusChip>
        </div>
        <Space>
          {msgs.length > 0 && (
            <Button size="small" onClick={clearChat} icon={<Icon name="trash" size={14} />}>
              Clear
            </Button>
          )}
          <Tooltip title="Open (or retry) the WebSocket to the device gateway">
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

      {status !== "open" && (
        <Alert
          style={{ marginBottom: 8 }}
          type={status === "connecting" ? "info" : "warning"}
          showIcon
          message={
            status === "connecting"
              ? "Connecting to the device…"
              : `Not connected. Make sure the device is on this network and its address is set in Device → Live-chat device address (currently ${addr}).`
          }
        />
      )}

      <div className={styles.list} ref={listRef}>
        {msgs.length === 0 && (
          <div className={styles.system}>
            Messages go straight to the agent loop on the device — try “hello” or
            “what can you do?”.
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
          placeholder={status === "open" ? "Message ESPET…" : "Connect to the device first"}
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
