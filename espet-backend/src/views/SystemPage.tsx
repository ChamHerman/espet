/* System — one scroll page stacking Device → Tools with the same right-side
   bubble rail as the Assistant page. */

import { useEffect } from "react";
import { SectionRail } from "../components/SectionRail";
import { SYSTEM_PAGE } from "../routes";
import { DeviceView } from "./Device";
import { ToolsView } from "./Tools";

export function SystemPage({ section }: { section?: string }) {
  useEffect(() => {
    if (!section) window.scrollTo({ top: 0, behavior: "smooth" });
  }, [section]);

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 30 }}>
      <SectionRail page="system" items={SYSTEM_PAGE.items} initial={section} />
      <section id="sec-device" className="espet-section">
        <DeviceView />
      </section>
      <section id="sec-tools" className="espet-section">
        <ToolsView />
      </section>
    </div>
  );
}
