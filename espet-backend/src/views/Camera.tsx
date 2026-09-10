/* Camera — live view of the ESP32-S3 OV2640.

   The dashboard polls a snapshot endpoint on the device's HTTP server
   (`http://<device>:18789/camera.jpg`, same host/port as the chat WebSocket
   gateway) and repaints the frame on an interval — a low-bandwidth "live view"
   suited to the ESP32. NOTE: the firmware does not serve that route yet
   (ws_server.c registers only the `/` WebSocket); until it does, this page
   shows a clear offline state. Endpoint plan: .agent/NEXT_PLAN_BACKEND.MD §6.

   Device address is shared with Chat (localStorage `espet.deviceAddr`). */

import { Button, Input, Segmented, Space, Tag, Typography } from "antd";
import { createStyles } from "antd-style";
import { useCallback, useEffect, useRef, useState } from "react";
import { getDeviceAddr, setDeviceAddr } from "../api";
import { Icon } from "../components/Icon";
import { StatusChip } from "../components/StatusChips";
import { useApp } from "../store";
import { Card, useLayout } from "../ui";

const { Text } = Typography;

const CAMERA_PORT = 18789;
const INTERVALS: Record<string, number> = {
  "2 fps": 500,
  "1 fps": 1000,
  "1 / 2 s": 2000,
  "1 / 5 s": 5000,
};

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
  scan: css`
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
  meta: css`
    margin-top: 10px;
    font-size: 12px;
    color: #737373;
  `,
}));

export function CameraView() {
  const { styles } = useS();
  const layout = useLayout();
  const { toast } = useApp();
  const [addr, setAddr] = useState(getDeviceAddr());
  const [live, setLive] = useState(false);
  const [rate, setRate] = useState<keyof typeof INTERVALS>("1 fps");
  const [frame, setFrame] = useState<{ url: string; at: number } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const timer = useRef<number | null>(null);

  const grab = useCallback(() => {
    const url = `http://${addr}:${CAMERA_PORT}/camera.jpg?t=${Date.now()}`;
    setError(null);
    const probe = new Image();
    probe.onload = () => setFrame({ url, at: Date.now() });
    probe.onerror = () =>
      setError(
        `No response from ${addr}:${CAMERA_PORT} — is the device on this network, and does its firmware serve GET /camera.jpg?`
      );
    probe.src = url;
  }, [addr]);

  useEffect(() => {
    if (!live) {
      if (timer.current) window.clearInterval(timer.current);
      timer.current = null;
      return;
    }
    grab();
    timer.current = window.setInterval(grab, INTERVALS[rate]);
    return () => {
      if (timer.current) window.clearInterval(timer.current);
      timer.current = null;
    };
  }, [live, rate, grab]);

  const saveAddr = () => {
    setDeviceAddr(addr);
    toast("Device address saved — shared with Chat");
  };

  return (
    <div className={layout.styles.stack}>
      <Card>
        <div className={styles.head}>
          <span className={styles.title}>
            <Icon name="camera" size={16} /> ESP32-S3 camera
          </span>
          <StatusChip
            tone={live ? (error ? "danger" : "success") : "neutral"}
            pulse={live && !!error}
          >
            {live ? (error ? "Unreachable" : "Live") : "Paused"}
          </StatusChip>
        </div>
        <div className={styles.frame}>
          {frame && !error ? (
            <img className={styles.img} src={frame.url} alt="ESP32 camera view" />
          ) : (
            <div className={styles.off}>
              <Icon name="camera" size={40} />
              <div>
                {error ?? (
                  <>
                    Press <b>Go live</b> to start polling{" "}
                    <span className="espet-mono">
                      http://{addr}:{CAMERA_PORT}/camera.jpg
                    </span>
                  </>
                )}
              </div>
              {error && (
                <Text style={{ fontSize: 12, color: "#737373", maxWidth: 560 }}>
                  The camera endpoint ships with the firmware update described in{" "}
                  <span className="espet-mono">
                    .agent/NEXT_PLAN_BACKEND.MD §6
                  </span>{" "}
                  (a <span className="espet-mono">GET /camera.jpg</span> handler
                  on the device's existing HTTP server). Until then this view
                  stays offline.
                </Text>
              )}
            </div>
          )}
          {frame && !error && live && (
            <Tag
              className={styles.scan}
              bordered={false}
              style={{
                background: "rgba(143, 217, 174, 0.12)",
                color: "#8fd9ae",
                border: "1px solid rgba(143, 217, 174, 0.3)",
              }}
            >
              LIVE · {rate}
            </Tag>
          )}
        </div>
        <div className={styles.controls}>
          <Space.Compact style={{ width: 320, maxWidth: "100%" }}>
            <Input
              value={addr}
              onChange={(e) => setAddr(e.target.value)}
              placeholder="espet.local or 192.168.x.x"
              addonBefore="http://"
              className="espet-mono"
            />
            <Button onClick={saveAddr} icon={<Icon name="save" size={14} />}>
              Save
            </Button>
          </Space.Compact>
          <Space wrap>
            <Segmented
              options={Object.keys(INTERVALS)}
              value={rate}
              onChange={(v) => setRate(v as keyof typeof INTERVALS)}
            />
            <Button onClick={grab} icon={<Icon name="refresh" size={14} />}>
              Snapshot
            </Button>
            <Button
              type="primary"
              onClick={() => setLive((l) => !l)}
              icon={
                <Icon
                  name={live ? "send" : "camera"}
                  size={14}
                  className={live ? styles.stopIcon : undefined}
                />
              }
            >
              {live ? "Stop" : "Go live"}
            </Button>
          </Space>
        </div>
        {frame && (
          <div className={styles.meta}>
            Last frame: {new Date(frame.at).toLocaleTimeString()} ·{" "}
            <span className="espet-mono">{addr}</span>
          </div>
        )}
      </Card>
      <div className={layout.styles.sectionNote}>
        <Icon name="info" size={16} />
        <span>
          Snapshots are fetched directly from the device over your LAN — the
          backend is not involved, and nothing leaves your network. The frame
          rate is intentionally low: the OV2640 + ESP32-S3 serve one JPEG per
          request.
        </span>
      </div>
    </div>
  );
}
