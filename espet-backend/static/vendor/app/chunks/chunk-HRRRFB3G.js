import{G as g,J as h,R as f,a as y}from"./chunk-B5OUD7EE.js";import{d as c}from"./chunk-NM6J3ND7.js";var n=c(y(),1);var a=c(g(),1),k=h(({css:o})=>({rail:o`
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
  `,line:o`
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
  `,bubble:o`
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
  `,label:o`
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
  `,dot:o`
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
  `,active:o`
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
  `}));function z({page:o,items:t,initial:s}){let{styles:r,cx:d}=k(),[w,p]=(0,n.useState)(s??t[0]?.id??"");(0,n.useEffect)(()=>{let e=0,b=()=>{let A=window.innerHeight*.35,l=t[0]?.id??"";for(let u of t){let x=document.getElementById(`sec-${u.id}`);x&&x.getBoundingClientRect().top<=A&&(l=u.id)}window.innerHeight+window.scrollY>=document.documentElement.scrollHeight-4&&(l=t[t.length-1]?.id??l),p(l)},i=()=>{cancelAnimationFrame(e),e=requestAnimationFrame(b)};b(),window.addEventListener("scroll",i,{passive:!0}),window.addEventListener("resize",i);let m=new ResizeObserver(i);return m.observe(document.documentElement),()=>{window.removeEventListener("scroll",i),window.removeEventListener("resize",i),m.disconnect(),cancelAnimationFrame(e)}},[t]),(0,n.useEffect)(()=>{if(!s)return;let e=setTimeout(()=>{document.getElementById(`sec-${s}`)?.scrollIntoView({behavior:"smooth",block:"start"})},140);return()=>clearTimeout(e)},[s]);let v=e=>{p(e),document.getElementById(`sec-${e}`)?.scrollIntoView({behavior:"smooth",block:"start"}),history.replaceState(null,"",`#${o}/${e}`)};return(0,a.jsxs)("nav",{className:r.rail,"aria-label":"Sections",children:[(0,a.jsx)("span",{className:r.line}),t.map(e=>(0,a.jsxs)("button",{type:"button",className:d(r.bubble,"espet-rail-hoverable",w===e.id&&r.active),onClick:()=>v(e.id),title:e.label,children:[(0,a.jsx)("span",{className:d(r.label,"label"),children:e.label}),(0,a.jsx)("span",{className:d(r.dot,"dot"),children:(0,a.jsx)(f,{name:e.icon,size:15})})]},e.id))]})}var L=["Asia/Kuala_Lumpur","Asia/Singapore","Asia/Jakarta","Asia/Bangkok","Asia/Manila","Asia/Hong_Kong","Asia/Shanghai","Asia/Taipei","Asia/Seoul","Asia/Tokyo","Asia/Kolkata","Asia/Dubai","Australia/Perth","Australia/Sydney","Pacific/Auckland","Europe/London","Europe/Paris","Europe/Berlin","Europe/Moscow","America/New_York","America/Chicago","America/Denver","America/Los_Angeles","America/Toronto","America/Sao_Paulo","UTC"];export{z as a,L as b};
