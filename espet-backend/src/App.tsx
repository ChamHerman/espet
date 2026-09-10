/* App shell: provider stack (lobe/antd theme → antd App context → store),
   hash router with an unsaved-changes guard, glass nav, page header, and the
   Suspense-lazy view outlet. Views are code-split so editor/chart chunks load
   only when opened. */

import { App as AntdApp, Card as AntdCard, Flex, Spin } from "antd";
import { createStyles } from "antd-style";
import { lazy, Suspense, useEffect, useRef, useState } from "react";
import { GradientBackground } from "./components/GradientBackground";
import { GlassNav } from "./components/GlassNav";
import { StatusDock } from "./components/StatusChips";
import { parseRoute, viewMeta } from "./routes";
import { StoreProvider, useApp } from "./store";
import { AppProviders } from "./theme";

const views: Record<string, React.ComponentType<{ section?: string }>> = {
  overview: lazy(() => import("./views/Home").then((m) => ({ default: m.HomeView }))),
  agent: lazy(() => import("./views/Agent").then((m) => ({ default: m.AgentView }))),
  chat: lazy(() => import("./views/Chat").then((m) => ({ default: m.ChatView }))),
  camera: lazy(() => import("./views/Camera").then((m) => ({ default: m.CameraView }))),
  assistant: lazy(() => import("./views/AssistantPage").then((m) => ({ default: m.AssistantPage }))),
  system: lazy(() => import("./views/SystemPage").then((m) => ({ default: m.SystemPage }))),
};

const useS = createStyles(({ css }) => ({
  shell: css`
    position: relative;
    z-index: 1;
    min-height: 100dvh;
    display: flex;
    flex-direction: column;
  `,
  content: css`
    flex: 1 1 auto;
    width: 100%;
    max-width: 1080px;
    margin: 0 auto;
    padding: 22px 22px 90px;
  `,
  head: css`
    margin-bottom: 18px;
    h1 {
      margin: 0;
      font-size: 1.35rem;
      font-weight: 700;
      letter-spacing: -0.2px;
    }
    p {
      margin: 4px 0 0;
      color: #a3a3a3;
      font-size: 0.9rem;
      max-width: 720px;
    }
  `,
  footer: css`
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 12px;
    padding: 26px 22px 30px;
    font-size: 12px;
    letter-spacing: 0.5px;
    background: none;
    border: none;
  `,
  footBrand: css`
    color: #a3a3a3;
    font-weight: 600;
  `,
  footSep: css`
    color: #3f3f46;
  `,
  footCredit: css`
    color: #737373;
  `,
}));

function readRoute(): { view: string; section?: string } {
  return parseRoute(location.hash.replace(/^#/, "") || "overview");
}

function Shell() {
  const { styles } = useS();
  const { anyDirty, clearAllDirty, confirm } = useApp();
  const [route, setRoute] = useState(readRoute);
  const dirtyRef = useRef(anyDirty);
  dirtyRef.current = anyDirty;

  // initial + manual hash edits
  useEffect(() => {
    if (!location.hash) location.replace("#overview");
    const on = () => setRoute(readRoute());
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);

  const navigate = (next: string) => {
    const target = parseRoute(next);
    if (target.view === route.view && target.section === route.section) return;
    if (dirtyRef.current) {
      confirm({
        title: "Unsaved changes",
        message: "You have unsaved edits on this page. Leave anyway?",
        okLabel: "Discard & leave",
        danger: true,
      }).then((ok) => {
        if (ok) {
          clearAllDirty();
          location.hash = next;
        }
      });
    } else {
      location.hash = next;
    }
  };

  const meta = viewMeta(route.view);
  const View = views[route.view] ?? views.overview;
  const isScrollPage = route.view === "assistant" || route.view === "system";

  return (
    <div className={styles.shell}>
      <GradientBackground />
      <GlassNav current={route.view} onNavigate={navigate} />
      <main className={styles.content} id="view-root">
        {route.view !== "chat" && (
          <header className={styles.head}>
            <h1>{meta.title}</h1>
            <p>{meta.subtitle}</p>
          </header>
        )}
        <Suspense
          fallback={
            <AntdCard style={{ background: "rgba(255,255,255,0.05)", border: "1px solid rgba(255,255,255,0.1)" }}>
              <Flex justify="center" style={{ padding: 48 }}>
                <Spin tip="Loading…" size="large" />
              </Flex>
            </AntdCard>
          }
        >
          <View section={isScrollPage ? route.section : undefined} />
        </Suspense>
      </main>
      <footer className={styles.footer}>
        <span className={styles.footBrand}>ESPET Control Center</span>
        <span className={styles.footSep}>·</span>
        <span className={styles.footCredit}>powered by MimiClaw</span>
      </footer>
      <StatusDock />
    </div>
  );
}

export function App() {
  return (
    <AppProviders>
      <AntdApp>
        <StoreProvider>
          <Shell />
        </StoreProvider>
      </AntdApp>
    </AppProviders>
  );
}
