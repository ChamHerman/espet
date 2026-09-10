/* Assistant — one scroll page stacking the agent-shaping sections in order:
   Personality → Profile → Memory → Schedule → Skills. The section views are
   reused unchanged (their own cards, dirty tracking and save flows all work);
   the right-side bubble rail tracks scroll position and deep-links sections. */

import { useEffect } from "react";
import { SectionRail } from "../components/SectionRail";
import { ASSISTANT_PAGE } from "../routes";
import { PersonalityView } from "./Personality";
import { ProfileView } from "./Profile";
import { MemoryView } from "./Memory";
import { ScheduleView } from "./Schedule";
import { SkillsView } from "./Skills";

export function AssistantPage({ section }: { section?: string }) {
  /* clicking "Assistant" again (hash → #assistant) returns to the top */
  useEffect(() => {
    if (!section) window.scrollTo({ top: 0, behavior: "smooth" });
  }, [section]);

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 30 }}>
      <SectionRail page="assistant" items={ASSISTANT_PAGE.items} initial={section} />
      <section id="sec-personality" className="espet-section">
        <PersonalityView />
      </section>
      <section id="sec-profile" className="espet-section">
        <ProfileView />
      </section>
      <section id="sec-memory" className="espet-section">
        <MemoryView />
      </section>
      <section id="sec-schedule" className="espet-section">
        <ScheduleView />
      </section>
      <section id="sec-skills" className="espet-section">
        <SkillsView />
      </section>
    </div>
  );
}
