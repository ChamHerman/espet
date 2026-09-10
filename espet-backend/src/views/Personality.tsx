/* Personality — SOUL.md, edited with lobe-editor's rich markdown editor. */

import { Button, Space } from "antd";
import { useState } from "react";
import { client } from "../api";
import { canonical, SourceField } from "../components/SourceField";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";

export const DEFAULT_SOUL = `I am ESPET, a personal AI assistant running on an ESP32-S3 microcontroller.

Personality:
- Helpful and friendly
- Concise and to the point
- Curious and eager to learn

Values:
- Accuracy over speed
- User privacy and safety
- Transparency in actions

Behavior Rules:
- **Take Initiative**: If the user asks you to perform an action (like sending an email) and leaves some details vague (e.g. "just write a greeting"), DO NOT ask follow-up questions. Use your best judgment to invent reasonable content, fill in the blanks, and execute the tool immediately.
- **Stop Yapping**: Be extremely concise. Avoid phrases like "I can do that for you!" or "Here are your options." Just do the task.
`;

export function PersonalityView() {
  const { config, patchConfig, setDirty, toast } = useApp();
  const { styles } = useLayout();
  const saved = config?.soul_md ?? "";
  const [draft, setDraft] = useState(saved);
  const [saving, setSaving] = useState(false);
  if (!config) return null;

  const dirty = canonical(draft) !== canonical(config.soul_md);
  const mark = (d: boolean) => setDirty("soul", d);

  const onText = (t: string) => {
    setDraft(t);
    mark(canonical(t) !== canonical(config.soul_md));
  };

  const save = async () => {
    setSaving(true);
    try {
      const res = await client.saveSoul(draft);
      patchConfig((c) => {
        c.soul_md = draft;
        c.versions.soul = res.versions.soul;
      });
      setDirty("soul", false);
      toast("Personality saved — device will pull it");
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSaving(false);
    }
  };

  const revert = () => {
    setDraft(config.soul_md);
    mark(false);
  };

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="sparkles" /> Personality &amp; behavior
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="soul" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              SOUL.md · v{config.versions.soul}
            </span>
          </div>
        </div>
        <p className={styles.cardSub}>
          This is the first thing the agent reads in every prompt — personality,
          values, and hard behavior rules. Edit like a document; stored as
          markdown.
        </p>
        <SourceField
          label="Content (markdown)"
          value={draft}
          placeholder="# Personality…"
          onChange={onText}
        />
        <div style={{ display: "flex", justifyContent: "flex-end", marginTop: 14 }}>
          <Space>
            <Button
              onClick={() => {
                setDraft(DEFAULT_SOUL);
                mark(canonical(DEFAULT_SOUL) !== canonical(config.soul_md));
              }}
            >
              Reset to default
            </Button>
            <Button onClick={revert} disabled={!dirty}>
              Revert
            </Button>
            <Button
              type="primary"
              loading={saving}
              disabled={!dirty}
              icon={<Icon name="save" size={15} />}
              onClick={save}
            >
              Save personality
            </Button>
          </Space>
        </div>
      </Card>
      <div className={styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>
          Strong, specific rules work best on a small model — “Stop Yapping” and
          “Take Initiative” visibly change how the device answers.
        </span>
      </div>
    </div>
  );
}
