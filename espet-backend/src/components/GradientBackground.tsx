/* Ambient animated gradient orbs — pure CSS keyframes (global.css). Replaces
   the legacy anime.js orb drift; honours prefers-reduced-motion via CSS. */
export function GradientBackground() {
  return (
    <div className="espet-bg" aria-hidden="true">
      <div className="espet-orb a" />
      <div className="espet-orb b" />
      <div className="espet-orb c" />
      <div className="espet-orb d" />
    </div>
  );
}
