import{a as L,b as G,c as V}from"./chunk-OWFZGM2X.js";import{C as R,F as k,I as S,Q as w,R as Y,V as $,a as j,m as A,t as W,y as I}from"./chunk-HLESZX2U.js";import{d as y}from"./chunk-NM6J3ND7.js";var u=y(j(),1);var h=y(j(),1);var B=y(k(),1),ae=S(({css:t})=>({stage:t`
    position: relative;
    width: 100%;
    height: 100%;
    min-height: 260px;
    display: grid;
    place-items: center;
    overflow: hidden;
  `,fallback:t`
    font-size: 5rem;
    opacity: 0.85;
  `,canvasHost:t`
    position: absolute;
    inset: 0;
    cursor: grab;
    &:active {
      cursor: grabbing;
    }
  `}));function U({opts:t,play:a}){let{styles:m}=ae(),o=(0,h.useRef)(null),i=(0,h.useRef)(!1),f=(0,h.useRef)(t);f.current=t,(0,h.useEffect)(()=>{let p=o.current,r=window.ToyAgent;if(!p||!r)return;let s=!1;return r.setOpts(f.current),r.mount(p,{zoom:1.18}).then(()=>{s?r.unmount():i.current=!0}).catch(()=>{i.current=!1}),()=>{if(s=!0,i.current){try{r.unmount()}catch{}i.current=!1}}},[]),(0,h.useEffect)(()=>{i.current&&window.ToyAgent?.setOpts(t)},[t]),(0,h.useEffect)(()=>{a&&i.current&&window.ToyAgent?.play(a.clip,!1)},[a?.nonce]);let l=typeof window<"u"&&!!window.ToyAgent;return(0,B.jsx)("div",{className:m.stage,children:l?(0,B.jsx)("div",{ref:o,className:m.canvasHost}):(0,B.jsx)("div",{className:m.fallback,"aria-hidden":"true",children:"\u{1F916}"})})}var v=["10B981","3B82F6","8B5CF6","F59E0B","EF4444","EC4899","14B8A6","64748B"],C=["262E33","FF6B6B","FFD93D","65C9FF","C084FC","A7FFC4"],D=["none","tophat","party","propeller","beanie"],M=["none","round","visor"],J=["none","ball"],q=["none","on"],K=["idle","dance","wave","jump","thumbsup"],Q={none:"None",tophat:"Top hat",party:"Party cone",propeller:"Propeller",beanie:"Beanie",round:"Round specs",visor:"Visor",ball:"Ball",on:"On",idle:"Idle",dance:"Dance",wave:"Wave",jump:"Jump",thumbsup:"Thumbs up"},T=t=>t[Math.floor(Math.random()*t.length)];function z(t){let a=(t.soul_md||"").toLowerCase(),m=(t.user_md||"").toLowerCase(),o=(t.memory_md||"").split(`
`).filter(O=>O.trim().startsWith("-")).length,i=(t.jobs||[]).length,f=m.match(/- name:\s*(.+)/),l=(f?f[1]:t.device?.name||"ESPET").replace(/[()]/g,"").trim()||"ESPET",p=0;for(let O of l)p=p*31+O.charCodeAt(0)>>>0;let r={bodyColor:v[p%v.length],headColor:v[(p>>3)%v.length],hat:"none",hatColor:C[p%C.length],glasses:"none",antenna:"ball",bowtie:"none",mood:"idle"},s=[`"${l}" \u2192 factory paint job`];return/friendly|helpful/.test(a)&&(r.mood="wave",s.push("friendly \u2192 waves hello")),/curious|eager to learn/.test(a)&&s.push("curious \u2192 antenna up"),/concise|stop yapping|to the point/.test(a)&&(r.antenna="none",s.push("concise \u2192 stripped to basics")),/professional|formal|business/.test(a)&&(r.bowtie="on",r.bodyColor="334155",s.push("professional \u2192 bow tie, slate paint")),/playful|fun|joke/.test(a)&&(r.mood="dance",r.hat="party",s.push("playful \u2192 party cone & dance")),/privacy|safety|secure/.test(a)&&(r.glasses="visor",s.push("privacy-first \u2192 visor down")),o>=3&&r.glasses==="none"&&(r.glasses="round",s.push(`${o} memories \u2192 wise round specs`)),i>0&&(r.hat="tophat",s.push(`${i} scheduled job${i>1?"s":""} \u2192 top hat`)),{opts:r,traits:s}}function X(t,a){let{opts:m}=z(t);return a.mode==="custom"?{...m,...a.opts}:m}function Z(t){let a=()=>Math.random()<.3;return{bodyColor:T(v),headColor:T(v),hat:a()?T(D.slice(1)):"none",hatColor:T(C),glasses:a()?T(M.slice(1)):"none",antenna:Math.random()<.5?"ball":"none",bowtie:a()?"on":"none",mood:t||"idle"}}var e=y(k(),1),re=S(({css:t})=>({stage:t`
    position: relative;
    height: 360px;
    border-radius: 18px;
    overflow: hidden;
    background: radial-gradient(
        120% 80% at 50% 120%,
        rgba(255, 255, 255, 0.08),
        transparent 60%
      ),
      linear-gradient(180deg, rgba(255, 255, 255, 0.02), transparent);
    border: 1px solid rgba(255, 255, 255, 0.07);
  `,name:t`
    position: absolute;
    bottom: 14px;
    left: 16px;
    font-weight: 700;
    font-size: 1.1rem;
    z-index: 2;
  `,badge:t`
    position: absolute;
    top: 14px;
    right: 16px;
    z-index: 2;
  `,group:t`
    margin-bottom: 14px;
  `,glabel:t`
    font-size: 12px;
    color: #a3a3a3;
    text-transform: uppercase;
    letter-spacing: 0.4px;
    margin-bottom: 8px;
  `,row:t`
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  `,chip:t`
    padding: 6px 12px;
    border-radius: 999px;
    font-size: 13px;
    cursor: pointer;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    display: inline-flex;
    align-items: center;
    gap: 7px;
    transition: all 0.15s ease;
    &:hover {
      border-color: rgba(255, 255, 255, 0.35);
    }
  `,chipOn:t`
    color: #050505;
    background: #f5f5f5;
    border-color: #f5f5f5;
    font-weight: 600;
  `,sw:t`
    width: 26px;
    height: 26px;
    border-radius: 50%;
    display: inline-grid;
    place-items: center;
    cursor: pointer;
    border: 2px solid transparent;
    & > span {
      width: 18px;
      height: 18px;
      border-radius: 50%;
    }
  `,swOn:t`
    border-color: #ffffff;
  `,trait:t`
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 5px 11px;
    border-radius: 999px;
    font-size: 12.5px;
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.09);
  `}));function ve(){let{config:t,patchConfig:a,toast:m}=$(),{styles:o,cx:i}=re(),{styles:f}=V(),[l,p]=(0,u.useState)({mode:"auto",opts:{}}),[r,s]=(0,u.useState)(!1),[O,ee]=(0,u.useState)({clip:"Idle",nonce:0}),F=(0,u.useRef)();(0,u.useEffect)(()=>{Y.avatar().then(n=>p({mode:n.mode,opts:n.opts||{}})).catch(()=>{})},[]);let H=(0,u.useMemo)(()=>t?z(t):null,[t]),c=(0,u.useMemo)(()=>t?X(t,l):null,[t,l]);if(!t||!H||!c)return null;let P=n=>{clearTimeout(F.current),F.current=setTimeout(async()=>{try{let g=await Y.saveAvatar(n.mode,n.opts);a(b=>{b.versions.avatar=g.versions?.avatar??b.versions.avatar})}catch(g){m(g.message,"error")}},400)},N=n=>{let g={mode:"custom",opts:{...l.opts,...n}};p(g),P(g)},te=()=>{let n={mode:"auto",opts:{}};p(n),P(n)},oe=()=>{N(Z(c.mood)),ee({clip:"Dance",nonce:Date.now()})},x=({label:n,key:g,values:b,current:_})=>(0,e.jsxs)("div",{className:o.group,children:[(0,e.jsx)("div",{className:o.glabel,children:n}),(0,e.jsx)("div",{className:o.row,children:b.map(d=>(0,e.jsx)("span",{className:i(o.chip,_===d&&o.chipOn),onClick:()=>N({[g]:d}),children:Q[d]||d},d))})]}),E=({label:n,key:g,palette:b,current:_})=>(0,e.jsxs)("div",{className:o.group,children:[(0,e.jsx)("div",{className:o.glabel,children:n}),(0,e.jsx)("div",{className:o.row,children:b.map(d=>(0,e.jsx)("span",{title:"#"+d,className:i(o.sw,String(_||"").toUpperCase()===d&&o.swOn),onClick:()=>N({[g]:d}),children:(0,e.jsx)("span",{style:{background:"#"+d}})},d))})]});return(0,e.jsxs)("div",{className:f.stack,children:[(0,e.jsxs)(L,{children:[(0,e.jsxs)("div",{className:o.stage,children:[(0,e.jsx)(U,{opts:c,play:O}),(0,e.jsx)("span",{className:o.badge,children:(0,e.jsx)(G,{tone:l.mode==="custom"?"default":"success",children:l.mode==="custom"?"Custom style":"Auto style \xB7 from your settings"})}),(0,e.jsx)("span",{className:o.name,children:t.device.name||"ESPET"})]}),(0,e.jsx)("div",{style:{display:"flex",justifyContent:"center",marginTop:16},children:(0,e.jsxs)(W,{wrap:!0,children:[(0,e.jsx)(A,{type:"primary",icon:(0,e.jsx)(w,{name:"bot",size:15}),onClick:()=>s(!0),children:"Customize appearance"}),(0,e.jsx)(A,{icon:(0,e.jsx)(w,{name:"dice",size:15}),onClick:oe,children:"Surprise me"}),(0,e.jsx)(A,{icon:(0,e.jsx)(w,{name:"refresh",size:15}),onClick:te,disabled:l.mode==="auto",children:"Reset to auto"})]})})]}),(0,e.jsxs)(L,{children:[(0,e.jsx)("div",{className:f.cardHead,children:(0,e.jsxs)("div",{className:f.cardTitle,children:[(0,e.jsx)(w,{name:"spark"})," Style traits from your settings"]})}),(0,e.jsx)("p",{className:f.cardSub,children:"The robot reads its own configuration and suits up accordingly \u2014 change SOUL.md, memories or schedules and it re-outfits. Drag the robot to spin it."}),(0,e.jsxs)("div",{style:{display:"flex",flexWrap:"wrap",gap:8},children:[H.traits.map(n=>(0,e.jsxs)(R,{className:o.trait,bordered:!1,children:[(0,e.jsx)(w,{name:"spark",size:13})," ",n]},n)),l.mode==="custom"&&(0,e.jsx)(R,{className:o.trait,bordered:!1,children:"Manual Overrides Active"})]})]}),(0,e.jsxs)(I,{title:"Dress-up workbench",open:r,onCancel:()=>s(!1),footer:(0,e.jsx)(A,{type:"primary",onClick:()=>s(!1),children:"Done"}),width:560,children:[(0,e.jsx)(x,{label:"Hat",values:D,current:c.hat},"hat"),(0,e.jsx)(x,{label:"Glasses",values:M,current:c.glasses},"glasses"),(0,e.jsx)(x,{label:"Antenna",values:J,current:c.antenna},"antenna"),(0,e.jsx)(x,{label:"Bow tie",values:q,current:c.bowtie},"bowtie"),(0,e.jsx)(x,{label:"Mood",values:K,current:c.mood},"mood"),(0,e.jsx)(E,{label:"Body paint",palette:v,current:c.bodyColor},"bodyColor"),(0,e.jsx)(E,{label:"Head paint",palette:v,current:c.headColor},"headColor"),(0,e.jsx)(E,{label:"Hat color",palette:C,current:c.hatColor},"hatColor")]})]})}export{ve as AgentView};
