/* Schedule — web-managed cron jobs (source="web"). Each job sends a message to
   the agent at its trigger time; replies go through the chosen channel. Device
   limits mirrored from cron_service.h: 16 jobs, 31-char name, 255-char msg. */

import {
  Button,
  DatePicker,
  Form,
  Input,
  InputNumber,
  Modal,
  Radio,
  Select,
  Switch,
} from "antd";
import dayjs from "dayjs";
import { useState } from "react";
import { client } from "../api";
import { Icon } from "../components/Icon";
import { humanInterval, useApp } from "../store";
import { Badge, Card, useLayout } from "../ui";
import type { Job } from "../types";

const JOB_NAME_MAX = 31;
const JOB_MSG_MAX = 255;
const JOB_MAX = 16;

interface JobDraft {
  name: string;
  kind: "every" | "at";
  interval_s: number;
  at_local: dayjs.Dayjs | null;
  message: string;
  channel: "system" | "telegram";
  chat_id: string;
  enabled: boolean;
  delete_after_run: boolean;
}

const emptyDraft = (): JobDraft => ({
  name: "",
  kind: "every",
  interval_s: 86400,
  at_local: null,
  message: "",
  channel: "system",
  chat_id: "",
  enabled: true,
  delete_after_run: false,
});

const toDraft = (j: Job): JobDraft => ({
  name: j.name,
  kind: j.kind,
  interval_s: j.interval_s ?? 86400,
  at_local: j.at_epoch ? dayjs(j.at_epoch * 1000) : null,
  message: j.message,
  channel: j.channel,
  chat_id: j.channel === "telegram" ? j.chat_id : "",
  enabled: j.enabled,
  delete_after_run: j.delete_after_run ?? false,
});

const scheduleText = (j: Job) =>
  j.kind === "every"
    ? `every ${humanInterval(j.interval_s ?? 0)}`
    : `${new Date((j.at_epoch ?? 0) * 1000).toLocaleString(undefined, {
        dateStyle: "medium",
        timeStyle: "short",
      })}${j.delete_after_run ? " · once" : ""}`;

const destText = (j: Job) =>
  j.channel === "telegram" ? `Telegram · chat ${j.chat_id}` : "Device (OLED + voice)";

export function ScheduleView() {
  const { config, patchConfig, toast, confirm } = useApp();
  const { styles } = useLayout();
  const [open, setOpen] = useState(false);
  const [editing, setEditing] = useState<Job | null>(null);
  const [form] = Form.useForm<JobDraft>();
  const kind = Form.useWatch("kind", form);
  const channel = Form.useWatch("channel", form);
  if (!config) return null;

  const jobs = config.jobs;

  const openNew = () => {
    setEditing(null);
    form.setFieldsValue(emptyDraft());
    setOpen(true);
  };
  const openEdit = (j: Job) => {
    setEditing(j);
    form.setFieldsValue(toDraft(j));
    setOpen(true);
  };

  const submit = async () => {
    let v: JobDraft;
    try {
      v = await form.validateFields();
    } catch {
      return;
    }
    const payload: Partial<Job> = {
      name: v.name.trim(),
      kind: v.kind,
      message: v.message.trim(),
      channel: v.channel,
      chat_id: v.channel === "telegram" ? v.chat_id.trim() : "cron",
      enabled: v.enabled,
      delete_after_run: v.kind === "at" ? v.delete_after_run : false,
    };
    if (v.kind === "every") payload.interval_s = v.interval_s;
    else payload.at_local = v.at_local!.toISOString().slice(0, 16);

    try {
      const res = editing
        ? await client.updateJob(editing.id, payload)
        : await client.createJob(payload);
      patchConfig((c) => {
        c.jobs = res.jobs;
        c.versions.jobs = res.versions.jobs;
      });
      setOpen(false);
      toast(editing ? "Job updated" : "Job created");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  const toggle = async (j: Job, enabled: boolean) => {
    try {
      const res = await client.updateJob(j.id, { enabled });
      patchConfig((c) => {
        c.jobs = res.jobs;
        c.versions.jobs = res.versions.jobs;
      });
      toast(enabled ? "Job enabled" : "Job disabled");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  const del = async (j: Job) => {
    const ok = await confirm({
      title: `Delete “${j.name}”?`,
      message: "The job will be removed from the device at its next sync.",
      okLabel: "Delete job",
      danger: true,
    });
    if (!ok) return;
    try {
      const res = await client.deleteJob(j.id);
      patchConfig((c) => {
        c.jobs = res.jobs;
        c.versions.jobs = res.versions.jobs;
      });
      toast("Job deleted");
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  return (
    <div className={styles.stack}>
      <Card>
        <div className={styles.cardHead}>
          <div className={styles.cardTitle}>
            <Icon name="calendar" /> Scheduled jobs
          </div>
          <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
            <Badge>{jobs.length}/{JOB_MAX} slots</Badge>
            <Button
              type="primary"
              disabled={jobs.length >= JOB_MAX}
              icon={<Icon name="plus" size={15} />}
              onClick={openNew}
            >
              New job
            </Button>
          </div>
        </div>
        <p className={styles.cardSub}>
          Each job sends a message to the agent at its trigger time. ESPET
          acts on it like any other request and replies through the chosen
          channel.
        </p>
        <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
          {jobs.length ? (
            jobs.map((j) => (
              <div className={styles.rowItem} key={j.id}>
                <Switch
                  checked={j.enabled}
                  size="small"
                  onChange={(v) => toggle(j, v)}
                />
                <div className={styles.grow}>
                  <div className={styles.name}>{j.name}</div>
                  <div className={styles.meta}>
                    <Badge tone={j.enabled ? "success" : "default"}>
                      {scheduleText(j)}
                    </Badge>{" "}
                    <span style={{ marginLeft: 6 }}>{destText(j)}</span>
                  </div>
                </div>
                <div style={{ display: "flex", gap: 6 }}>
                  <Button
                    type="text"
                    size="small"
                    icon={<Icon name="pencil" size={15} />}
                    onClick={() => openEdit(j)}
                  />
                  <Button
                    type="text"
                    size="small"
                    danger
                    icon={<Icon name="trash" size={15} />}
                    onClick={() => del(j)}
                  />
                </div>
              </div>
            ))
          ) : (
            <div className={styles.empty}>
              <Icon name="calendar" size={28} />
              <div>No jobs yet. Add a morning briefing or a one-time reminder.</div>
            </div>
          )}
        </div>
      </Card>
      <div className={`${styles.sectionNote}`}>
        <Icon name="info" size={16} />
        <span>
          Jobs created here are pushed to the device as{" "}
          <span className="espet-mono">cron.json</span>. Jobs ESPET creates by
          itself (e.g. “remind me at 5pm” by voice) live only on the device and
          are preserved during sync.
        </span>
      </div>

      <Modal
        title={editing ? "Edit scheduled job" : "New scheduled job"}
        open={open}
        onOk={submit}
        onCancel={() => setOpen(false)}
        okText={editing ? "Save changes" : "Create job"}
        cancelText="Cancel"
        destroyOnClose
        width={520}
      >
        <Form form={form} layout="vertical" preserve={false} style={{ marginTop: 8 }}>
          <Form.Item
            name="name"
            label="Job name"
            rules={[{ required: true, message: "Job name is required" }]}
          >
            <Input maxLength={JOB_NAME_MAX} placeholder="Morning briefing" />
          </Form.Item>
          <Form.Item name="kind" label="Trigger">
            <Radio.Group optionType="button" buttonStyle="solid">
              <Radio.Button value="every">Recurring</Radio.Button>
              <Radio.Button value="at">One-time</Radio.Button>
            </Radio.Group>
          </Form.Item>
          {kind !== "at" && (
            <Form.Item
              name="interval_s"
              label="Repeat every"
              rules={[{ required: true, message: "Interval is required" }]}
            >
              <Select
                options={[
                  { value: 900, label: "15 minutes" },
                  { value: 1800, label: "30 minutes" },
                  { value: 3600, label: "1 hour" },
                  { value: 21600, label: "6 hours" },
                  { value: 43200, label: "12 hours" },
                  { value: 86400, label: "1 day" },
                  { value: 604800, label: "1 week" },
                ]}
              />
            </Form.Item>
          )}
          {kind === "at" && (
            <Form.Item
              name="at_local"
              label="Run at (your local time)"
              rules={[{ required: true, message: "Pick a date and time" }]}
            >
              <DatePicker showTime style={{ width: "100%" }} />
            </Form.Item>
          )}
          <Form.Item
            name="message"
            label="Message to the agent"
            rules={[{ required: true, message: "Message is required" }]}
          >
            <Input.TextArea
              rows={3}
              maxLength={JOB_MSG_MAX}
              showCount
              placeholder="Give me a summary of my day and unread emails"
            />
          </Form.Item>
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12 }}>
            <Form.Item name="channel" label="Deliver reply via">
              <Select
                options={[
                  { value: "system", label: "Device (OLED + voice)" },
                  { value: "telegram", label: "Telegram" },
                ]}
              />
            </Form.Item>
            {channel === "telegram" && (
              <Form.Item
                name="chat_id"
                label="Telegram chat ID"
                rules={[{ required: true, message: "Chat ID is required" }]}
              >
                <Input className="espet-mono" placeholder="123456789" />
              </Form.Item>
            )}
          </div>
          {kind === "at" && (
            <Form.Item name="delete_after_run" valuePropName="checked">
              <Switch />{" "}
              <span style={{ marginLeft: 8, color: "#d4d4d4" }}>
                One-shot: remove after running
              </span>
            </Form.Item>
          )}
          <Form.Item name="enabled" valuePropName="checked">
            <Switch />{" "}
            <span style={{ marginLeft: 8, color: "#d4d4d4" }}>Enabled</span>
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
