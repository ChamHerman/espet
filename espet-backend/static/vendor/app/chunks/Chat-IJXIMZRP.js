import{d as J}from"./chunk-CHVLPIXG.js";import"./chunk-7T3PPQMJ.js";import"./chunk-3DF6D57D.js";import{a as H}from"./chunk-OCCKVN4E.js";import{F as I,I as P,Q as l,S as W,U as _,V as U,a as X,f as A,j as D,l as S,m as h,t as k,w as B,x as O}from"./chunk-HLESZX2U.js";import{d as w}from"./chunk-NM6J3ND7.js";var o=w(X(),1);var e=w(I(),1),L="espet.chat.v1",Y="dashboard",q=6e4,K=80,F=P(({css:n})=>({wrap:n`
    display: flex;
    flex-direction: column;
    height: calc(100dvh - var(--espet-nav-h) - 32px);
    min-height: 420px;
  `,head:n`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 10px 2px;
  `,list:n`
    flex: 1 1 auto;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 8px 4px 16px;
    scroll-behavior: smooth;
  `,row:n`
    display: flex;
    gap: 10px;
    max-width: 860px;
  `,rowUser:n`
    margin-left: auto;
    flex-direction: row-reverse;
  `,bubble:n`
    padding: 10px 14px;
    border-radius: 16px;
    line-height: 1.6;
    color: #e8e8e8;
    max-width: 78%;
    word-break: break-word;
  `,agentBubble:n`
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-top-left-radius: 5px;
  `,userBubble:n`
    background: rgba(255, 255, 255, 0.13);
    border: 1px solid rgba(255, 255, 255, 0.24);
    border-top-right-radius: 5px;
  `,system:n`
    align-self: center;
    font-size: 12px;
    color: #737373;
  `,composer:n`
    display: flex;
    gap: 10px;
    align-items: flex-end;
    padding: 12px;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.06);
    backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.1);
  `,logo:n`
    width: 40px;
    height: 40px;
    border-radius: 11px;
    flex: none;
  `}));function G(){try{return JSON.parse(localStorage.getItem(L)||"[]")}catch{return[]}}function $(n){try{localStorage.setItem(L,JSON.stringify(n.slice(-K)))}catch{}}function ae(){let{toast:n}=U(),{styles:r,cx:b}=F(),[g,C]=(0,o.useState)(G),[a,c]=(0,o.useState)("idle"),[u,N]=(0,o.useState)(!1),[v,T]=(0,o.useState)(""),f=(0,o.useRef)(null),d=(0,o.useRef)(),M=(0,o.useRef)(0),z=(0,o.useRef)(!1),x=(0,o.useRef)(null);(0,o.useEffect)(()=>{x.current?.scrollTo({top:x.current.scrollHeight})},[g,u]);let p=(0,o.useCallback)(t=>{C(s=>{let i=[...s,{...t,id:`${Date.now()}-${Math.random().toString(36).slice(2,6)}`,ts:Date.now()}];return $(i),i.slice(-K)})},[]),m=()=>{N(!1),d.current&&clearTimeout(d.current)},j=(0,o.useCallback)(()=>{f.current?.close();let t=_();c("connecting");let s;try{s=new WebSocket(t)}catch{c("closed"),n("Could not reach device gateway","error");return}f.current=s,s.onopen=()=>c("open"),s.onmessage=i=>{try{let y=JSON.parse(i.data);y.type==="response"&&typeof y.content=="string"&&(m(),p({role:"agent",text:y.content}))}catch{m(),p({role:"agent",text:String(i.data??"")})}},s.onclose=()=>{c("closed"),m()},s.onerror=()=>c("closed")},[p,n]);(0,o.useEffect)(()=>()=>{f.current?.close(),d.current&&clearTimeout(d.current)},[]),(0,o.useEffect)(()=>{z.current=u},[u]);let R=()=>{let t=v.trim();if(!t)return;let s=f.current;if(!s||s.readyState!==WebSocket.OPEN){n("Device offline \u2014 connect first (top right)","error");return}s.send(JSON.stringify({type:"message",content:t,chat_id:Y})),p({role:"user",text:t}),T(""),N(!0);let i=Date.now();M.current=i,d.current=setTimeout(()=>{z.current&&M.current===i&&(m(),p({role:"system",text:"No reply within 60s \u2014 the agent may be busy (tool calls) or unreachable."}))},q)},V=()=>{C([]),$([])},E=W();return(0,e.jsxs)("div",{className:r.wrap,children:[(0,e.jsxs)("div",{className:r.head,children:[(0,e.jsxs)("div",{style:{display:"flex",alignItems:"center",gap:10},children:[(0,e.jsx)("div",{className:`espet-logomark ${r.logo}`,children:(0,e.jsx)("img",{src:"/static/brand/espet_logo.jpg",alt:"ESPET logo",width:40,height:40})}),(0,e.jsxs)("div",{children:[(0,e.jsx)("div",{style:{fontWeight:600},children:"ESPET \xB7 live chat"}),(0,e.jsx)("div",{style:{fontSize:12,color:"#737373"},className:"espet-mono",children:E})]}),(0,e.jsx)(H,{tone:a==="open"?"success":a==="connecting"?"warning":a==="closed"?"danger":"neutral",pulse:a==="connecting",children:a==="open"?"Connected":a==="connecting"?"Connecting\u2026":a==="closed"?"Offline":"Not Connected"})]}),(0,e.jsxs)(k,{children:[g.length>0&&(0,e.jsx)(h,{size:"small",onClick:V,icon:(0,e.jsx)(l,{name:"trash",size:14}),children:"Clear"}),(0,e.jsx)(D,{title:"Open (or retry) the WebSocket to the device gateway",children:(0,e.jsx)(h,{size:"small",type:a==="open"?"default":"primary",icon:(0,e.jsx)(l,{name:"refresh",size:14}),onClick:j,children:a==="open"?"Reconnect":"Connect"})})]})]}),a!=="open"&&(0,e.jsx)(A,{style:{marginBottom:8},type:a==="connecting"?"info":"warning",showIcon:!0,message:a==="connecting"?"Connecting to the device\u2026":`Not connected. Make sure the device is on this network and its address is set in Device \u2192 Live-chat device address (currently ${E}).`}),(0,e.jsxs)("div",{className:r.list,ref:x,children:[g.length===0&&(0,e.jsx)("div",{className:r.system,children:"Messages go straight to the agent loop on the device \u2014 try \u201Chello\u201D or \u201Cwhat can you do?\u201D."}),g.map(t=>t.role==="system"?(0,e.jsx)("div",{className:r.system,children:t.text},t.id):(0,e.jsxs)("div",{className:b(r.row,t.role==="user"&&r.rowUser),children:[(0,e.jsx)(S,{size:32,style:{flex:"none",background:t.role==="user"?"rgba(255,255,255,0.16)":"linear-gradient(135deg,#f5f5f5,#a3a3a3)",color:t.role==="user"?"#f5f5f5":"#050505"},children:t.role==="user"?(0,e.jsx)(l,{name:"user",size:16}):(0,e.jsx)(l,{name:"bot",size:16})}),(0,e.jsx)("div",{className:b(r.bubble,t.role==="user"?r.userBubble:r.agentBubble),children:t.role==="agent"?(0,e.jsx)(J,{children:t.text}):(0,e.jsx)("span",{style:{whiteSpace:"pre-wrap"},children:t.text})})]},t.id)),u&&(0,e.jsxs)("div",{className:r.row,children:[(0,e.jsx)(S,{size:32,style:{flex:"none",background:"linear-gradient(135deg,#f5f5f5,#a3a3a3)",color:"#050505"},children:(0,e.jsx)(l,{name:"bot",size:16})}),(0,e.jsx)("div",{className:b(r.bubble,r.agentBubble),children:(0,e.jsxs)(k,{children:[(0,e.jsx)(O,{size:"small"}),(0,e.jsx)("span",{style:{color:"#a3a3a3"},children:"thinking\u2026"})]})})]})]}),(0,e.jsxs)("div",{className:r.composer,children:[(0,e.jsx)(B.TextArea,{value:v,autoSize:{minRows:1,maxRows:5},placeholder:a==="open"?"Message ESPET\u2026":"Connect to the device first",disabled:a!=="open",onChange:t=>T(t.target.value),onPressEnter:t=>{t.shiftKey||(t.preventDefault(),R())},style:{background:"transparent",border:"none",boxShadow:"none",color:"#e8e8e8"}}),(0,e.jsx)(h,{type:"primary",disabled:a!=="open"||!v.trim(),icon:(0,e.jsx)(l,{name:"send",size:15}),onClick:R,children:"Send"})]})]})}export{ae as ChatView};
