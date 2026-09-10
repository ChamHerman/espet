/* Agent — the 3D companion. Auto-style derives the look from the agent's own
   settings; the dress-up workbench overrides it (mode="custom"). Every option
   change persists to /api/ui/avatar (debounced 400 ms). ToyAgent is the
   committed three.js bundle, reached through the <ToyAvatar> wrapper. */

import { Button, Modal, Space, Tag } from "antd";
import { createStyles } from "antd-style";
import { useEffect, useMemo, useRef, useState } from "react";
import { client } from "../api";
import { Icon } from "../components/Icon";
import { ToyAvatar } from "../components/ToyAvatar";
import { useApp } from "../store";
import { Badge, Card, useLayout } from "../ui";
import type { AvatarState } from "../types";
import {
  deriveAvatar,
  effectiveAvatar,
  randomOutfit,
  TOY_ANTENNA,
  TOY_BOWTIE,
  TOY_GLASSES,
  TOY_HATS,
  TOY_HAT_COLOR,
  TOY_LABELS,
  TOY_MOODS,
  TOY_BODY,
  type AvatarOpts,
} from "./avatar";

const useS = createStyles(({ css }) => ({
  stage: css`
    position: relative;
    height: 360px;
    border-radius: 18px;
    overflow: hidden;
    background: radial-gradient(
        120% 80% at 50% 120%,
        rgba(255, 255, 255, 0.08),
        transparent 60%
      ),
      linear-gradient(180deg, rgba(255, 255, 255, 0.02), transparent);
    border: 1px solid rgba(255, 255, 255, 0.07);
  `,
  name: css`
    position: absolute;
    bottom: 14px;
    left: 16px;
    font-weight: 700;
    font-size: 1.1rem;
    z-index: 2;
  `,
  badge: css`
    position: absolute;
    top: 14px;
    right: 16px;
    z-index: 2;
  `,
  group: css`
    margin-bottom: 14px;
  `,
  glabel: css`
    font-size: 12px;
    color: #a3a3a3;
    text-transform: uppercase;
    letter-spacing: 0.4px;
    margin-bottom: 8px;
  `,
  row: css`
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  `,
  chip: css`
    padding: 6px 12px;
    border-radius: 999px;
    font-size: 13px;
    cursor: pointer;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    display: inline-flex;
    align-items: center;
    gap: 7px;
    transition: all 0.15s ease;
    &:hover {
      border-color: rgba(255, 255, 255, 0.35);
    }
  `,
  chipOn: css`
    color: #050505;
    background: #f5f5f5;
    border-color: #f5f5f5;
    font-weight: 600;
  `,
  sw: css`
    width: 26px;
    height: 26px;
    border-radius: 50%;
    display: inline-grid;
    place-items: center;
    cursor: pointer;
    border: 2px solid transparent;
    & > span {
      width: 18px;
      height: 18px;
      border-radius: 50%;
    }
  `,
  swOn: css`
    border-color: #ffffff;
  `,
  trait: css`
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 5px 11px;
    border-radius: 999px;
    font-size: 12.5px;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.09);
  `,
}));

export function AgentView() {
  const { config, patchConfig, toast } = useApp();
  const { styles, cx } = useS();
  const { styles: L } = useLayout();
  const [avatar, setAvatar] = useState<AvatarState>({ mode: "auto", opts: {} });
  const [dressOpen, setDressOpen] = useState(false);
  const [play, setPlay] = useState<{ clip: string; nonce: number }>({
    clip: "Idle",
    nonce: 0,
  });
  const saveTimer = useRef<ReturnType<typeof setTimeout>>();

  useEffect(() => {
    client
      .avatar()
      .then((a) => setAvatar({ mode: a.mode, opts: a.opts || {} }))
      .catch(() => {});
  }, []);

  const derived = useMemo(
    () => (config ? deriveAvatar(config) : null),
    [config]
  );
  const eff = useMemo(
    () => (config ? effectiveAvatar(config, avatar) : null),
    [config, avatar]
  );
  if (!config || !derived || !eff) return null;

  const persist = (next: AvatarState) => {
    clearTimeout(saveTimer.current);
    saveTimer.current = setTimeout(async () => {
      try {
        const res = await client.saveAvatar(next.mode, next.opts);
        patchConfig((c) => {
          c.versions.avatar = res.versions?.avatar ?? c.versions.avatar;
        });
      } catch (e) {
        toast((e as Error).message, "error");
      }
    }, 400);
  };

  const setOpts = (patch: Partial<AvatarOpts>) => {
    const next: AvatarState = {
      mode: "custom",
      opts: { ...avatar.opts, ...patch },
    };
    setAvatar(next);
    persist(next);
  };

  const resetAuto = () => {
    const next: AvatarState = { mode: "auto", opts: {} };
    setAvatar(next);
    persist(next);
  };

  const surprise = () => {
    setOpts(randomOutfit((eff as AvatarOpts).mood));
    setPlay({ clip: "Dance", nonce: Date.now() });
  };

  const ValueRow = ({
    label,
    key,
    values,
    current,
  }: {
    label: string;
    key: keyof AvatarOpts;
    values: string[];
    current?: string;
  }) => (
    <div className={styles.group}>
      <div className={styles.glabel}>{label}</div>
      <div className={styles.row}>
        {values.map((v) => (
          <span
            key={v}
            className={cx(styles.chip, current === v && styles.chipOn)}
            onClick={() => setOpts({ [key]: v } as Partial<AvatarOpts>)}
          >
            {TOY_LABELS[v] || v}
          </span>
        ))}
      </div>
    </div>
  );

  const ColorRow = ({
    label,
    key,
    palette,
    current,
  }: {
    label: string;
    key: keyof AvatarOpts;
    palette: string[];
    current?: string;
  }) => (
    <div className={styles.group}>
      <div className={styles.glabel}>{label}</div>
      <div className={styles.row}>
        {palette.map((c) => (
          <span
            key={c}
            title={"#" + c}
            className={cx(
              styles.sw,
              String(current || "").toUpperCase() === c && styles.swOn
            )}
            onClick={() => setOpts({ [key]: c } as Partial<AvatarOpts>)}
          >
            <span style={{ background: "#" + c }} />
          </span>
        ))}
      </div>
    </div>
  );

  return (
    <div className={L.stack}>
      <Card>
        <div className={styles.stage}>
          <ToyAvatar opts={eff as Record<string, unknown>} play={play} />
          <span className={styles.badge}>
            <Badge tone={avatar.mode === "custom" ? "default" : "success"}>
              {avatar.mode === "custom"
                ? "Custom style"
                : "Auto style · from your settings"}
            </Badge>
          </span>
          <span className={styles.name}>{config.device.name || "ESPET"}</span>
        </div>
        <div style={{ display: "flex", justifyContent: "center", marginTop: 16 }}>
          <Space wrap>
            <Button
              type="primary"
              icon={<Icon name="bot" size={15} />}
              onClick={() => setDressOpen(true)}
            >
              Customize appearance
            </Button>
            <Button icon={<Icon name="dice" size={15} />} onClick={surprise}>
              Surprise me
            </Button>
            <Button
              icon={<Icon name="refresh" size={15} />}
              onClick={resetAuto}
              disabled={avatar.mode === "auto"}
            >
              Reset to auto
            </Button>
          </Space>
        </div>
      </Card>

      <Card>
        <div className={L.cardHead}>
          <div className={L.cardTitle}>
            <Icon name="spark" /> Style traits from your settings
          </div>
        </div>
        <p className={L.cardSub}>
          The robot reads its own configuration and suits up accordingly — change
          SOUL.md, memories or schedules and it re-outfits. Drag the robot to
          spin it.
        </p>
        <div style={{ display: "flex", flexWrap: "wrap", gap: 8 }}>
          {derived.traits.map((t) => (
            <Tag className={styles.trait} key={t} bordered={false}>
              <Icon name="spark" size={13} /> {t}
            </Tag>
          ))}
          {avatar.mode === "custom" && (
            <Tag className={styles.trait} bordered={false}>
              Manual Overrides Active
            </Tag>
          )}
        </div>
      </Card>

      <Modal
        title="Dress-up workbench"
        open={dressOpen}
        onCancel={() => setDressOpen(false)}
        footer={
          <Button type="primary" onClick={() => setDressOpen(false)}>
            Done
          </Button>
        }
        width={560}
      >
        <ValueRow label="Hat" key="hat" values={TOY_HATS} current={eff.hat} />
        <ValueRow
          label="Glasses"
          key="glasses"
          values={TOY_GLASSES}
          current={eff.glasses}
        />
        <ValueRow
          label="Antenna"
          key="antenna"
          values={TOY_ANTENNA}
          current={eff.antenna}
        />
        <ValueRow
          label="Bow tie"
          key="bowtie"
          values={TOY_BOWTIE}
          current={eff.bowtie}
        />
        <ValueRow label="Mood" key="mood" values={TOY_MOODS} current={eff.mood} />
        <ColorRow
          label="Body paint"
          key="bodyColor"
          palette={TOY_BODY}
          current={eff.bodyColor}
        />
        <ColorRow
          label="Head paint"
          key="headColor"
          palette={TOY_BODY}
          current={eff.headColor}
        />
        <ColorRow
          label="Hat color"
          key="hatColor"
          palette={TOY_HAT_COLOR}
          current={eff.hatColor}
        />
      </Modal>
    </div>
  );
}
