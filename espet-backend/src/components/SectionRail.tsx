/* Right-side section rail — glass bubbles joined by a vertical gradient line.
   The bubble whose section has scrolled past ~35% of the viewport lights up
   (scroll-spy); tapping one smooth-scrolls to it and deep-links the hash via
   replaceState. Bubbles are solid (the line never bleeds through) and sized
   for easy tapping; the label pill is collapsed to zero width for all bubbles
   except the active one, and slides open on hover — so the rail never crowds
   the main content. Under 820px labels drop entirely. */

import { createStyles } from "antd-style";
import { useEffect, useState } from "react";
import type { ViewMeta } from "../routes";
import { Icon } from "./Icon";

const useS = createStyles(({ css }) => ({
  rail: css`
    position: fixed;
    right: 14px;
    top: 50%;
    transform: translateY(-50%);
    z-index: 25;
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 18px;
    padding: 10px 0;
    @media (max-width: 819px) {
      right: 10px;
      gap: 12px;
    }
  `,
  line: css`
    position: absolute;
    right: 17px; /* passes through the centre of the 36 px dots */
    top: 28px;
    bottom: 28px;
    width: 2px;
    border-radius: 2px;
    background: linear-gradient(
      180deg,
      rgba(255, 255, 255, 0.4),
      rgba(255, 255, 255, 0.08)
    );
    pointer-events: none;
    @media (max-width: 819px) {
      right: 15px; /* centre of the 32 px mobile dots */
      top: 22px;
      bottom: 22px;
    }
  `,
  bubble: css`
    position: relative;
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: 10px;
    padding: 0;
    margin: 0;
    border: none;
    background: none;
    cursor: pointer;
    color: #a3a3a3;
    font-family: inherit;
    font-size: 12.5px;
    font-weight: 500;
    transition: color 0.15s ease;
    &:hover {
      color: #e5e5e5;
    }
    &:hover .label {
      max-width: 140px;
      opacity: 1;
      padding-inline: 12px;
      border-width: 1px;
    }
  `,
  label: css`
    /* collapsed by default — only the active bubble (or a hovered one) opens */
    max-width: 0;
    opacity: 0;
    overflow: hidden;
    padding: 5px 0;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.07);
    border: 0 solid rgba(255, 255, 255, 0.1);
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    white-space: nowrap;
    transition: max-width 0.2s ease, opacity 0.2s ease,
      padding 0.2s ease, border-color 0.15s ease;
    @media (max-width: 819px) {
      display: none;
    }
  `,
  dot: css`
    position: relative;
    z-index: 1;
    flex: none;
    width: 36px;
    height: 36px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    color: #a3a3a3;
    /* solid fill so the connector line stops at the bubble, not through it */
    background: #0b0b0b;
    border: 1px solid rgba(255, 255, 255, 0.18);
    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.5);
    transition: transform 0.18s cubic-bezier(0.34, 1.56, 0.64, 1),
      border-color 0.18s ease, color 0.18s ease, box-shadow 0.18s ease;
    .espet-rail-hoverable:hover & {
      transform: scale(1.1);
      border-color: rgba(255, 255, 255, 0.4);
    }
    @media (max-width: 819px) {
      width: 32px;
      height: 32px;
    }
  `,
  active: css`
    color: #ffffff;
    & .dot {
      color: #050505;
      border-color: rgba(255, 255, 255, 0.85);
      background: #f5f5f5;
      box-shadow: 0 0 0 3px rgba(255, 255, 255, 0.14),
        0 0 16px rgba(255, 255, 255, 0.35);
      transform: scale(1.08);
    }
    & .label {
      max-width: 140px;
      opacity: 1;
      padding-inline: 12px;
      border-width: 1px;
      border-color: rgba(255, 255, 255, 0.35);
    }
  `,
}));

export function SectionRail({
  page,
  items,
  initial,
}: {
  page: string;
  items: ViewMeta[];
  /** Section to scroll to on mount (deep link). */
  initial?: string;
}) {
  const { styles, cx } = useS();
  const [active, setActive] = useState(initial ?? items[0]?.id ?? "");

  /* scroll-spy: the last section whose top has passed the probe line wins.
     A ResizeObserver on the document re-measures while lazy section content
     (config-gated views) fills in and changes page height. */
  useEffect(() => {
    let raf = 0;
    const measure = () => {
      const probe = window.innerHeight * 0.35;
      let cur = items[0]?.id ?? "";
      for (const it of items) {
        const el = document.getElementById(`sec-${it.id}`);
        if (el && el.getBoundingClientRect().top <= probe) cur = it.id;
      }
      if (
        window.innerHeight + window.scrollY >=
        document.documentElement.scrollHeight - 4
      )
        cur = items[items.length - 1]?.id ?? cur;
      setActive(cur);
    };
    const on = () => {
      cancelAnimationFrame(raf);
      raf = requestAnimationFrame(measure);
    };
    measure();
    window.addEventListener("scroll", on, { passive: true });
    window.addEventListener("resize", on);
    const ro = new ResizeObserver(on);
    ro.observe(document.documentElement);
    return () => {
      window.removeEventListener("scroll", on);
      window.removeEventListener("resize", on);
      ro.disconnect();
      cancelAnimationFrame(raf);
    };
  }, [items]);

  /* deep-link / nav-target scroll (sections render after config loads) */
  useEffect(() => {
    if (!initial) return;
    const t = setTimeout(() => {
      document
        .getElementById(`sec-${initial}`)
        ?.scrollIntoView({ behavior: "smooth", block: "start" });
    }, 140);
    return () => clearTimeout(t);
  }, [initial]);

  const jump = (id: string) => {
    setActive(id);
    document
      .getElementById(`sec-${id}`)
      ?.scrollIntoView({ behavior: "smooth", block: "start" });
    history.replaceState(null, "", `#${page}/${id}`);
  };

  return (
    <nav className={styles.rail} aria-label="Sections">
      <span className={styles.line} />
      {items.map((it) => (
        <button
          key={it.id}
          type="button"
          className={cx(
            styles.bubble,
            "espet-rail-hoverable",
            active === it.id && styles.active
          )}
          onClick={() => jump(it.id)}
          title={it.label}
        >
          <span className={cx(styles.label, "label")}>{it.label}</span>
          <span className={cx(styles.dot, "dot")}>
            <Icon name={it.icon} size={15} />
          </span>
        </button>
      ))}
    </nav>
  );
}
