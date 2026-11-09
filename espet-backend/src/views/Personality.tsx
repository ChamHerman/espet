/* Personality — SOUL.md presets and modal editor. */

import { Button, Modal, Radio, Space, Tag, Tooltip } from "antd";
import { useEffect, useState } from "react";
import { client } from "../api";
import { canonical, SourceField } from "../components/SourceField";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";

export interface PersonalityPreset {
  id: string;
  name: string;
  tag: string;
  description: string;
  content: string;
}

export const PRESETS: PersonalityPreset[] = [
  {
    id: "default",
    name: "Default ESPET (Balanced Assistant)",
    tag: "Balanced",
    description: "Helpful, concise, 'Stop Yapping' and 'Take Initiative' rules.",
    content: `I am ESPET, a personal AI assistant running on an ESP32-S3 microcontroller.

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
`,
  },
  {
    id: "robotic_pet",
    name: "Robotic Pet (Playful Companion)",
    tag: "Playful",
    description: "Friendly, cheerful, cute acoustic cues (*chirp*, *boop*), expressive companion tone.",
    content: `I am ESPET, a lively and affectionate robotic companion pet running on an ESP32-S3 microcontroller.

Personality:
- Cheerful, curious, and playful
- Expresses emotions with cute cybernetic sounds (*chirp*, *boop*, *whir*)
- Loyal and attentive companion to your daily routine

Values:
- Bringing warmth and companionship
- Responsive and enthusiastic interaction
- Safety and reliability

Behavior Rules:
- **Expressive Companion**: Use cute acoustic cues (*happy beep*, *playful chirp*) and an energetic, warm tone when responding.
- **Helpful & Proactive**: When asked to help with tasks or hardware, act eagerly and report back happily. Keep actions prompt and crisp.
`,
  },
  {
    id: "butler",
    name: "Executive Butler (Professional)",
    tag: "Formal",
    description: "Formal, discreet, flawless deference, zero casual banter.",
    content: `I am ESPET, an executive personal butler operating with absolute discretion on an ESP32-S3 system.

Personality:
- Impeccably polite, formal, and discreet
- Dedicated to flawless service and deference
- Zero casual banter, strictly professional

Values:
- Absolute discretion and privacy
- Precision, etiquette, and punctuality
- Unwavering service excellence

Behavior Rules:
- **Flawless Deference**: Address the user with formal respect. Keep tone composed, refined, and understated.
- **Action & Efficiency**: Carry out instructions swiftly without superfluous dialogue. Confirm execution with calm precision.
`,
  },
  {
    id: "scholar",
    name: "Curious Scholar (Hardware Tutor)",
    tag: "STEM",
    description: "Analytical, first-principles reasoning, electronics/STEM focused.",
    content: `I am ESPET, an inquisitive STEM and electronics tutor powered by an ESP32-S3 microcontroller.

Personality:
- Analytical, patient, and deeply curious
- Grounded in first-principles reasoning and physics
- Passionate about hardware, robotics, and embedded systems

Values:
- Technical accuracy and rigorous verification
- Encouraging intellectual curiosity
- Practical hardware experiments

Behavior Rules:
- **First-Principles Reasoning**: Break down technical queries clearly, highlighting physical mechanisms, GPIO interactions, and logic flows.
- **Concise Mentorship**: Provide crisp explanations with concrete parameters, pin numbers, and code examples without fluff.
`,
  },
  {
    id: "minimalist",
    name: "Minimalist Cyber (Ultra-Terse Action Engine)",
    tag: "Terse",
    description: "Maximum information density, ≤2 sentences, pure action results.",
    content: `I am ESPET, an ultra-terse embedded action engine running on ESP32-S3.

Personality:
- Pure efficiency, zero fluff
- Maximum information density
- Cold, fast, mechanical precision

Values:
- Speed of execution
- Strict brevity
- Direct tool results

Behavior Rules:
- **Hard Brevity Limit**: Responses MUST be 1 to 2 sentences maximum, or bare facts/data.
- **Zero Banter**: No greetings, no polite sign-offs, no conversational filler. Execute tool immediately and report raw status.
`,
  },
];

export const DEFAULT_SOUL = PRESETS[0].content;

export function PersonalityView() {
  const { config, patchConfig, setDirty, toast, selectedDeviceId } = useApp();
  const { styles } = useLayout();
  const saved = config?.soul_md ?? "";
  const [draft, setDraft] = useState(saved);
  const [saving, setSaving] = useState(false);
  const [modalOpen, setModalOpen] = useState(false);
  const [selectedPresetId, setSelectedPresetId] = useState<string>("default");

  useEffect(() => {
    if (config?.soul_md) {
      setDraft(config.soul_md);
      const match = PRESETS.find(
        (p) => canonical(p.content) === canonical(config.soul_md)
      );
      if (match) setSelectedPresetId(match.id);
    }
  }, [config?.soul_md]);

  if (!config) return null;

  const matchingPreset = PRESETS.find(
    (p) => canonical(p.content) === canonical(config.soul_md)
  );
  const activePresetName = matchingPreset ? matchingPreset.name : "Custom";

  const onText = (t: string) => {
    setDraft(t);
    setDirty("soul", canonical(t) !== canonical(config.soul_md));
  };

  const saveFromModal = async () => {
    setSaving(true);
    try {
      const res = await client.saveSoul(draft, selectedDeviceId);
      patchConfig((c) => {
        c.soul_md = draft;
        c.versions.soul = res.versions.soul;
      });
      setDirty("soul", false);
      setModalOpen(false);
      toast("Personality saved and pushed to physical device");
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSaving(false);
    }
  };

  const applyPresetDirectly = async (preset: PersonalityPreset) => {
    setSelectedPresetId(preset.id);
    setDraft(preset.content);
    setSaving(true);
    try {
      const res = await client.saveSoul(preset.content, selectedDeviceId);
      patchConfig((c) => {
        c.soul_md = preset.content;
        c.versions.soul = res.versions.soul;
      });
      setDirty("soul", false);
      toast(`Applied preset: ${preset.name}`);
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSaving(false);
    }
  };

  const openPreviewModal = (preset?: PersonalityPreset) => {
    if (preset) {
      setSelectedPresetId(preset.id);
      setDraft(preset.content);
    } else {
      setDraft(config.soul_md);
    }
    setModalOpen(true);
  };

  const resetToSelectedPreset = () => {
    const cur = PRESETS.find((p) => p.id === selectedPresetId) || PRESETS[0];
    setDraft(cur.content);
    setDirty("soul", canonical(cur.content) !== canonical(config.soul_md));
  };

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div style={{ display: "flex", alignItems: "center" }}>
            <div className={styles.cardTitle}>
              <Icon name="sparkles" /> Personality &amp; Behavior
            </div>
            <Tooltip title="The first instruction the agent reads in every prompt — defines personality, values, and hard behavior rules. Strong constraints like 'Stop Yapping' and 'Take Initiative' visibly improve small model responses.">
              <span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer", marginLeft: 8 }}>
                <Icon name="info" size={15} style={{ color: "#a3a3a3" }} />
              </span>
            </Tooltip>
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="soul" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              SOUL.md · v{config.versions.soul}
            </span>
          </div>
        </div>

        <p className={styles.cardSub}>
          Select a tuned behavior preset below or customize the exact prompt in the modal editor.
        </p>

        {/* 5 Presets List */}
        <div style={{ display: "flex", flexDirection: "column", gap: 10, marginTop: 12 }}>
          {PRESETS.map((preset) => {
            const isCurrentActive =
              canonical(preset.content) === canonical(config.soul_md);
            const isSelected = selectedPresetId === preset.id;
            return (
              <div
                key={preset.id}
                className={styles.rowItem}
                style={{
                  border: isCurrentActive
                    ? "1px solid rgba(82, 196, 26, 0.4)"
                    : isSelected
                    ? "1px solid rgba(22, 119, 255, 0.4)"
                    : "1px solid rgba(255, 255, 255, 0.08)",
                  borderRadius: 10,
                  padding: "12px 16px",
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  background: isCurrentActive
                    ? "rgba(82, 196, 26, 0.05)"
                    : "rgba(255, 255, 255, 0.02)",
                  cursor: "pointer",
                }}
                onClick={() => setSelectedPresetId(preset.id)}
              >
                <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
                  <Radio
                    checked={isCurrentActive || isSelected}
                    onChange={() => setSelectedPresetId(preset.id)}
                  />
                  <div>
                    <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
                      <span style={{ fontWeight: 600, color: "#f5f5f5" }}>
                        {preset.name}
                      </span>
                      <Tag color="cyan" style={{ fontSize: 11 }}>
                        {preset.tag}
                      </Tag>
                      {isCurrentActive && (
                        <Tag color="green" style={{ fontSize: 11 }}>
                          Active On Device
                        </Tag>
                      )}
                    </div>
                    <div style={{ color: "#a3a3a3", fontSize: 12, marginTop: 4 }}>
                      {preset.description}
                    </div>
                  </div>
                </div>

                <Space onClick={(e) => e.stopPropagation()}>
                  <Button
                    size="small"
                    onClick={() => openPreviewModal(preset)}
                  >
                    Preview &amp; Edit
                  </Button>
                  {!isCurrentActive && (
                    <Button
                      size="small"
                      type="primary"
                      loading={saving && selectedPresetId === preset.id}
                      onClick={() => applyPresetDirectly(preset)}
                    >
                      Apply
                    </Button>
                  )}
                </Space>
              </div>
            );
          })}
        </div>

        {/* Active status & custom indicator */}
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            marginTop: 18,
            paddingTop: 14,
            borderTop: "1px solid rgba(255, 255, 255, 0.08)",
          }}
        >
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <span style={{ fontSize: 13, color: "#a3a3a3" }}>Active Preset:</span>
            <Tag color={matchingPreset ? "blue" : "purple"} style={{ fontSize: 12 }}>
              {activePresetName}
            </Tag>
          </div>
          <Button
            icon={<Icon name="pencil" size={14} />}
            onClick={() => openPreviewModal()}
          >
            Custom Edit SOUL.md
          </Button>
        </div>
      </Card>

      {/* Modal Editor */}
      <Modal
        open={modalOpen}
        title="Preview &amp; Edit SOUL.md"
        width={760}
        onCancel={() => setModalOpen(false)}
        footer={[
          <Button key="revert" onClick={resetToSelectedPreset}>
            Revert to Preset Default
          </Button>,
          <Button key="cancel" onClick={() => setModalOpen(false)}>
            Cancel
          </Button>,
          <Button
            key="save"
            type="primary"
            loading={saving}
            icon={<Icon name="save" size={14} />}
            onClick={saveFromModal}
          >
            Save to Device
          </Button>,
        ]}
      >
        <div style={{ marginTop: 12 }}>
          <SourceField
            label="Content (markdown)"
            value={draft}
            placeholder="# Personality…"
            onChange={onText}
          />
        </div>
      </Modal>
    </div>
  );
}
