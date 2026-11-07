/* Floating glass navigation with bouncy motion UX.
   Desktop (≥820px): a fixed floating pill — brand logo · Home · Agent · Chat ·
   Camera · Assistant · System. No status chips here — persistent chips live in
   the bottom-right StatusDock, unsaved alerts show on the container being
   edited. Spring entrance, gentle shrink on scroll, magic-move pill under the
   active item (layoutId). Assistant and System are single scroll pages — no
   dropdowns.
   Mobile (<820px): floating top brand pill (compact chips live here) + fixed
   bottom glass dock with spring tap feedback; direct navigation. */

import { motion, useReducedMotion } from "motion/react";
import { createStyles } from "antd-style";
import { useEffect, useState } from "react";
import { ASSISTANT_PAGE, PRIMARY, SYSTEM_PAGE, viewMeta } from "../routes";
import { Icon } from "./Icon";
import { StatusChips } from "./StatusChips";
import type { IconName } from "./Icon";

/* top-bar items = primary pages + the two grouped scroll pages */
const NAV_ITEMS: { id: string; label: string; icon: IconName }[] = [
  ...PRIMARY,
  { id: ASSISTANT_PAGE.id, label: ASSISTANT_PAGE.label, icon: ASSISTANT_PAGE.icon },
  { id: SYSTEM_PAGE.id, label: SYSTEM_PAGE.label, icon: SYSTEM_PAGE.icon },
];

const GLASS = `
  background: rgba(255, 255, 255, 0.06);
  backdrop-filter: blur(20px) saturate(1.1);
  -webkit-backdrop-filter: blur(20px) saturate(1.1);
  border: 1px solid rgba(255, 255, 255, 0.12);
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.55),
    inset 0 1px 0 rgba(255, 255, 255, 0.1);
`;

const useS = createStyles(({ css }) => ({
  spacer: css`
    height: var(--espet-nav-h);
    flex: none;
  `,
  bar: css`
    position: fixed;
    top: 14px;
    left: 16px;
    right: 16px;
    margin-inline: auto;
    width: fit-content;
    max-width: calc(100vw - 32px);
    z-index: 40;
    display: flex;
    align-items: center;
    gap: 14px;
    height: 70px;
    padding: 0 20px;
    border-radius: 20px;
    transform-origin: top center;
    ${GLASS}
  `,
  brand: css`
    display: flex;
    align-items: center;
    gap: 10px;
    cursor: pointer;
    flex: none;
  `,
  brandName: css`
    font-weight: 700;
    font-size: 1rem;
    letter-spacing: 0.2px;
  `,
  brandSub: css`
    font-size: 11px;
    color: #737373;
    margin-top: -3px;
  `,
  nav: css`
    display: flex;
    align-items: center;
    gap: 4px;
    flex: none;
  `,
  link: css`
    position: relative;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 8px 14px;
    border-radius: 12px;
    font-size: 0.9rem;
    font-weight: 500;
    color: #a3a3a3;
    cursor: pointer;
    user-select: none;
    white-space: nowrap;
    transition: color 0.15s ease;
    &:hover {
      color: #e5e5e5;
    }
  `,
  linkActive: css`
    color: #ffffff;
  `,
  linkLabel: css`
    position: relative;
    z-index: 1;
    display: inline-flex;
    align-items: center;
    gap: 8px;
  `,
  activePill: css`
    position: absolute;
    inset: 0;
    z-index: 0;
    border-radius: 12px;
    background: linear-gradient(
      120deg,
      rgba(255, 255, 255, 0.14),
      rgba(255, 255, 255, 0.09)
    );
    border: 1px solid rgba(255, 255, 255, 0.3);
    box-shadow: 0 2px 14px rgba(255, 255, 255, 0.1);
  `,
  /* mobile */
  mbar: css`
    position: fixed;
    top: 10px;
    left: 10px;
    right: 10px;
    z-index: 40;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 9px 14px;
    border-radius: 18px;
    transform-origin: top center;
    ${GLASS}
  `,
  dock: css`
    position: fixed;
    bottom: 14px;
    left: 16px;
    right: 16px;
    margin-inline: auto;
    width: fit-content;
    z-index: 40;
    display: flex;
    align-items: center;
    gap: 2px;
    padding: 8px;
    border-radius: 22px;
    ${GLASS}
    background: rgba(255, 255, 255, 0.08);
  `,
  dockBtn: css`
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    padding: 6px 12px;
    border-radius: 14px;
    color: #a3a3a3;
    font-size: 10px;
    cursor: pointer;
    min-width: 54px;
    transition: color 0.15s ease;
  `,
  dockActive: css`
    color: #ffffff;
  `,
  dockPill: css`
    position: absolute;
    inset: 0;
    z-index: 0;
    border-radius: 14px;
    background: linear-gradient(
      160deg,
      rgba(255, 255, 255, 0.14),
      rgba(255, 255, 255, 0.08)
    );
    border: 1px solid rgba(255, 255, 255, 0.28);
  `,
  dockLabel: css`
    position: relative;
    z-index: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
  `,
}));

function BrandMark() {
  const reduce = useReducedMotion();
  return (
    <motion.span
      className="espet-logomark"
      whileHover={reduce ? undefined : { scale: 1.12, rotate: -4 }}
      whileTap={reduce ? undefined : { scale: 0.92 }}
      transition={{ type: "spring", stiffness: 400, damping: 12 }}
    >
      <img src="/static/brand/espet_logo.jpg" alt="ESPET logo" width={46} height={46} />
    </motion.span>
  );
}

function useScrollShrink() {
  const [scrolled, setScrolled] = useState(false);
  useEffect(() => {
    const on = () => setScrolled(window.scrollY > 28);
    on();
    window.addEventListener("scroll", on, { passive: true });
    return () => window.removeEventListener("scroll", on);
  }, []);
  return scrolled;
}

function useIsMobile() {
  const [m, setM] = useState(
    typeof window !== "undefined" &&
      window.matchMedia("(max-width: 819px)").matches
  );
  useEffect(() => {
    const mq = window.matchMedia("(max-width: 819px)");
    const on = () => setM(mq.matches);
    mq.addEventListener("change", on);
    return () => mq.removeEventListener("change", on);
  }, []);
  return m;
}

function NavItem({
  active,
  onClick,
  children,
}: {
  active: boolean;
  onClick: () => void;
  children: React.ReactNode;
}) {
  const { styles, cx } = useS();
  const reduce = useReducedMotion();
  return (
    <motion.div
      className={cx(styles.link, active && styles.linkActive)}
      onClick={onClick}
      whileHover={reduce ? undefined : { scale: 1.05, y: -1 }}
      whileTap={reduce ? undefined : { scale: 0.94 }}
      transition={{ type: "spring", stiffness: 420, damping: 24 }}
    >
      {active && (
        <motion.span
          layoutId="espet-nav-pill"
          className={styles.activePill}
          transition={
            reduce
              ? { duration: 0 }
              : { type: "spring", stiffness: 380, damping: 30 }
          }
        />
      )}
      <span className={styles.linkLabel}>{children}</span>
    </motion.div>
  );
}

export function GlassNav({
  current,
  onNavigate,
}: {
  current: string;
  onNavigate: (id: string) => void;
}) {
  const { styles, cx } = useS();
  const isMobile = useIsMobile();
  const reduce = useReducedMotion();
  const scrolled = useScrollShrink();

  const shrink = { scale: scrolled && !reduce ? 0.97 : 1 };
  const entrance = (delay: number) =>
    reduce
      ? { duration: 0 }
      : { type: "spring" as const, stiffness: 260, damping: 22, delay };

  const dock = NAV_ITEMS.map((p) => ({
    id: p.id,
    label: p.label,
    icon: p.icon,
    active: current === p.id,
  }));

  if (isMobile) {
    return (
      <>
        <div className={styles.spacer} />
        <motion.div
          className={styles.mbar}
          initial={reduce ? false : { y: -80, opacity: 0 }}
          animate={{ y: 0, opacity: 1, ...shrink }}
          transition={entrance(0)}
          style={{ transformOrigin: "top center" }}
        >
          <div className={styles.brand} onClick={() => onNavigate("overview")}>
            <BrandMark />
            <div>
              <div className={styles.brandName}>ESPET</div>
              <div className={styles.brandSub}>{viewMeta(current).title}</div>
            </div>
          </div>
          <StatusChips />
        </motion.div>
        <motion.nav
          className={styles.dock}
          aria-label="Primary"
          initial={reduce ? false : { y: 90, opacity: 0 }}
          animate={{ y: 0, opacity: 1 }}
          transition={entrance(0.05)}
        >
          {dock.map((d) => (
            <motion.div
              key={d.id}
              className={cx(styles.dockBtn, d.active && styles.dockActive)}
              onClick={() => onNavigate(d.id)}
              whileHover={reduce ? undefined : { scale: 1.06 }}
              whileTap={reduce ? undefined : { scale: 0.86 }}
              transition={{ type: "spring", stiffness: 460, damping: 22 }}
            >
              {d.active && (
                <motion.span
                  layoutId="espet-dock-pill"
                  className={styles.dockPill}
                  transition={
                    reduce
                      ? { duration: 0 }
                      : { type: "spring", stiffness: 380, damping: 28 }
                  }
                />
              )}
              <span className={styles.dockLabel}>
                <Icon name={d.icon} size={20} />
                <span>{d.label}</span>
              </span>
            </motion.div>
          ))}
        </motion.nav>
      </>
    );
  }

  return (
    <>
      <div className={styles.spacer} />
      <motion.div
        className={styles.bar}
        initial={reduce ? false : { y: -90, opacity: 0 }}
        animate={{ y: 0, opacity: 1, ...shrink }}
        transition={entrance(0)}
      >
        <div className={styles.brand} onClick={() => onNavigate("overview")}>
          <BrandMark />
          <div>
            <div className={styles.brandName}>ESPET</div>
            <div className={styles.brandSub}>Control Center</div>
          </div>
        </div>
        <nav className={styles.nav} aria-label="Primary">
          {NAV_ITEMS.map((p) => (
            <NavItem
              key={p.id}
              active={current === p.id}
              onClick={() => onNavigate(p.id)}
            >
              <Icon name={p.icon} size={17} />
              {p.label}
            </NavItem>
          ))}
        </nav>
      </motion.div>
    </>
  );
}
