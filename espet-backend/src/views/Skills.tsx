/* Skills — web-managed markdown playbooks synced to /spiffs/skills/. Content
   is fetched separately (GET /api/ui/skills) because it is excluded from
   /api/config to fit the device's 32 KB sync buffer. Byte-exact editing via
   SourceField: the device parses `# Title` as the literal first line and caps
   each file at 8192 bytes. */

import { Button, Input, Modal, Switch } from "antd";
import { useEffect, useState } from "react";
import { client } from "../api";
import { Icon } from "../components/Icon";
import { SourceField } from "../components/SourceField";
import { byteLen, humanBytes, useApp } from "../store";
import { Badge, Card, useLayout } from "../ui";
import type { Skill } from "../types";

const SKILL_MAX = 16;
const SKILL_BYTES = 8192;
const SKILL_NAME_MAX = 48;

interface Draft {
  id: string | null;
  name: string;
  content: string;
  enabled: boolean;
}
const blank = (): Draft => ({ id: null, name: "", content: "", enabled: true });

export function SkillsView() {
  const { config, patchConfig, toast, confirm } = useApp();
  const { styles } = useLayout();
  const [skills, setSkills] = useState<Skill[]>([]);
  const [draft, setDraft] = useState<Draft | null>(null);
  const [saving, setSaving] = useState(false);

  const reload = () =>
    client
      .skills()
      .then((r) => setSkills(r.skills || []))
      .catch((e) => toast((e as Error).message, "error"));

  useEffect(() => {
    reload();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  if (!config) return null;

  const patchVersion = (res: { skills: Skill[]; versions?: { skills: number } }) => {
    setSkills(res.skills);
    patchConfig((c) => {
      if (res.versions) c.versions.skills = res.versions.skills;
    });
  };

  const save = async () => {
    if (!draft) return;
    if (!draft.name.trim()) return toast("Skill name is required", "error");
    if (!draft.content.trim()) return toast("Skill content is required", "error");
    if (!draft.content.trimStart().startsWith("#"))
      return toast("Skill must start with a '# Title' heading", "error");
    if (byteLen(draft.content) > SKILL_BYTES)
      return toast(`Skill content must be at most ${SKILL_BYTES} bytes`, "error");
    setSaving(true);
    try {
      const res = draft.id
        ? await client.updateSkill(draft.id, {
            name: draft.name,
            content: draft.content,
            enabled: draft.enabled,
          })
        : await client.createSkill({
            name: draft.name,
            content: draft.content,
            enabled: draft.enabled,
          });
      patchVersion(res);
      setDraft(null);
      toast(draft.id ? "Skill updated" : "Skill created");
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSaving(false);
    }
  };

  const toggle = async (s: Skill, enabled: boolean) => {
    try {
      const res = await client.updateSkill(s.id, { enabled });
      setSkills(res.skills);
      toast(enabled ? "Skill enabled" : "Skill disabled");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  const del = async (s: Skill) => {
    const ok = await confirm({
      title: `Delete “${s.name}”?`,
      message: `The file ${s.filename} will be removed from the device at its next sync.`,
      okLabel: "Delete skill",
      danger: true,
    });
    if (!ok) return;
    try {
      const res = await client.deleteSkill(s.id);
      setSkills(res.skills);
      toast("Skill deleted");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="book" /> Skills
          </div>
          <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
            <Badge>
              {skills.length}/{SKILL_MAX} slots
            </Badge>
            <Button
              type="primary"
              disabled={skills.length >= SKILL_MAX}
              icon={<Icon name="plus" size={15} />}
              onClick={() => setDraft(blank())}
            >
              New skill
            </Button>
          </div>
        </div>
        <p className={styles.cardSub}>
          Each skill is a markdown playbook. The agent sees every enabled skill's
          title and summary in its prompt and can read the full file on demand.
        </p>
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          {skills.length ? (
            skills.map((s) => (
              <div className={styles.rowItem} key={s.id}>
                <Switch
                  size="small"
                  checked={s.enabled}
                  onChange={(v) => toggle(s, v)}
                />
                <div className={styles.grow}>
                  <div className={styles.name}>{s.name}</div>
                  <div className={styles.meta}>
                    <span className="espet-mono" style={{ color: "#d4d4d4" }}>
                      {s.filename}
                    </span>{" "}
                    · {humanBytes(byteLen(s.content))}
                  </div>
                </div>
                <div style={{ display: "flex", gap: 6 }}>
                  <Button
                    type="text"
                    size="small"
                    icon={<Icon name="pencil" size={15} />}
                    onClick={() =>
                      setDraft({
                        id: s.id,
                        name: s.name,
                        content: s.content,
                        enabled: s.enabled,
                      })
                    }
                  />
                  <Button
                    type="text"
                    size="small"
                    danger
                    icon={<Icon name="trash" size={15} />}
                    onClick={() => del(s)}
                  />
                </div>
              </div>
            ))
          ) : (
            <div className={styles.empty}>
              <Icon name="book" size={28} />
              <div>No skills yet. Add a playbook the agent can follow.</div>
            </div>
          )}
        </div>
      </Card>
      <div className={styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>
          Skills created here are written to{" "}
          <span className="espet-mono">/spiffs/skills/</span> on the device. A
          skill must start with a <span className="espet-mono"># Title</span>{" "}
          heading — that's what the device parses.
        </span>
      </div>

      <Modal
        title={draft?.id ? "Edit skill" : "New skill"}
        open={!!draft}
        onOk={save}
        onCancel={() => setDraft(null)}
        okText="Save skill"
        confirmLoading={saving}
        width={720}
        destroyOnClose
      >
        {draft && (
          <div style={{ marginTop: 8 }}>
            <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
              Skill name
            </div>
            <Input
              maxLength={SKILL_NAME_MAX}
              value={draft.name}
              placeholder="daily-briefing"
              onChange={(e) => setDraft({ ...draft, name: e.target.value })}
            />
            <div style={{ fontSize: 12, color: "#737373", margin: "6px 0 14px" }}>
              Lowercase letters, numbers and hyphens. Becomes{" "}
              <span className="espet-mono">skills/&lt;name&gt;.md</span> on the
              device.
            </div>
            <SourceField
              label="Content (markdown)"
              value={draft.content}
              byteLimit={SKILL_BYTES}
              rows={12}
              hint={
                <span>
                  Must start with a <b># Title</b> heading.
                </span>
              }
              onChange={(content) => setDraft({ ...draft, content })}
            />
            <div style={{ marginTop: 12 }}>
              <Switch
                checked={draft.enabled}
                onChange={(v) => setDraft({ ...draft, enabled: v })}
              />{" "}
              <span style={{ marginLeft: 8, color: "#d4d4d4" }}>Enabled</span>
            </div>
          </div>
        )}
      </Modal>
    </div>
  );
}
