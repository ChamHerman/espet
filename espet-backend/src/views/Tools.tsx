/* Tools — MCP tools discovered from the backend's connected servers.
   client-side search + category filter, schema viewer modal. */

import { Input, Modal, Segmented } from "antd";
import { useEffect, useState } from "react";
import { client } from "../api";
import { Icon, type IconName } from "../components/Icon";
import { useApp } from "../store";
import { Badge, Card } from "../ui";
import type { McpTool } from "../types";

type Cat = "all" | "gmail" | "calendar" | "docs" | "drive" | "system";

function toolCategory(name: string): Exclude<Cat, "all"> {
  if (/gmail|thread|draft|message|label/.test(name)) return "gmail";
  if (/event|calendar|time/.test(name)) return "calendar";
  if (/doc|document/.test(name)) return "docs";
  if (/file|drive/.test(name)) return "drive";
  return "system";
}

const CAT_ICON: Record<Exclude<Cat, "all">, IconName> = {
  gmail: "send",
  calendar: "calendar",
  docs: "book",
  drive: "database",
  system: "cpu",
};

export function ToolsView() {
  const { toast } = useApp();
  const [tools, setTools] = useState<McpTool[] | null>(null);
  const [loading, setLoading] = useState(true);
  const [q, setQ] = useState("");
  const [cat, setCat] = useState<Cat>("all");
  const [schema, setSchema] = useState<McpTool | null>(null);

  useEffect(() => {
    let alive = true;
    client
      .tools()
      .then((d) => alive && setTools(d.tools || []))
      .catch((e) => alive && toast((e as Error).message, "error"))
      .finally(() => alive && setLoading(false));
    return () => {
      alive = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const filtered = (tools || []).filter((t) => {
    const s = q.toLowerCase();
    const matchSearch =
      !s ||
      t.name.toLowerCase().includes(s) ||
      (t.description || "").toLowerCase().includes(s);
    const matchCat = cat === "all" || toolCategory(t.name) === cat;
    return matchSearch && matchCat;
  });

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 16 }}>
      <Card>
        <div
          style={{
            display: "flex",
            gap: 14,
            flexWrap: "wrap",
            alignItems: "center",
            justifyContent: "space-between",
            marginBottom: 16,
          }}
        >
          <Input.Search
            allowClear
            placeholder="Search tools…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            style={{ maxWidth: 320 }}
          />
          <Segmented
            value={cat}
            onChange={(v) => setCat(v as Cat)}
            options={[
              { label: "All", value: "all" },
              { label: "Gmail", value: "gmail" },
              { label: "Calendar", value: "calendar" },
              { label: "Docs", value: "docs" },
              { label: "Drive", value: "drive" },
              { label: "System", value: "system" },
            ]}
          />
        </div>

        {loading ? (
          <div style={{ color: "#737373", padding: 40, textAlign: "center" }}>
            Loading tools…
          </div>
        ) : filtered.length ? (
          <div
            style={{
              display: "grid",
              gridTemplateColumns: "repeat(auto-fill,minmax(280px,1fr))",
              gap: 12,
            }}
          >
            {filtered.map((t) => {
              const c = toolCategory(t.name);
              return (
                <div
                  key={t.name}
                  className="espet-glass-hover"
                  style={{
                    padding: 14,
                    borderRadius: 14,
                    border: "1px solid rgba(255,255,255,0.07)",
                    background: "rgba(255,255,255,0.03)",
                    display: "flex",
                    flexDirection: "column",
                    gap: 10,
                  }}
                >
                  <div
                    style={{
                      display: "flex",
                      justifyContent: "space-between",
                      alignItems: "center",
                      gap: 8,
                    }}
                  >
                    <span
                      className="espet-mono"
                      style={{ fontWeight: 600, color: "#e5e5e5", fontSize: 13 }}
                    >
                      {t.name}
                    </span>
                    <Badge>
                      <Icon name={CAT_ICON[c]} size={13} /> {c}
                    </Badge>
                  </div>
                  <div style={{ fontSize: 12.5, color: "#a3a3a3", minHeight: 40 }}>
                    {t.description || "No description provided."}
                  </div>
                  <div style={{ display: "flex", justifyContent: "flex-end" }}>
                    <button
                      className="espet-mini-btn"
                      onClick={() => setSchema(t)}
                    >
                      <Icon name="info" size={14} /> Schema
                    </button>
                  </div>
                </div>
              );
            })}
          </div>
        ) : (
          <div style={{ color: "#737373", padding: 40, textAlign: "center" }}>
            No tools match your search.
          </div>
        )}
      </Card>

      <Modal
        title={<span className="espet-mono">{schema?.name}</span>}
        open={!!schema}
        footer={null}
        onCancel={() => setSchema(null)}
        width={640}
      >
        <pre
          className="espet-mono"
          style={{
            margin: 0,
            padding: 14,
            borderRadius: 12,
            background: "rgba(8,8,9,0.6)",
            border: "1px solid rgba(255,255,255,0.07)",
            maxHeight: "60vh",
            overflow: "auto",
            fontSize: 12.5,
            color: "#d4d4d4",
          }}
        >
          {JSON.stringify(schema?.input_schema || {}, null, 2)}
        </pre>
      </Modal>
    </div>
  );
}
