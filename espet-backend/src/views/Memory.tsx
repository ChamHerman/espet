/* Memory — Dual Tiered Memory Architecture:
   1. On-Device Memory (MEMORY.md): Concise identity and personality summary (<2KB)
      synchronized to ESP32-S3 SPIFFS flash.
   2. Long-Term Vector Memory (SQLite-Vec RAG): Unbounded semantic memory store
      with fastembed (384-dim) and sqlite-vec cosine distance search.
   Warns users prominently before modifying long-term memory to prevent factual disruption. */

import {
  Alert,
  Button,
  Input,
  Modal,
  Select,
  Space,
  Tag,
  Tooltip,
} from "antd";
import { useEffect, useState } from "react";
import { client } from "../api";
import { canonical, SourceField } from "../components/SourceField";
import { Icon } from "../components/Icon";
import { SectionDirtyChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Badge, Card, useLayout } from "../ui";
import type { MemoryItem, MemoryQueryResult } from "../types";

export const EMPTY_MEMORY = `# Working Memory\n\n(empty - ESPET writes active task context and working notes here)\n`;

const CATEGORIES = [
  { value: "all", label: "All Categories" },
  { value: "fact", label: "Fact", color: "green" },
  { value: "preference", label: "Preference", color: "blue" },
  { value: "daily_note", label: "Daily Note", color: "gold" },
  { value: "action", label: "Action", color: "purple" },
  { value: "event", label: "Event", color: "cyan" },
  { value: "note", label: "Note", color: "default" },
];

function getCategoryColor(cat: string): string {
  const match = CATEGORIES.find((c) => c.value === cat);
  return match?.color || "default";
}

interface ItemDraft {
  id?: number;
  content: string;
  category: string;
}

export function MemoryView() {
  const { config, patchConfig, setDirty, toast, confirm, selectedDeviceId, dirty: storeDirty } = useApp();
  const { styles } = useLayout();

  // Tier 1: On-Device MEMORY.md state
  const server = config?.memory_md ?? "";
  const [draft, setDraft] = useState(server);
  const [note, setNote] = useState("");
  const [memoryModalOpen, setMemoryModalOpen] = useState(false);
  const [organizing, setOrganizing] = useState(false);

  useEffect(() => {
    if (config?.memory_md && !storeDirty?.memory && !memoryModalOpen) {
      setDraft(config.memory_md);
    }
  }, [config?.memory_md, storeDirty?.memory, memoryModalOpen]);
  // Tier 2: Long-Term SQLite-Vec Memory state
  const [memories, setMemories] = useState<MemoryItem[]>([]);
  const [totalCount, setTotalCount] = useState(0);
  const [loading, setLoading] = useState(false);
  const [activeCategory, setActiveCategory] = useState("all");

  // Semantic Vector Search tester state
  const [testQuery, setTestQuery] = useState("");
  const [searchResults, setSearchResults] = useState<MemoryQueryResult[] | null>(null);
  const [searchLatency, setSearchLatency] = useState<number | null>(null);
  const [searching, setSearching] = useState(false);

  // Edit/Create Modal state
  const [modalOpen, setModalOpen] = useState(false);
  const [itemDraft, setItemDraft] = useState<ItemDraft>({ content: "", category: "fact" });
  const [savingItem, setSavingItem] = useState(false);

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
      title: "Clear on-device working memory?",
      message:
        "MEMORY.md on device flash will be replaced with an empty working scratchpad template at next sync. Long-term vector memory in SQLite will be preserved.",
      okLabel: "Clear MEMORY.md",
      danger: true,
    });
    if (ok) apply(EMPTY_MEMORY);
  };

  const autoOrganize = async () => {
    setOrganizing(true);
    try {
      const res = await client.autoOrganizeMemory(selectedDeviceId);
      toast(`Auto-organize complete: cleaned ${res.cleaned_actions} noisy action records.`);
      reloadMemories();
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setOrganizing(false);
    }
  };
  const save = async () => {
    try {
      const res = await client.saveMemory(draft, selectedDeviceId);
      patchConfig((c) => {
        c.memory_md = draft;
        c.versions.memory = res.versions.memory;
      });
      setDirty("memory", false);
      toast("MEMORY.md saved and pushed to physical device");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  // --- Long-Term Memory API Handlers ---

  const reloadMemories = async () => {
    setLoading(true);
    try {
      const res = await client.listMemories({
        category: activeCategory === "all" ? undefined : activeCategory,
        device_id: selectedDeviceId,
      });
      setMemories(res.items || []);
      setTotalCount(res.total || 0);
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    reloadMemories();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeCategory, selectedDeviceId, config?.versions?.memory]);

  const handleTestSearch = async () => {
    const q = testQuery.trim();
    if (!q) {
      setSearchResults(null);
      setSearchLatency(null);
      return;
    }
    setSearching(true);
    try {
      const res = await client.queryMemory(q, 5, 0.28, selectedDeviceId);
      setSearchResults(res.results || []);
      setSearchLatency(res.latency_ms);
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSearching(false);
    }
  };

  const clearSearch = () => {
    setTestQuery("");
    setSearchResults(null);
    setSearchLatency(null);
  };

  const openCreateModal = () => {
    setItemDraft({ content: "", category: "fact" });
    setModalOpen(true);
  };

  const openEditModal = (item: MemoryItem) => {
    setItemDraft({
      id: item.id,
      content: item.content,
      category: item.source_type,
    });
    setModalOpen(true);
  };

  const saveMemoryItem = async () => {
    const content = itemDraft.content.trim();
    if (!content) return toast("Memory content is required", "error");

    setSavingItem(true);
    try {
      if (itemDraft.id) {
        await client.updateMemoryItem(itemDraft.id, {
          content,
          category: itemDraft.category,
        });
        toast("Memory updated and vector re-indexed");
      } else {
        await client.createMemoryItem({
          content,
          category: itemDraft.category,
          device_id: selectedDeviceId,
        });
      }
      setModalOpen(false);
      reloadMemories();
      if (searchResults) handleTestSearch();
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSavingItem(false);
    }
  };

  const deleteItem = async (item: MemoryItem) => {
    const ok = await confirm({
      title: "Delete long-term memory record?",
      message: `Permanently delete record #${item.id} ("${item.content.slice(0, 60)}...")? The vector embedding in SQLite-Vec will be removed immediately.`,
      okLabel: "Delete permanently",
      danger: true,
    });
    if (!ok) return;

    try {
      await client.deleteMemoryItem(item.id);
      toast("Memory record deleted");
      reloadMemories();
      if (searchResults) handleTestSearch();
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  return (
    <div className={styles.stack}>
      {/* ── Tier 1: On-Device Working Memory (MEMORY.md) ── */}
      <Card>
        <div className={styles.cardHead}>
          <div style={{ display: "flex", alignItems: "center" }}>
            <div className={styles.cardTitle}>
              <Icon name="sparkles" /> Working Memory (Device Scratchpad)
            </div>
            <Tooltip title="On-device working scratchpad (<2KB) for physical ESP32 flash sync. The agent uses this for active task context across reboots. User profile belongs in USER.md; long-term semantic memories are in SQLite-Vec RAG below.">
              <span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer", marginLeft: 8 }}>
                <Icon name="info" size={15} style={{ color: "#a3a3a3" }} />
              </span>
            </Tooltip>
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <SectionDirtyChip section="memory" />
            <span className="espet-mono" style={{ color: "#737373", fontSize: 12 }}>
              MEMORY.md · v{config.versions.memory}
            </span>
          </div>
        </div>

        {/* Bounded Scrollable Read-Only Preview */}
        <div style={{ position: "relative", marginTop: 8 }}>
          <div
            style={{
              maxHeight: 220,
              overflowY: "auto",
              background: "#141414",
              border: "1px solid #262626",
              borderRadius: 8,
              padding: 12,
              fontFamily: "monospace",
              fontSize: 13,
              color: "#d4d4d4",
              whiteSpace: "pre-wrap",
              wordBreak: "break-word",
              lineHeight: 1.5,
            }}
          >
            {draft.trim() ? draft : <span style={{ color: "#525252" }}>(empty working memory)</span>}
          </div>
          <div
            style={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              marginTop: 8,
            }}
          >
            <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
              <Tag color="default">{draft.split("\n").length} lines</Tag>
              <Tag color="default">{new TextEncoder().encode(draft).length} bytes</Tag>
            </div>
            <Button
              icon={<Icon name="edit" size={14} />}
              onClick={() => setMemoryModalOpen(true)}
            >
              Edit Working Memory
            </Button>
          </div>
        </div>

        <div style={{ marginTop: 12 }}>
          <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
            Quick note
          </div>
          <div style={{ display: "flex", gap: 10 }}>
            <Input
              value={note}
              placeholder="e.g. Active task: investigating sensor reading jitter"
              onChange={(e) => setNote(e.target.value)}
              onPressEnter={appendNote}
            />
            <Button onClick={appendNote} disabled={!note.trim()}>
              Append
            </Button>
          </div>
          <div style={{ fontSize: 12, color: "#737373", marginTop: 4 }}>
            Adds a dated bullet at the end of MEMORY.md.
          </div>
        </div>

        {dirty && (
          <div
            style={{
              display: "flex",
              justifyContent: "flex-end",
              gap: 10,
              marginTop: 14,
            }}
          >
            <Button onClick={() => apply(config.memory_md)}>Revert</Button>
            <Button
              type="primary"
              icon={<Icon name="save" size={15} />}
              onClick={save}
            >
              Save MEMORY.md
            </Button>
          </div>
        )}
      </Card>

      {/* Working Memory Editor Modal */}
      <Modal
        open={memoryModalOpen}
        title="Edit Working Memory (MEMORY.md)"
        width={760}
        onCancel={() => setMemoryModalOpen(false)}
        footer={[
          <Button key="clear" danger onClick={clear} icon={<Icon name="trash" size={14} />}>
            Clear
          </Button>,
          <Button key="revert" onClick={() => apply(config.memory_md)} disabled={!dirty}>
            Revert
          </Button>,
          <Button
            key="save"
            type="primary"
            disabled={!dirty}
            icon={<Icon name="save" size={14} />}
            onClick={async () => {
              await save();
              setMemoryModalOpen(false);
            }}
          >
            Save to Device
          </Button>,
        ]}
      >
        <div style={{ paddingTop: 8 }}>
          <SourceField
            label="Content (markdown)"
            value={draft}
            placeholder="# Working Memory"
            onChange={apply}
          />
        </div>
      </Modal>
      {/* ── Tier 2: Long-Term Vector Memory (SQLite-Vec RAG) ── */}
      <Card>
        <div className={styles.cardHead}>
          <div style={{ display: "flex", alignItems: "center" }}>
            <div className={styles.cardTitle}>
              <Icon name="database" /> Long-Term Memory (SQLite-Vec RAG)
            </div>
            <Tooltip title="Permanent vector memory database powered by BAAI/bge-small-en-v1.5 and sqlite-vec. ESPET automatically writes here via autonomous tool interactions over time. Search tests vector similarity (<0.28 cosine distance) scoped to this device.">
              <span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer", marginLeft: 8 }}>
                <Icon name="info" size={15} style={{ color: "#a3a3a3" }} />
              </span>
            </Tooltip>
          </div>
          <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
            <Badge>{totalCount} records</Badge>
            <Button
              icon={<Icon name="sparkles" size={15} />}
              loading={organizing}
              onClick={autoOrganize}
            >
              AI Auto-Organize
            </Button>
            <Button
              type="primary"
              icon={<Icon name="plus" size={15} />}
              onClick={openCreateModal}
            >
              Add memory
            </Button>
          </div>
        </div>
        <p className={styles.cardSub}>
          Permanent vector memory database powered by BAAI/bge-small-en-v1.5 and sqlite-vec.
          The agent automatically writes here via MCP tools and retrieves relevant facts before every LLM response.
        </p>

        {/* Semantic Vector Tester */}
        <div
          style={{
            background: "rgba(255, 255, 255, 0.03)",
            border: "1px solid rgba(255, 255, 255, 0.08)",
            borderRadius: 12,
            padding: 14,
            marginBottom: 16,
          }}
        >
          <div style={{ display: "flex", alignItems: "center", gap: 6, marginBottom: 6 }}>
            <span style={{ fontSize: 13, fontWeight: 600, color: "#d4d4d4" }}>
              Semantic Vector Search Tester (NFR-5 Latency Compliance)
            </span>
            <Tooltip title="Tests semantic vector search scoped strictly to the selected device, returning nearest cosine distance matches in milliseconds.">
              <span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer" }}>
                <Icon name="info" size={14} style={{ color: "#a3a3a3" }} />
              </span>
            </Tooltip>
          </div>
          <div style={{ display: "flex", gap: 10 }}>
            <Input
              value={testQuery}
              placeholder="Type query to test vector similarity (e.g. 'Dr Wong consultation', 'coffee preferences')..."
              onChange={(e) => setTestQuery(e.target.value)}
              onPressEnter={handleTestSearch}
            />
            <Button
              onClick={handleTestSearch}
              loading={searching}
              icon={<Icon name="search" size={15} />}
            >
              Search
            </Button>
            {searchResults && (
              <Button onClick={clearSearch}>
                Clear
              </Button>
            )}
          </div>

          {searchLatency !== null && searchResults && (
            <div style={{ marginTop: 10 }}>
              <div style={{ display: "flex", gap: 8, alignItems: "center", marginBottom: 8 }}>
                <Tag color={searchLatency < 10 ? "green" : "red"}>
                  {searchLatency} ms retrieval
                </Tag>
                <span style={{ fontSize: 12, color: "#a3a3a3" }}>
                  {searchResults.length} match(es) (threshold &lt; 0.28 cosine distance)
                </span>
              </div>
              <div style={{ display: "flex", flexDirection: "column", gap: 6 }}>
                {searchResults.map((r) => (
                  <div
                    key={r.id}
                    style={{
                      background: "rgba(255, 255, 255, 0.04)",
                      padding: "8px 12px",
                      borderRadius: 8,
                      fontSize: 13,
                      display: "flex",
                      justifyContent: "space-between",
                      alignItems: "center",
                      gap: 10,
                    }}
                  >
                    <div>
                      <Tag color={getCategoryColor(r.source_type)} style={{ marginRight: 6 }}>
                        {r.source_type}
                      </Tag>
                      <span style={{ color: "#f5f5f5" }}>{r.content}</span>
                    </div>
                    <div className="espet-mono" style={{ fontSize: 11, color: "#737373", whiteSpace: "nowrap" }}>
                      sim: {Math.round(r.similarity * 100)}% (dist: {r.distance})
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Category Filter Tabs */}
        <div style={{ marginBottom: 14, display: "flex", gap: 6, flexWrap: "wrap" }}>
          {CATEGORIES.map((c) => (
            <Button
              key={c.value}
              size="small"
              type={activeCategory === c.value ? "primary" : "default"}
              onClick={() => setActiveCategory(c.value)}
            >
              {c.label}
            </Button>
          ))}
        </div>

        {/* Memory Item List */}
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          {loading ? (
            <div className={styles.empty}>Loading memory records...</div>
          ) : memories.length > 0 ? (
            memories.map((m) => (
              <div className={styles.rowItem} key={m.id}>
                <Tag color={getCategoryColor(m.source_type)} style={{ margin: 0 }}>
                  {m.source_type}
                </Tag>
                <div className={styles.grow}>
                  <div className={styles.name} style={{ fontWeight: 400 }}>
                    {m.content}
                  </div>
                  <div className={styles.meta}>
                    <span className="espet-mono" style={{ color: "#d4d4d4" }}>
                      #{m.id} · {m.source_ref || "web_ui"}
                    </span>{" "}
                    · {m.created_at ? new Date(m.created_at).toLocaleString() : ""}
                  </div>
                </div>
                <div style={{ display: "flex", gap: 6 }}>
                  <Button
                    type="text"
                    size="small"
                    icon={<Icon name="pencil" size={15} />}
                    onClick={() => openEditModal(m)}
                  />
                  <Button
                    type="text"
                    danger
                    size="small"
                    icon={<Icon name="trash" size={15} />}
                    onClick={() => deleteItem(m)}
                  />
                </div>
              </div>
            ))
          ) : (
            <div className={styles.empty}>
              No long-term memories stored in this category yet.
              <br />
              ESPET records memories autonomously as conversations happen, or you can add one with the button above.
            </div>
          )}
        </div>
      </Card>

      {/* ── Edit / Create Memory Modal ── */}
      <Modal
        title={itemDraft.id ? `Edit Memory Record #${itemDraft.id}` : "Add Long-Term Memory Record"}
        open={modalOpen}
        onCancel={() => setModalOpen(false)}
        footer={[
          <Button key="cancel" onClick={() => setModalOpen(false)}>
            Cancel
          </Button>,
          <Button
            key="submit"
            type="primary"
            loading={savingItem}
            onClick={saveMemoryItem}
          >
            {itemDraft.id ? "Update Memory" : "Save Memory"}
          </Button>,
        ]}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 14, paddingTop: 10 }}>
          {itemDraft.id && (
            <Alert
              type="warning"
              showIcon
              message="Neural Grounding Warning"
              description="Modifying this record immediately updates its 384-dimensional vector embedding in SQLite-Vec. Future agent prompts will retrieve and ground reasoning in this revised statement."
            />
          )}

          <div>
            <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>Category</div>
            <Select
              style={{ width: "100%" }}
              value={itemDraft.category}
              onChange={(val) => setItemDraft((d) => ({ ...d, category: val }))}
              options={[
                { value: "fact", label: "Fact (objective facts, deadlines, locations)" },
                { value: "preference", label: "Preference (habits, communication style, coffee)" },
                { value: "daily_note", label: "Daily Note (episodic notes archived from device)" },
                { value: "action", label: "Action (tool executions, sent emails, created events)" },
                { value: "event", label: "Event (scheduled routines, milestone dates)" },
                { value: "note", label: "Note (general timestamped observation)" },
              ]}
            />
          </div>

          <div>
            <div style={{ fontSize: 12, color: "#a3a3a3", marginBottom: 4 }}>
              Memory Statement (natural language)
            </div>
            <Input.TextArea
              rows={4}
              value={itemDraft.content}
              placeholder="e.g. Supervisor Dr. Wong consultation is every Thursday at 2:00 PM in Block B."
              onChange={(e) => setItemDraft((d) => ({ ...d, content: e.target.value }))}
            />
          </div>
        </div>
      </Modal>
    </div>
  );
}
