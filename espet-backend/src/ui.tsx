/* Shared glass design-system primitives (antd-style createStyles).
   The fixed dark canvas + orbs live in global.css; these are the translucent
   "glass" surfaces, cards, badges, chips and layout wrappers every view reuses.
   Theme colors come from the customToken { Espet } injected in theme.tsx. */

import { createStyles } from "antd-style";
import type { CSSObject, HTMLAttributes, PropsWithChildren } from "react";

export const useGlass = createStyles(({ css, cx }) => ({
  glass: css`
    background: rgba(255, 255, 255, 0.055);
    backdrop-filter: blur(14px) saturate(1.1);
    -webkit-backdrop-filter: blur(14px) saturate(1.1);
    border: 1px solid rgba(255, 255, 255, 0.09);
    border-radius: 16px;
  `,
  glassSoft: css`
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
  `,
  hoverable: css`
    transition: transform 0.18s ease, border-color 0.18s ease,
      background 0.18s ease;
    &:hover {
      transform: translateY(-2px);
      border-color: rgba(255, 255, 255, 0.28);
    }
  `,
  merge: (...a: (string | undefined)[]) => cx(...a.filter(Boolean)),
  card: css`
    padding: 20px;
    background: rgba(255, 255, 255, 0.055);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.09);
    border-radius: 16px;
  `,
}));

/** A glass card container. */
export function Card({
  className,
  hover,
  ...rest
}: PropsWithChildren<HTMLAttributes<HTMLDivElement>> & { hover?: boolean }) {
  const { styles, cx } = useGlass();
  return (
    <div
      className={cx(styles.card, hover && styles.hoverable, className)}
      {...rest}
    />
  );
}

export const useBadges = createStyles(({ css }) => ({
  base: css`
    display: inline-flex;
    align-items: center;
    gap: 5px;
    padding: 2px 10px;
    border-radius: 999px;
    font-size: 12px;
    font-weight: 500;
    line-height: 1.7;
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.05);
    white-space: nowrap;
  `,
  /* semantic tones — the sanctioned dimmed status hues (DESIGN.MD §2).
     default stays glass black + white. */
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
}));

export function Badge({
  tone = "default",
  className,
  children,
  ...rest
}: PropsWithChildren<HTMLAttributes<HTMLSpanElement>> & {
  tone?: "default" | "success" | "warning" | "danger";
}) {
  const { styles, cx } = useBadges();
  const toneCls =
    tone === "default" ? "" : (styles as Record<string, string>)[tone];
  return (
    <span className={cx(styles.base, toneCls, className)} {...rest}>
      {children}
    </span>
  );
}

/* ── page + section layout ─────────────────────────────────── */
export const useLayout = createStyles(({ css }) => ({
  stack: css`
    display: flex;
    flex-direction: column;
    gap: 18px;
  `,
  grid: css`
    display: grid;
    gap: 16px;
  `,
  cols2: css`
    grid-template-columns: repeat(2, 1fr);
    @media (max-width: 720px) {
      grid-template-columns: 1fr;
    }
  `,
  cols3: css`
    grid-template-columns: repeat(3, 1fr);
    @media (max-width: 860px) {
      grid-template-columns: 1fr;
    }
  `,
  cardHead: css`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  `,
  cardTitle: css`
    display: flex;
    align-items: center;
    gap: 9px;
    font-size: 1.02rem;
    font-weight: 600;
    color: #f5f5f5;
  `,
  cardSub: css`
    color: #a3a3a3;
    font-size: 0.86rem;
    margin: -6px 0 14px;
  `,
  sectionNote: css`
    display: flex;
    gap: 8px;
    align-items: flex-start;
    color: #a3a3a3;
    font-size: 0.85rem;
    padding: 12px 14px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
  `,
  empty: css`
    display: grid;
    place-items: center;
    gap: 10px;
    padding: 40px 20px;
    color: #737373;
    text-align: center;
  `,
  rowItem: css`
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 14px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
  `,
  grow: css`
    flex: 1 1 auto;
    min-width: 0;
  `,
  name: css`
    font-weight: 500;
    color: #e5e5e5;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  `,
  meta: css`
    font-size: 0.8rem;
    color: #737373;
    margin-top: 2px;
  `,
}));

/* KV table rows (device card, versions). */
export const useKV = createStyles(({ css }) => ({
  table: css`
    display: flex;
    flex-direction: column;
    gap: 2px;
  `,
  row: css`
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
    padding: 7px 2px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.045);
    &:last-child {
      border-bottom: none;
    }
  `,
  k: css`
    color: #a3a3a3;
    font-size: 0.86rem;
  `,
  v: css`
    color: #e5e5e5;
    font-family: var(--espet-font-mono);
    font-size: 0.85rem;
    text-align: right;
  `,
}));

export function KV({
  rows,
}: {
  rows: { k: React.ReactNode; v: React.ReactNode }[];
}) {
  const { styles } = useKV();
  return (
    <div className={styles.table}>
      {rows.map((r, i) => (
        <div className={styles.row} key={i}>
          <span className={styles.k}>{r.k}</span>
          <span className={styles.v}>{r.v}</span>
        </div>
      ))}
    </div>
  );
}

/* small typed style helper so views can compose CSSObject maps cleanly */
export function cssMap(o: Record<string, CSSObject>) {
  return o;
}
