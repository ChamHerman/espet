import{b as w,d as m}from"./chunks/chunk-3DF6D57D.js";import{a as Y,b as y,c as k,d as S,e as A}from"./chunks/chunk-UXN76GPX.js";import{c as H,d as O}from"./chunks/chunk-OCCKVN4E.js";import{D as R,F as p,I as x,N as F,Q as B,V as G,W as D,a as z,c as ae,g as M,q as T,u as I,x as L}from"./chunks/chunk-HLESZX2U.js";import{d}from"./chunks/chunk-NM6J3ND7.js";var Z=d(ae(),1);var n=d(z(),1);var g=d(p(),1);function V(){return(0,g.jsxs)("div",{className:"espet-bg","aria-hidden":"true",children:[(0,g.jsx)("div",{className:"espet-orb a"}),(0,g.jsx)("div",{className:"espet-orb b"}),(0,g.jsx)("div",{className:"espet-orb c"}),(0,g.jsx)("div",{className:"espet-orb d"})]})}var h=d(z(),1);var t=d(p(),1),_=[...Y,{id:y.id,label:y.label,icon:y.icon},{id:k.id,label:k.label,icon:k.icon}],P=`
  background: rgba(255, 255, 255, 0.06);
  backdrop-filter: blur(20px) saturate(1.1);
  -webkit-backdrop-filter: blur(20px) saturate(1.1);
  border: 1px solid rgba(255, 255, 255, 0.12);
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.55),
    inset 0 1px 0 rgba(255, 255, 255, 0.1);
`,U=x(({css:e})=>({spacer:e`
    height: var(--espet-nav-h);
    flex: none;
  `,bar:e`
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
    ${P}
  `,brand:e`
    display: flex;
    align-items: center;
    gap: 10px;
    cursor: pointer;
    flex: none;
  `,brandName:e`
    font-weight: 700;
    font-size: 1rem;
    letter-spacing: 0.2px;
  `,brandSub:e`
    font-size: 11px;
    color: #737373;
    margin-top: -3px;
  `,nav:e`
    display: flex;
    align-items: center;
    gap: 4px;
    flex: none;
  `,link:e`
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
  `,linkActive:e`
    color: #ffffff;
  `,linkLabel:e`
    position: relative;
    z-index: 1;
    display: inline-flex;
    align-items: center;
    gap: 8px;
  `,activePill:e`
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
  `,mbar:e`
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
    ${P}
  `,dock:e`
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
    ${P}
    background: rgba(255, 255, 255, 0.08);
  `,dockBtn:e`
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
  `,dockActive:e`
    color: #ffffff;
  `,dockPill:e`
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
  `,dockLabel:e`
    position: relative;
    z-index: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
  `}));function W(){let e=w();return(0,t.jsx)(m.span,{className:"espet-logomark",whileHover:e?void 0:{scale:1.12,rotate:-4},whileTap:e?void 0:{scale:.92},transition:{type:"spring",stiffness:400,damping:12},children:(0,t.jsx)("img",{src:"/static/brand/espet_logo.jpg",alt:"ESPET logo",width:46,height:46})})}function oe(){let[e,s]=(0,h.useState)(!1);return(0,h.useEffect)(()=>{let a=()=>s(window.scrollY>28);return a(),window.addEventListener("scroll",a,{passive:!0}),()=>window.removeEventListener("scroll",a)},[]),e}function re(){let[e,s]=(0,h.useState)(typeof window<"u"&&window.matchMedia("(max-width: 819px)").matches);return(0,h.useEffect)(()=>{let a=window.matchMedia("(max-width: 819px)"),c=()=>s(a.matches);return a.addEventListener("change",c),()=>a.removeEventListener("change",c)},[]),e}function ne({active:e,onClick:s,children:a}){let{styles:c,cx:l}=U(),i=w();return(0,t.jsxs)(m.div,{className:l(c.link,e&&c.linkActive),onClick:s,whileHover:i?void 0:{scale:1.05,y:-1},whileTap:i?void 0:{scale:.94},transition:{type:"spring",stiffness:420,damping:24},children:[e&&(0,t.jsx)(m.span,{layoutId:"espet-nav-pill",className:c.activePill,transition:i?{duration:0}:{type:"spring",stiffness:380,damping:30}}),(0,t.jsx)("span",{className:c.linkLabel,children:a})]})}function $({current:e,onNavigate:s}){let{styles:a,cx:c}=U(),l=re(),i=w(),v={scale:oe()&&!i?.97:1},b=r=>i?{duration:0}:{type:"spring",stiffness:260,damping:22,delay:r},C=_.map(r=>({id:r.id,label:r.label,icon:r.icon,active:e===r.id}));return l?(0,t.jsxs)(t.Fragment,{children:[(0,t.jsx)("div",{className:a.spacer}),(0,t.jsxs)(m.div,{className:a.mbar,initial:i?!1:{y:-80,opacity:0},animate:{y:0,opacity:1,...v},transition:b(0),style:{transformOrigin:"top center"},children:[(0,t.jsxs)("div",{className:a.brand,onClick:()=>s("overview"),children:[(0,t.jsx)(W,{}),(0,t.jsxs)("div",{children:[(0,t.jsx)("div",{className:a.brandName,children:"ESPET"}),(0,t.jsx)("div",{className:a.brandSub,children:S(e).title})]})]}),(0,t.jsx)(O,{})]}),(0,t.jsx)(m.nav,{className:a.dock,"aria-label":"Primary",initial:i?!1:{y:90,opacity:0},animate:{y:0,opacity:1},transition:b(.05),children:C.map(r=>(0,t.jsxs)(m.div,{className:c(a.dockBtn,r.active&&a.dockActive),onClick:()=>s(r.id),whileHover:i?void 0:{scale:1.06},whileTap:i?void 0:{scale:.86},transition:{type:"spring",stiffness:460,damping:22},children:[r.active&&(0,t.jsx)(m.span,{layoutId:"espet-dock-pill",className:a.dockPill,transition:i?{duration:0}:{type:"spring",stiffness:380,damping:28}}),(0,t.jsxs)("span",{className:a.dockLabel,children:[(0,t.jsx)(B,{name:r.icon,size:20}),(0,t.jsx)("span",{children:r.label})]})]},r.id))})]}):(0,t.jsxs)(t.Fragment,{children:[(0,t.jsx)("div",{className:a.spacer}),(0,t.jsxs)(m.div,{className:a.bar,initial:i?!1:{y:-90,opacity:0},animate:{y:0,opacity:1,...v},transition:b(0),children:[(0,t.jsxs)("div",{className:a.brand,onClick:()=>s("overview"),children:[(0,t.jsx)(W,{}),(0,t.jsxs)("div",{children:[(0,t.jsx)("div",{className:a.brandName,children:"ESPET"}),(0,t.jsx)("div",{className:a.brandSub,children:"Control Center"})]})]}),(0,t.jsx)("nav",{className:a.nav,"aria-label":"Primary",children:_.map(r=>(0,t.jsxs)(ne,{active:e===r.id,onClick:()=>s(r.id),children:[(0,t.jsx)(B,{name:r.icon,size:17}),r.label]},r.id))})]})]})}var j=d(p(),1),u={accent:"#e5e5e5",blue:"#93c5fd",success:"#8fd9ae",warning:"#e6cf94",danger:"#eda7a7",successBg:"rgba(143, 217, 174, 0.10)",warningBg:"rgba(230, 207, 148, 0.10)",dangerBg:"rgba(237, 167, 167, 0.10)",successBorder:"rgba(143, 217, 174, 0.28)",warningBorder:"rgba(230, 207, 148, 0.28)",dangerBorder:"rgba(237, 167, 167, 0.28)",violet:"#a3a3a3",amber:"#d4d4d4",red:"#a3a3a3",bg:"#050505",elevated:"#101010",text:"#f5f5f5",textMuted:"#a3a3a3",textDim:"#737373",border:"rgba(255,255,255,0.08)",glass:"rgba(255,255,255,0.055)",glassStrong:"rgba(255,255,255,0.08)"},ie={algorithm:R.darkAlgorithm,token:{colorPrimary:u.accent,colorInfo:"#d4d4d4",colorSuccess:u.success,colorWarning:u.warning,colorError:u.danger,colorBgBase:u.bg,colorBgContainer:u.elevated,borderRadius:14,controlHeight:40,fontFamily:'Outfit, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif',fontFamilyCode:"'JetBrains Mono', ui-monospace, monospace",fontSize:15},components:{Modal:{contentBg:"#0d0d0d",headerBg:"#0d0d0d"},Message:{contentBg:"#141414"},Card:{colorBgContainer:"rgba(255,255,255,0.055)"},Segmented:{itemSelectedBg:"rgba(147, 197, 253, 0.22)",itemSelectedColor:"#dbeafe"},Radio:{colorPrimary:"rgba(147, 197, 253, 0.5)",buttonSolidCheckedBg:"rgba(147, 197, 253, 0.22)",buttonSolidCheckedColor:"#dbeafe",buttonSolidCheckedBorderColor:"rgba(147, 197, 253, 0.35)"},Button:{paddingInline:18,paddingInlineSM:12,paddingInlineLG:26,controlHeight:40,controlHeightSM:30,fontWeight:500}}};function J({children:e}){return(0,j.jsx)(F,{appearance:"dark",themeMode:"dark",theme:ie,children:e})}var o=d(p(),1),q={overview:(0,n.lazy)(()=>import("./chunks/Home-WQ7WBSGL.js").then(e=>({default:e.HomeView}))),agent:(0,n.lazy)(()=>import("./chunks/Agent-G4JAZ3H4.js").then(e=>({default:e.AgentView}))),cloud:(0,n.lazy)(()=>import("./chunks/Cloud-VIEW.js").then(e=>({default:e.CloudView}))),chat:(0,n.lazy)(()=>import("./chunks/Chat-IJXIMZRP.js").then(e=>({default:e.ChatView}))),camera:(0,n.lazy)(()=>import("./chunks/Camera-ZMUBYH2J.js").then(e=>({default:e.CameraView}))),assistant:(0,n.lazy)(()=>import("./chunks/AssistantPage-WSFF4DZJ.js").then(e=>({default:e.AssistantPage}))),system:(0,n.lazy)(()=>import("./chunks/SystemPage-E2VR4534.js").then(e=>({default:e.SystemPage})))},se=x(({css:e})=>({shell:e`
    position: relative;
    z-index: 1;
    min-height: 100dvh;
    display: flex;
    flex-direction: column;
  `,content:e`
    flex: 1 1 auto;
    width: 100%;
    max-width: 1080px;
    margin: 0 auto;
    padding: 22px 22px 90px;
  `,head:e`
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
  `,footer:e`
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 12px;
    padding: 26px 22px 30px;
    font-size: 12px;
    letter-spacing: 0.5px;
    background: none;
    border: none;
  `,footBrand:e`
    color: #a3a3a3;
    font-weight: 600;
  `,footSep:e`
    color: #3f3f46;
  `,footCredit:e`
    color: #737373;
  `}));function K(){let h=location.hash.replace(/^#/,"")||"overview";return A(h)}function le(){let{styles:e}=se(),{anyDirty:s,clearAllDirty:a,confirm:c}=G(),[l,i]=(0,n.useState)(K),N=(0,n.useRef)(s);N.current=s,(0,n.useEffect)(()=>{location.hash||location.replace("#overview");let f=()=>i(K());return window.addEventListener("hashchange",f),()=>window.removeEventListener("hashchange",f)},[]);let v=f=>{let E=A(f);E.view===l.view&&E.section===l.section||(N.current?c({title:"Unsaved changes",message:"You have unsaved edits on this page. Leave anyway?",okLabel:"Discard & leave",danger:!0}).then(te=>{te&&(a(),location.hash=f)}):location.hash=f)},b=S(l.view),C=q[l.view]??q.overview,r=l.view==="assistant"||l.view==="system";return(0,o.jsxs)("div",{className:e.shell,children:[(0,o.jsx)(V,{}),(0,o.jsx)($,{current:l.view,onNavigate:v}),(0,o.jsxs)("main",{className:e.content,id:"view-root",children:[l.view!=="chat"&&(0,o.jsxs)("header",{className:e.head,children:[(0,o.jsx)("h1",{children:b.title}),(0,o.jsx)("p",{children:b.subtitle})]}),(0,o.jsx)(n.Suspense,{fallback:(0,o.jsx)(T,{style:{background:"rgba(255,255,255,0.05)",border:"1px solid rgba(255,255,255,0.1)"},children:(0,o.jsx)(I,{justify:"center",style:{padding:48},children:(0,o.jsx)(L,{tip:"Loading\u2026",size:"large"})})}),children:(0,o.jsx)(C,{section:r?l.section:void 0})})]}),(0,o.jsxs)("footer",{className:e.footer,children:[(0,o.jsx)("span",{className:e.footBrand,children:"ESPET Control Center"}),(0,o.jsx)("span",{className:e.footSep,children:"\xB7"}),(0,o.jsx)("span",{className:e.footCredit,children:"powered by MimiClaw"})]}),(0,o.jsx)(H,{})]})}function Q(){return(0,o.jsx)(J,{children:(0,o.jsx)(M,{children:(0,o.jsx)(D,{children:(0,o.jsx)(le,{})})})})}var ee=d(p(),1),X=document.getElementById("root");X&&(0,Z.createRoot)(X).render((0,ee.jsx)(Q,{}));
