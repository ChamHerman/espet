/* Avatar auto-style — the robot dresses itself from the agent's own settings
   (SOUL.md keywords, memory volume, scheduled jobs, user name). Ported from
   legacy app.js so behaviour is identical. opts + human-readable traits. */

import type { AvatarState, ConfigBundle } from "../types";

export const TOY_BODY = [
  "10B981",
  "3B82F6",
  "8B5CF6",
  "F59E0B",
  "EF4444",
  "EC4899",
  "14B8A6",
  "64748B",
];
export const TOY_HAT_COLOR = [
  "262E33",
  "FF6B6B",
  "FFD93D",
  "65C9FF",
  "C084FC",
  "A7FFC4",
];

export const TOY_HATS = ["none", "tophat", "party", "propeller", "beanie"];
export const TOY_GLASSES = ["none", "round", "visor"];
export const TOY_ANTENNA = ["none", "ball"];
export const TOY_BOWTIE = ["none", "on"];
export const TOY_MOODS = ["idle", "dance", "wave", "jump", "thumbsup"];

export const TOY_LABELS: Record<string, string> = {
  none: "None",
  tophat: "Top hat",
  party: "Party cone",
  propeller: "Propeller",
  beanie: "Beanie",
  round: "Round specs",
  visor: "Visor",
  ball: "Ball",
  on: "On",
  idle: "Idle",
  dance: "Dance",
  wave: "Wave",
  jump: "Jump",
  thumbsup: "Thumbs up",
};

export interface AvatarOpts extends Record<string, string | number | boolean> {
  bodyColor: string;
  headColor: string;
  hat: string;
  hatColor: string;
  glasses: string;
  antenna: string;
  bowtie: string;
  mood: string;
}

const pick = <T,>(arr: T[]): T => arr[Math.floor(Math.random() * arr.length)];

export function deriveAvatar(cfg: ConfigBundle): { opts: AvatarOpts; traits: string[] } {
  const soul = (cfg.soul_md || "").toLowerCase();
  const user = (cfg.user_md || "").toLowerCase();
  const memLines = (cfg.memory_md || "")
    .split("\n")
    .filter((l) => l.trim().startsWith("-")).length;
  const jobs = (cfg.jobs || []).length;
  const nameMatch = user.match(/- name:\s*(.+)/);
  const name =
    (nameMatch ? nameMatch[1] : cfg.device?.name || "ESPET")
      .replace(/[()]/g, "")
      .trim() || "ESPET";

  let hash = 0;
  for (const c of name) hash = (hash * 31 + c.charCodeAt(0)) >>> 0;

  const opts: AvatarOpts = {
    bodyColor: TOY_BODY[hash % TOY_BODY.length],
    headColor: TOY_BODY[(hash >> 3) % TOY_BODY.length],
    hat: "none",
    hatColor: TOY_HAT_COLOR[hash % TOY_HAT_COLOR.length],
    glasses: "none",
    antenna: "ball",
    bowtie: "none",
    mood: "idle",
  };

  const traits = [`"${name}" → factory paint job`];
  if (/friendly|helpful/.test(soul)) {
    opts.mood = "wave";
    traits.push("friendly → waves hello");
  }
  if (/curious|eager to learn/.test(soul)) traits.push("curious → antenna up");
  if (/concise|stop yapping|to the point/.test(soul)) {
    opts.antenna = "none";
    traits.push("concise → stripped to basics");
  }
  if (/professional|formal|business/.test(soul)) {
    opts.bowtie = "on";
    opts.bodyColor = "334155";
    traits.push("professional → bow tie, slate paint");
  }
  if (/playful|fun|joke/.test(soul)) {
    opts.mood = "dance";
    opts.hat = "party";
    traits.push("playful → party cone & dance");
  }
  if (/privacy|safety|secure/.test(soul)) {
    opts.glasses = "visor";
    traits.push("privacy-first → visor down");
  }
  if (memLines >= 3 && opts.glasses === "none") {
    opts.glasses = "round";
    traits.push(`${memLines} memories → wise round specs`);
  }
  if (jobs > 0) {
    opts.hat = "tophat";
    traits.push(`${jobs} scheduled job${jobs > 1 ? "s" : ""} → top hat`);
  }
  return { opts, traits };
}

export function effectiveAvatar(
  cfg: ConfigBundle,
  avatar: AvatarState
): AvatarOpts {
  const { opts } = deriveAvatar(cfg);
  return avatar.mode === "custom"
    ? { ...opts, ...(avatar.opts as Partial<AvatarOpts>) }
    : opts;
}

export function randomOutfit(keepMood?: string): Partial<AvatarOpts> {
  const r30 = () => Math.random() < 0.3;
  return {
    bodyColor: pick(TOY_BODY),
    headColor: pick(TOY_BODY),
    hat: r30() ? pick(TOY_HATS.slice(1)) : "none",
    hatColor: pick(TOY_HAT_COLOR),
    glasses: r30() ? pick(TOY_GLASSES.slice(1)) : "none",
    antenna: Math.random() < 0.5 ? "ball" : "none",
    bowtie: r30() ? "on" : "none",
    mood: keepMood || "idle",
  };
}
