/* Camera — backend-relayed live MJPEG view of the ESP32-S3 OV2640. */

import { Button, Segmented, Space, Switch, Tag, Typography } from "antd";
import { createStyles } from "antd-style";
import { useEffect, useState } from "react";
import { cameraSnapshotUrl, cameraStreamUrl, CameraStatus, client, OwnerFaceStatus } from "../api";
import { Icon } from "../components/Icon";
import { StatusChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";

const { Text } = Typography;
const FPS_OPTIONS = [2, 5, 8, 12];

const useS = createStyles(({ css }) => ({
  head: css`
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    margin-bottom: 14px;
  `,
  title: css`
    display: inline-flex;
    align-items: center;
    gap: 8px;
    font-weight: 600;
    font-size: 15px;
  `,
  frame: css`
    position: relative;
    border-radius: 16px;
    overflow: hidden;
    background: #000;
    border: 1px solid rgba(255, 255, 255, 0.12);
    aspect-ratio: 16 / 10;
    display: grid;
    place-items: center;
  `,
  img: css`
    width: 100%;
    height: 100%;
    object-fit: contain;
    display: block;
  `,
  off: css`
    display: grid;
    place-items: center;
    gap: 12px;
    padding: 30px;
    text-align: center;
    color: #737373;
    height: 100%;
  `,
  liveTag: css`
    position: absolute;
    top: 12px;
    left: 14px;
    z-index: 2;
  `,
  stopIcon: css`
    transform: rotate(180deg);
  `,
  controls: css`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-wrap: wrap;
    margin-top: 16px;
  `,
  telemetry: css`
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    margin-top: 12px;
  `,
  ownerPanel: css`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
    margin-top: 14px;
    padding: 12px;
    border-radius: 14px;
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.09);
  `,
  ownerAlert: css`
    color: #8fd9ae;
    border: 1px solid rgba(143, 217, 174, 0.28);
    background: rgba(143, 217, 174, 0.1);
    border-radius: 999px;
    padding: 6px 10px;
    font-size: 13px;
  `,
  meta: css`
    margin-top: 10px;
    font-size: 12px;
    color: #737373;
  `,
}));

function latestDeviceId(devices: Awaited<ReturnType<typeof client.devices>> | null) {
  return devices?.latest_device_id || devices?.devices?.[0]?.device_id || "espet_s3_01";
}

export function CameraView() {
  const { styles } = useS();
  const layout = useLayout();
  const { toast } = useApp();
  const [deviceId, setDeviceId] = useState("espet_s3_01");
  const [live, setLive] = useState(false);
  const [fps, setFps] = useState(8);
  const [mode, setMode] = useState<"raw" | "processed">("processed");
  const [status, setStatus] = useState<CameraStatus | null>(null);
  const [ownerFace, setOwnerFace] = useState<OwnerFaceStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [streamRev, setStreamRev] = useState(0);

  useEffect(() => {
    client.devices().then((res) => setDeviceId(latestDeviceId(res))).catch(() => undefined);
  }, []);

  useEffect(() => {
    client.ownerFaceStatus(deviceId).then(setOwnerFace).catch(() => undefined);
  }, [deviceId]);

  useEffect(() => {
    let cancelled = false;
    const tick = () => {
      client.cameraStatus(deviceId)
        .then((s) => {
          if (cancelled) return;
          setStatus(s);
          const stalled = live && s.has_frame && s.last_frame_age_ms > 5000;
          if (stalled) {
            setError("Camera stream stalled; reconnecting...");
            client.startCameraStream({ device_id: deviceId, fps }).catch(() => undefined);
            setStreamRev((v) => v + 1);
          }
        })
        .catch((e) => !cancelled && setError(e.message));
    };
    tick();
    const timer = window.setInterval(tick, live ? 1000 : 3000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [deviceId, fps, live]);

  const streamSrc = live ? `${cameraStreamUrl(deviceId, mode, fps)}&r=${streamRev}` : "";
  const detections = status?.detections?.length || 0;
  const stalled = live && !!status?.has_frame && (status?.last_frame_age_ms ?? 0) > 5000;
  const ownerLastSeen = status?.owner_last_seen_ts
    ? new Date(status.owner_last_seen_ts * 1000).toLocaleString()
    : "Never";

  const toggleLive = async () => {
    setError(null);
    if (live) {
      setLive(false);
      await client.stopCameraStream(deviceId).catch(() => undefined);
      return;
    }
    const res = await client.startCameraStream({ device_id: deviceId, fps });
    if (!res.pushed) {
      toast("No ESP32 camera device online", "error");
    }
    setStreamRev((v) => v + 1);
    setLive(true);
  };

  const changeMode = async (checked: boolean) => {
    const next = checked ? "processed" : "raw";
    setMode(next);
    await client.setCameraConfig({ device_id: deviceId, mode: next }).catch(() => undefined);
  };

  return (
    <div className={layout.styles.stack}>
      <Card>
        <div className={styles.head}>
          <span className={styles.title}>
            <Icon name="camera" size={16} /> ESP32-S3 Camera
          </span>
          <StatusChip tone={live && !error && !stalled ? "success" : error || stalled ? "danger" : "neutral"} pulse={live && !error && !stalled}>
            {stalled ? "Stalled" : live && !error ? "Live" : error ? "Error" : "Paused"}
          </StatusChip>
        </div>
        <div className={styles.frame}>
          {live ? (
            <img
              key={`${deviceId}-${mode}-${fps}-${streamRev}-${Number(live)}`}
              className={styles.img}
              src={streamSrc}
              alt="ESP32 camera live stream"
              onError={() => setError("Camera stream unavailable")}
              onLoad={() => setError(null)}
            />
          ) : (
            <div className={styles.off}>
              <Icon name="camera" size={40} />
              <div>
                Press <b>Go Live</b> to start the backend MJPEG camera stream.
              </div>
              <Text style={{ fontSize: 12, color: "#737373", maxWidth: 560 }}>
                The backend sends a stream-start control frame to the ESP32 only while this view is open.
              </Text>
            </div>
          )}
          {live && !error && (
            <Tag
              className={styles.liveTag}
              bordered={false}
              style={{ background: "rgba(143, 217, 174, 0.12)", color: "#8fd9ae", border: "1px solid rgba(143, 217, 174, 0.3)" }}
            >
              LIVE · {fps} FPS · {mode === "raw" ? "RAW" : "CV"}
            </Tag>
          )}
        </div>
        <div className={styles.controls}>
          <Space wrap>
            <Segmented options={FPS_OPTIONS.map((v) => `${v} FPS`)} value={`${fps} FPS`} onChange={(v) => setFps(Number(String(v).split(" ")[0]))} />
            <span style={{ display: "inline-flex", alignItems: "center", gap: 8 }}>
              <Switch checked={mode === "processed"} onChange={changeMode} />
              <span>AI Overlay</span>
            </span>
          </Space>
          <Space wrap>
            <Button href={cameraSnapshotUrl(deviceId, mode)} target="_blank" icon={<Icon name="refresh" size={14} />}>
              Snapshot
            </Button>
            <Button type="primary" onClick={toggleLive} icon={<Icon name={live ? "send" : "camera"} size={14} className={live ? styles.stopIcon : undefined} />}>
              {live ? "Stop" : "Go Live"}
            </Button>
          </Space>
        </div>
        <div className={styles.ownerPanel}>
          <Space wrap>
            <StatusChip tone={status?.owner_detected ? "success" : ownerFace?.enrolled ? "neutral" : "warning"} pulse={!!status?.owner_detected}>
              {status?.owner_detected ? "Owner Detected" : ownerFace?.enrolled ? "Owner Enrolled" : "Owner Not Set"}
            </StatusChip>
            {status?.owner_detected && <span className={styles.ownerAlert}>Owner Detected · {Math.round((status.owner_similarity || 0) * 100)}%</span>}
            <Tag bordered={false}>Last Seen {ownerLastSeen}</Tag>
            {ownerFace?.error && <Tag bordered={false} style={{ color: "#eda7a7" }}>Face AI Not Installed</Tag>}
          </Space>
          <Button size="small" onClick={() => { location.hash = "#overview"; }}>
            Owner Settings
          </Button>
        </div>
        <div className={styles.telemetry}>
          <Tag bordered={false}>Device <span className="espet-mono">{deviceId}</span></Tag>
          <Tag bordered={false}>FPS {status?.fps ?? 0}</Tag>
          <Tag bordered={false}>Inference {status?.inference_ms ?? 0} ms</Tag>
          <Tag bordered={false}>Detections {detections}</Tag>
          <Tag bordered={false}>Viewers {status?.viewer_count ?? 0}</Tag>
          <Tag bordered={false}>Frame Age {Math.round(status?.last_frame_age_ms ?? 0)} ms</Tag>
          <Tag bordered={false}>Owner Similarity {status?.owner_similarity ?? 0}</Tag>
        </div>
        {error && <div className={styles.meta}>{error}</div>}
      </Card>
      <div className={layout.styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>Video frames stay on the LAN: ESP32 → FastAPI backend → browser. Streaming stops automatically when viewers disconnect.</span>
      </div>
    </div>
  );
}
