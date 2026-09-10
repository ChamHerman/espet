/* Profile — USER.md. Two modes: a structured form (same parse/build contract
   as the legacy dashboard so it stays compatible with what the device and the
   agent expect) and a raw **Markdown** tab (SourceField: exact-byte edit +
   preview). Switching tabs converts in both directions. */

import { Button, Input, Select, Space, Tabs, Typography } from "antd";
import { useMemo, useState } from "react";
import { client } from "../api";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { SourceField } from "../components/SourceField";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";
import { TIMEZONES } from "./deviceZones";

const { Text } = Typography;

const LANGUAGES = [
  "Chinese / English",
  "English",
  "Chinese",
  "Bahasa Malaysia",
  "English / Bahasa Malaysia",
];

export interface UserFields {
  name: string;
  language: string;
  timezone: string;
  notes: string[];
}

export function parseUserMd(md: string): UserFields {
  const fields: UserFields = { name: "", language: "", timezone: "", notes: [] };
  for (const line of md.split("\n")) {
    const m = line.match(/^-\s*(Name|Language|Timezone):\s*(.*)$/i);
    if (m) {
      (fields as unknown as Record<string, string>)[m[1].toLowerCase()] = m[2].trim();
    } else if (
      line.trim() === "# User Profile" ||
      (line.trim() === "" && !fields.name && !fields.language && !fields.timezone)
    ) {
      continue;
    } else {
      fields.notes.push(line);
    }
  }
  while (fields.notes.length && fields.notes[0].trim() === "") fields.notes.shift();
  while (fields.notes.length && fields.notes[fields.notes.length - 1].trim() === "")
    fields.notes.pop();
  return fields;
}

export function buildUserMd(f: UserFields): string {
  const parts = [
    "# User Profile",
    "",
    `- Name: ${f.name || "(not set)"}`,
    `- Language: ${f.language}`,
    `- Timezone: ${f.timezone}`,
  ];
  const notes = f.notes.join("\n").trim();
  if (notes) parts.push("", notes);
  return parts.join("\n") + "\n";
}

const norm = (s: string) => s.trim().replace(/\s+/g, " ");

export function ProfileView() {
  const { config, patchConfig, setDirty, toast } = useApp();
  const { styles } = useLayout();
  const server = useMemo(
    () => (config ? parseUserMd(config.user_md) : null),
    [config]
  );
  const [draft, setDraft] = useState<UserFields | null>(server);
  const [tab, setTab] = useState<"fields" | "markdown">("fields");
  const [raw, setRaw] = useState(() => (config ? config.user_md : ""));
  if (!config || !server || !draft) return null;

  const serverMd = buildUserMd(server);
  const currentMd = tab === "fields" ? buildUserMd(draft) : raw;
  const dirty = norm(currentMd) !== norm(serverMd);
  const track = (next: UserFields) => {
    setDraft(next);
    setDirty("profile", norm(buildUserMd(next)) !== norm(serverMd));
  };

  const onTab = (k: string) => {
    const next = k === "markdown" ? "markdown" : "fields";
    if (next === tab) return;
    if (next === "markdown") {
      const md = buildUserMd(draft);
      setRaw(md);
      setDirty("profile", norm(md) !== norm(serverMd));
    } else {
      setDraft(parseUserMd(raw));
      setDirty("profile", norm(raw) !== norm(serverMd));
    }
    setTab(next);
  };

  const save = async () => {
    const md = tab === "fields" ? buildUserMd(draft) : raw;
    try {
      const res = await client.saveProfile(md);
      patchConfig((c) => {
        c.user_md = md;
        c.versions.user = res.versions.user;
      });
      setDraft(parseUserMd(md));
      setRaw(md);
      setDirty("profile", false);
      toast("Profile saved — device will pull it");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  const revert = () => {
    setDraft(server);
    setRaw(serverMd);
    setDirty("profile", false);
  };

  const fieldsForm = (
    <>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16, marginBottom: 14 }}>
        <div>
          <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>Name</div>
          <Input
            value={draft.name}
            placeholder="Jeff"
            onChange={(e) => track({ ...draft, name: e.target.value })}
          />
        </div>
        <div>
          <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>Language</div>
          <Select
            style={{ width: "100%" }}
            value={draft.language || undefined}
            placeholder="(not set)"
            onChange={(v) => track({ ...draft, language: v })}
            options={[
              ...LANGUAGES.map((l) => ({ value: l, label: l })),
              ...(LANGUAGES.includes(draft.language) || !draft.language
                ? []
                : [{ value: draft.language, label: draft.language }]),
            ]}
          />
        </div>
      </div>
      <div style={{ marginBottom: 14 }}>
        <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>Timezone (in profile)</div>
        <Select
          style={{ width: "100%" }}
          value={draft.timezone || undefined}
          placeholder="(not set)"
          onChange={(v) => track({ ...draft, timezone: v })}
          options={[
            ...TIMEZONES.map((t) => ({ value: t, label: t })),
            { value: config.device.timezone, label: `${config.device.timezone} (device setting)` },
          ]}
        />
        <Text style={{ fontSize: 12, color: "#737373" }}>
          Keep this the same as the Device timezone so scheduled jobs fire at
          the right time.
        </Text>
      </div>
      <div>
        <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
          Extra context for the agent
        </div>
        <Input.TextArea
          rows={5}
          value={draft.notes.join("\n")}
          placeholder={"- Occupation: student\n- Prefers brief answers in the morning"}
          onChange={(e) => track({ ...draft, notes: e.target.value.split("\n") })}
        />
        <Text style={{ fontSize: 12, color: "#737373" }}>
          Anything you want ESPET to know about you. Written into USER.md
          as-is.
        </Text>
      </div>
    </>
  );

  const markdownPane = (
    <SourceField
      label="Content (markdown)"
      value={raw}
      rows={16}
      onChange={(v) => {
        setRaw(v);
        setDirty("profile", norm(v) !== norm(serverMd));
      }}
      placeholder="# User Profile"
      hint="USER.md exactly as the device reads it — switching back to Fields parses these lines."
    />
  );

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="user" /> User profile
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="profile" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              USER.md · v{config.versions.user}
            </span>
          </div>
        </div>
        <Tabs
          size="small"
          activeKey={tab}
          onChange={onTab}
          items={[
            { key: "fields", label: "Fields", children: fieldsForm },
            { key: "markdown", label: "Markdown", children: markdownPane },
          ]}
        />
        <div style={{ display: "flex", justifyContent: "flex-end", marginTop: 16 }}>
          <Space>
            <Button onClick={revert} disabled={!dirty}>
              Revert
            </Button>
            <Button
              type="primary"
              disabled={!dirty}
              icon={<Icon name="save" size={15} />}
              onClick={save}
            >
              Save profile
            </Button>
          </Space>
        </div>
      </Card>
      <div className={styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>Saved content is pulled by the device at its next sync.</span>
      </div>
    </div>
  );
}
