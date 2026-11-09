/* Assistant — one scroll page stacking the agent-shaping sections in order:
   Personality → Profile → Memory → Schedule → Skills. The section views are
   reused unchanged (their own cards, dirty tracking and save flows all work);
   the right-side bubble rail tracks scroll position and deep-links sections. */

import { useEffect, useState } from "react";
import { Button, Select, Tag, Tooltip } from "antd";
import { client } from "../api";
import { Icon } from "../components/Icon";
import { SectionRail } from "../components/SectionRail";
import { ASSISTANT_PAGE } from "../routes";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";
import { PersonalityView } from "./Personality";
import { ProfileView } from "./Profile";
import { MemoryView } from "./Memory";
import { ScheduleView } from "./Schedule";
import { SkillsView } from "./Skills";

export function DeviceSyncBar() {
  const { styles } = useLayout();
  const {
    selectedDeviceId,
    setSelectedDeviceId,
    devices,
    refreshDevices,
    loadConfig,
    toast,
  } = useApp();
  const [syncing, setSyncing] = useState(false);

  const currentDev = devices.find((d) => d.device_id === selectedDeviceId);

  const onSyncNow = async () => {
    if (!selectedDeviceId) return;
    setSyncing(true);
    try {
      const res = await client.syncNow(selectedDeviceId);
      await loadConfig(selectedDeviceId);
      await refreshDevices();
      toast(
        res.pushed
          ? `Real-time sync frame pushed to device ${selectedDeviceId}`
          : `Device ${selectedDeviceId} is offline; update saved to backend`
      );
    } catch (e) {
      toast((e as Error).message, "error");
    } finally {
      setSyncing(false);
    }
  };

  const handleDeviceChange = async (val: string) => {
    setSelectedDeviceId(val);
    try {
      await loadConfig(val);
      toast(`Switched to target device ${val}`);
    } catch (e) {
      toast((e as Error).message, "error");
    }
  };

  return (
    <Card>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          flexWrap: "wrap",
          gap: 12,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <div className={styles.cardTitle} style={{ margin: 0 }}>
            <Icon name="cpu" size={18} /> Target ESPET Device
          </div>
          <Tooltip title="Select which physical ESPET device to synchronize with. All personality, profile, memory, and scheduled jobs in this tab directly mirror the physical flash of the selected device.">
            <span style={{ display: "inline-flex", alignItems: "center", cursor: "pointer", marginLeft: 4 }}>
              <Icon name="info" size={15} style={{ color: "#a3a3a3" }} />
            </span>
          </Tooltip>
        </div>

        <div style={{ display: "flex", alignItems: "center", gap: 12, flexWrap: "wrap" }}>
          <Select
            style={{ minWidth: 260 }}
            value={selectedDeviceId || undefined}
            onChange={handleDeviceChange}
            placeholder="Select ESPET Device"
            options={devices.map((d, idx) => ({
              value: d.device_id,
              label: (
                <div style={{ display: "flex", alignItems: "center", gap: 8, justifyContent: "space-between" }}>
                  <div style={{ display: "flex", alignItems: "center", gap: 6 }}>
                    <span
                      style={{
                        display: "inline-block",
                        width: 8,
                        height: 8,
                        borderRadius: "50%",
                        background: d.online ? "#52c41a" : "#8c8c8c",
                      }}
                    />
                    <span className="espet-mono" style={{ fontWeight: 500 }}>
                      {d.device_id}
                    </span>
                    {d.name && d.name !== d.device_id && (
                      <span style={{ color: "#a3a3a3", fontSize: 12 }}>({d.name})</span>
                    )}
                  </div>
                  {idx === 0 && <Tag color="blue" style={{ fontSize: 10, margin: 0 }}>Latest</Tag>}
                </div>
              ),
            }))}
          />

          {currentDev && (
            <Tag color={currentDev.online ? "success" : "default"} style={{ margin: 0 }}>
              {currentDev.online ? "Online" : "Offline"} · {currentDev.last_seen ? new Date(currentDev.last_seen).toLocaleTimeString() : "Never"}
            </Tag>
          )}

          <Button
            type="primary"
            icon={<Icon name="refresh" size={14} />}
            loading={syncing}
            onClick={onSyncNow}
          >
            Sync Now
          </Button>
        </div>
      </div>
    </Card>
  );
}
export function AssistantPage({ section }: { section?: string }) {
  /* clicking "Assistant" again (hash → #assistant) returns to the top */
  useEffect(() => {
    if (!section) window.scrollTo({ top: 0, behavior: "smooth" });
  }, [section]);

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 30 }}>
      <SectionRail page="assistant" items={ASSISTANT_PAGE.items} initial={section} />
      <section id="sec-device-sync" className="espet-section">
        <DeviceSyncBar />
      </section>
      <section id="sec-personality" className="espet-section">
        <PersonalityView />
      </section>
      <section id="sec-profile" className="espet-section">
        <ProfileView />
      </section>
      <section id="sec-memory" className="espet-section">
        <MemoryView />
      </section>
      <section id="sec-schedule" className="espet-section">
        <ScheduleView />
      </section>
      <section id="sec-skills" className="espet-section">
        <SkillsView />
      </section>
    </div>
  );
}
