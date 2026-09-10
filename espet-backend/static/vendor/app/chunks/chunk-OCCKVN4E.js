import{F as p,I as c,V as a}from"./chunk-HLESZX2U.js";import{d as l}from"./chunk-NM6J3ND7.js";var t=l(p(),1),i=c(({css:e})=>({wrap:e`
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  `,dock:e`
    position: fixed;
    right: 14px;
    bottom: 14px;
    z-index: 35;
    display: flex;
    align-items: center;
    gap: 8px;
    @media (max-width: 819px) {
      display: none;
    }
  `,chip:e`
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 7px 15px;
    border-radius: 999px;
    font-size: 13px;
    font-weight: 500;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    backdrop-filter: blur(14px) saturate(1.1);
    -webkit-backdrop-filter: blur(14px) saturate(1.1);
    box-shadow: 0 6px 24px rgba(0, 0, 0, 0.45);
    white-space: nowrap;
    transition: border-color 0.2s ease, color 0.2s ease;
  `,neutral:e`
    color: #d4d4d4;
  `,success:e`
    color: #8fd9ae;
    background: rgba(143, 217, 174, 0.1);
    border-color: rgba(143, 217, 174, 0.28);
  `,warning:e`
    color: #e6cf94;
    background: rgba(230, 207, 148, 0.1);
    border-color: rgba(230, 207, 148, 0.28);
  `,danger:e`
    color: #eda7a7;
    background: rgba(237, 167, 167, 0.1);
    border-color: rgba(237, 167, 167, 0.28);
  `,dot:e`
    display: inline-block;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    flex: none;
  `,pulse:e`
    @keyframes espet-chip-pulse {
      0%,
      100% {
        opacity: 1;
      }
      50% {
        opacity: 0.3;
      }
    }
    animation: espet-chip-pulse 1.6s ease-in-out infinite;
  `})),d={neutral:"#a3a3a3",success:"#8fd9ae",warning:"#e6cf94",danger:"#eda7a7"};function o({tone:e="neutral",dot:n=!0,dotColor:g,pulse:x,title:b,className:f,children:h}){let{styles:r,cx:s}=i();return(0,t.jsxs)("span",{title:b,className:s(r.chip,r[e],f),children:[n&&(0,t.jsx)("span",{className:s(r.dot,x&&r.pulse),style:{background:g??d[e]}}),h]})}function k({section:e}){let{dirty:n}=a();return n[e]?(0,t.jsx)(o,{tone:"warning",pulse:!0,title:"Edits not saved to the device yet",children:"Unsaved Changes"}):null}function u({maxVersion:e}){return(0,t.jsxs)(o,{tone:"neutral",dotColor:d.success,title:"Highest config version the device will pull next sync",children:["Config v",e??"\u2013"]})}function S(){let{serverOnline:e,maxVersion:n}=a();return(0,t.jsxs)("div",{className:i().styles.dock,"aria-label":"System status",children:[(0,t.jsx)(u,{maxVersion:n}),(0,t.jsx)(o,{tone:e?"success":"danger",children:e?"Backend Online":"Backend Offline"})]})}function w(){let{serverOnline:e,maxVersion:n}=a();return(0,t.jsxs)("div",{className:i().styles.wrap,children:[(0,t.jsx)(u,{maxVersion:n}),(0,t.jsx)(o,{tone:e?"success":"danger",children:e?"Backend Online":"Backend Offline"})]})}export{o as a,k as b,S as c,w as d};
