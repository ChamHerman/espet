/* Memory — MEMORY.md. Rich editor + a "quick note" that appends a dated bullet
   without opening the editor, and a clear action. Warns that the agent also
   writes here on-device during consolidation. */

import { Alert, Button, Input, Space } from "antd";
import { useState } from "react";
import { client } from "../api";
import { canonical, SourceField } from "../components/SourceField";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";

export const EMPTY_MEMORY = `# Long-term Memory\n\n(empty - ESPET will write memories here as it learns)\n`;

export function MemoryView() {
  const { config, patchConfig, setDirty, toast, confirm } = useApp();
  const { styles } = useLayout();
  const server = config?.memory_md ?? "";
  const [draft, setDraft] = useState(server);
  const [note, setNote] = useState("");
  if (!config) return null;

  const dirty = canonical(draft) !== canonical(config.memory_md);

  const apply = (next: string) => {
    setDraft(next);
    setDirty("memory", canonical(next) !== canonical(config.memory_md));
  };

  const appendNote = () => {
    const n = note.trim();
    if (!n) return;
    const date = new Date().toISOString().slice(0, 10);
    const next = draft.replace(/\s*$/, "") + `\n- ${n} (added ${date})\n`;
    apply(next);
    setNote("");
  };

  const clear = async () => {
    const ok = await confirm({
      title: "Clear long-term memory?",
      message:
        "MEMORY.md on the device will be replaced with an empty memory file at the next sync. The agent will start learning from scratch.",
      okLabel: "Clear memory",
      danger: true,
    });
    if (ok) apply(EMPTY_MEMORY);
  };

  const save = async () => {
    try {
      const res = await client.saveMemory(draft);
      patchConfig((c) => {
        c.memory_md = draft;
        c.versions.memory = res.versions.memory;
      });
      setDirty("memory", false);
      toast("Memory saved — device will pull it");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  return (
    <div className={styles.stack}>
      <Alert
        type="warning"
        showIcon
        message="The agent also writes to this file when it consolidates memories on the device. Your edit is applied at the device's next sync — after that, the agent may reorganize or extend it on its own."
      />
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="database" /> Long-term memory
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="memory" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              MEMORY.md · v{config.versions.memory}
            </span>
          </div>
        </div>
        <SourceField
          label="Content (markdown)"
          value={draft}
          placeholder="# Long-term Memory"
          onChange={apply}
        />
        <div style={{ marginTop: 12 }}>
          <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
            Quick note
          </div>
          <div style={{ display: "flex", gap: 10 }}>
            <Input
              value={note}
              placeholder="e.g. FYP demo is on Sept 11"
              onChange={(e) => setNote(e.target.value)}
              onPressEnter={appendNote}
            />
            <Button onClick={appendNote} disabled={!note.trim()}>
              Append
            </Button>
          </div>
          <div style={{ fontSize: 12, color: "#737373", marginTop: 4 }}>
            Adds a dated bullet at the end of MEMORY.md without opening the
            editor.
          </div>
        </div>
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            gap: 10,
            marginTop: 16,
          }}
        >
          <Button danger onClick={clear} icon={<Icon name="trash" size={15} />}>
            Clear memory
          </Button>
          <Space>
            <Button
              onClick={() => apply(config.memory_md)}
              disabled={!dirty}
            >
              Revert
            </Button>
            <Button
              type="primary"
              disabled={!dirty}
              icon={<Icon name="save" size={15} />}
              onClick={save}
            >
              Save memory
            </Button>
          </Space>
        </div>
      </Card>
    </div>
  );
}
