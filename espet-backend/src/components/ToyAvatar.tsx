/* Ambient 3D avatar viewer exposed by the committed static/vendor/toy.bundle.js
   as a framework-agnostic global — a plain mount/setOpts/unmount lifecycle, so
   a single React effect owns it. Props: opts (dress-up state) + animation clip. */

import { useEffect, useRef } from "react";
import { createStyles } from "antd-style";

declare global {
  interface Window {
    ToyAgent?: {
      mount: (el: HTMLElement, opts?: Record<string, unknown>) => Promise<void>;
      setOpts: (opts: Record<string, unknown>) => void;
      play: (name: string, once?: boolean) => void;
      unmount: () => void;
    };
  }
}

const useS = createStyles(({ css }) => ({
  stage: css`
    position: relative;
    width: 100%;
    height: 100%;
    min-height: 260px;
    display: grid;
    place-items: center;
    overflow: hidden;
  `,
  fallback: css`
    font-size: 5rem;
    opacity: 0.85;
  `,
  canvasHost: css`
    position: absolute;
    inset: 0;
    cursor: grab;
    &:active {
      cursor: grabbing;
    }
  `,
}));

export function ToyAvatar({
  opts,
  play,
}: {
  opts: Record<string, unknown>;
  play?: { clip: string; nonce: number };
}) {
  const { styles } = useS();
  const hostRef = useRef<HTMLDivElement>(null);
  const mountedRef = useRef(false);
  const optsRef = useRef(opts);
  optsRef.current = opts;

  useEffect(() => {
    const host = hostRef.current;
    const Toy = window.ToyAgent;
    if (!host || !Toy) return;
    let cancelled = false;
    Toy.setOpts(optsRef.current);
    Toy.mount(host, { zoom: 1.18 })
      .then(() => {
        if (cancelled) Toy.unmount();
        else mountedRef.current = true;
      })
      .catch(() => {
        mountedRef.current = false;
      });
    return () => {
      cancelled = true;
      if (mountedRef.current) {
        try {
          Toy.unmount();
        } catch {
          /* already gone */
        }
        mountedRef.current = false;
      }
    };
  }, []);

  useEffect(() => {
    if (mountedRef.current) window.ToyAgent?.setOpts(opts);
  }, [opts]);

  useEffect(() => {
    if (play && mountedRef.current) window.ToyAgent?.play(play.clip, false);
  }, [play?.nonce]); // eslint-disable-line react-hooks/exhaustive-deps

  const hasAgent = typeof window !== "undefined" && !!window.ToyAgent;
  return (
    <div className={styles.stage}>
      {hasAgent ? (
        <div ref={hostRef} className={styles.canvasHost} />
      ) : (
        <div className={styles.fallback} aria-hidden="true">
          🤖
        </div>
      )}
    </div>
  );
}
