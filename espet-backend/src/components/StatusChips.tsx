/* Status indicators — server health, config version, per-container unsaved
   chips. Semantic tones (DESIGN.MD §2): success = dimmed mint, failure =
   dimmed red, warning = dimmed yellow, general = glass black + white.
   Layout (DESIGN.MD §6): the two persistent chips (config version + backend)
   live in a fixed **bottom-right dock** on desktop; "Unsaved Changes" shows on
   the container being edited, never in the nav. Mobile keeps the compact chip
   row in the top brand pill (no room at the bottom — the dock lives there). */

import { createStyles } from "antd-style";
import { useApp } from "../store";
import type { Section } from "../store";

const useS = createStyles(({ css }) => ({
  wrap: css`
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  `,
  dock: css`
    position: fixed;
    right: 14px;
    bottom: 14px;
    z-index: 35;
    display: flex;
    align-items: center;
    gap: 8px;
    @media (max-width: 819px) {
      display: none;
    }
  `,
  chip: css`
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 7px 15px;
    border-radius: 999px;
    font-size: 13px;
    font-weight: 500;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    backdrop-filter: blur(14px) saturate(1.1);
    -webkit-backdrop-filter: blur(14px) saturate(1.1);
    box-shadow: 0 6px 24px rgba(0, 0, 0, 0.45);
    white-space: nowrap;
    transition: border-color 0.2s ease, color 0.2s ease;
  `,
  neutral: css`
    color: #d4d4d4;
  `,
  success: css`
    color: #8fd9ae;
    background: rgba(143, 217, 174, 0.1);
    border-color: rgba(143, 217, 174, 0.28);
  `,
  warning: css`
    color: #e6cf94;
    background: rgba(230, 207, 148, 0.1);
    border-color: rgba(230, 207, 148, 0.28);
  `,
  danger: css`
    color: #eda7a7;
    background: rgba(237, 167, 167, 0.1);
    border-color: rgba(237, 167, 167, 0.28);
  `,
  dot: css`
    display: inline-block;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    flex: none;
  `,
  pulse: css`
    @keyframes espet-chip-pulse {
      0%,
      100% {
        opacity: 1;
      }
      50% {
        opacity: 0.3;
      }
    }
    animation: espet-chip-pulse 1.6s ease-in-out infinite;
  `,
}));

export type StatusTone = "neutral" | "success" | "warning" | "danger";

const DOT_COLORS: Record<StatusTone, string> = {
  neutral: "#a3a3a3",
  success: "#8fd9ae",
  warning: "#e6cf94",
  danger: "#eda7a7",
};

/** Glass status pill with proper padding + a semantic dot. Title-case text. */
export function StatusChip({
  tone = "neutral",
  dot = true,
  dotColor,
  pulse,
  title,
  className,
  children,
}: {
  tone?: StatusTone;
  dot?: boolean;
  /** override the tone's dot color (e.g. mint dot on a neutral glass chip) */
  dotColor?: string;
  /** pulsing dot (attention needed — e.g. unsaved edits) */
  pulse?: boolean;
  title?: string;
  className?: string;
  children: React.ReactNode;
}) {
  const { styles, cx } = useS();
  return (
    <span
      title={title}
      className={cx(styles.chip, styles[tone], className)}
    >
      {dot && (
        <span
          className={cx(styles.dot, pulse && styles.pulse)}
          style={{ background: dotColor ?? DOT_COLORS[tone] }}
        />
      )}
      {children}
    </span>
  );
}

/** Per-container unsaved alert — lives on the card being edited (§6). */
export function SectionDirtyChip({ section }: { section: Section }) {
  const { dirty } = useApp();
  if (!dirty[section]) return null;
  return (
    <StatusChip tone="warning" pulse title="Edits not saved to the device yet">
      Unsaved Changes
    </StatusChip>
  );
}

/** Config version — neutral glass chip, mint sync dot. Shared by dock + mobile. */
function ConfigChip({ maxVersion }: { maxVersion: number | null }) {
  return (
    <StatusChip
      tone="neutral"
      dotColor={DOT_COLORS.success}
      title="Highest config version the device will pull next sync"
    >
      Config v{maxVersion ?? "–"}
    </StatusChip>
  );
}

/** Persistent system status — fixed bottom-right on desktop. */
export function StatusDock() {
  const { serverOnline, maxVersion } = useApp();
  return (
    <div className={useS().styles.dock} aria-label="System status">
      <ConfigChip maxVersion={maxVersion} />
      <StatusChip tone={serverOnline ? "success" : "danger"}>
        {serverOnline ? "Backend Online" : "Backend Offline"}
      </StatusChip>
    </div>
  );
}

/** Compact row for the mobile top bar. */
export function StatusChips() {
  const { serverOnline, maxVersion } = useApp();
  return (
    <div className={useS().styles.wrap}>
      <ConfigChip maxVersion={maxVersion} />
      <StatusChip tone={serverOnline ? "success" : "danger"}>
        {serverOnline ? "Backend Online" : "Backend Offline"}
      </StatusChip>
    </div>
  );
}
