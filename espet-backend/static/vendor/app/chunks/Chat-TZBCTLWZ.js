import{d as F}from"./chunk-JMIOUBDS.js";import"./chunk-7T3PPQMJ.js";import"./chunk-BMEHTLPM.js";import{a as j}from"./chunk-74S6OUS6.js";import{G as H,J as G,R as d,Z as L,a as X,f as M,j as _,l as O,m as S,t as z,w as J,x as U,y as $}from"./chunk-B5OUD7EE.js";import{d as R}from"./chunk-NM6J3ND7.js";var o=R(X(),1);var e=R(H(),1),K="espet.chat.v1",Y=6e4,P=80,q=G(({css:r})=>({wrap:r`
    display: flex;
    flex-direction: column;
    height: calc(100dvh - var(--espet-nav-h) - 32px);
    min-height: 420px;
  `,head:r`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 10px 2px;
  `,list:r`
    flex: 1 1 auto;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 8px 4px 16px;
    scroll-behavior: smooth;
  `,row:r`
    display: flex;
    gap: 10px;
    max-width: 860px;
  `,rowUser:r`
    margin-left: auto;
    flex-direction: row-reverse;
  `,bubble:r`
    padding: 10px 14px;
    border-radius: 16px;
    line-height: 1.6;
    color: #e8e8e8;
    max-width: 78%;
    word-break: break-word;
  `,agentBubble:r`
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-top-left-radius: 5px;
  `,userBubble:r`
    background: rgba(255, 255, 255, 0.13);
    border: 1px solid rgba(255, 255, 255, 0.24);
    border-top-right-radius: 5px;
  `,system:r`
    align-self: center;
    font-size: 12px;
    color: #737373;
  `,composer:r`
    display: flex;
    gap: 10px;
    align-items: flex-end;
    padding: 12px;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.06);
    backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.1);
  `,logo:r`
    width: 40px;
    height: 40px;
    border-radius: 11px;
    flex: none;
  `}));function Q(){try{return JSON.parse(localStorage.getItem(K)||"[]")}catch{return[]}}function x(r){try{localStorage.setItem(K,JSON.stringify(r.slice(-P)))}catch{}}function Z(){return`${window.location.protocol==="https:"?"wss:":"ws:"}//${window.location.host}/api/cloud/ws/chat`}function se(){let{toast:r}=L(),{styles:s,cx:k}=q(),[w,u]=(0,o.useState)(Q),[i,g]=(0,o.useState)("idle"),[f,A]=(0,o.useState)(!1),[v,C]=(0,o.useState)(!1),[T,B]=(0,o.useState)(""),c=(0,o.useRef)(null),m=(0,o.useRef)(),y=(0,o.useRef)(),I=(0,o.useRef)(0),D=(0,o.useRef)(!1),N=(0,o.useRef)(null);(0,o.useEffect)(()=>{N.current?.scrollTo({top:N.current.scrollHeight})},[w,v]);let h=(0,o.useCallback)(t=>{u(a=>{let l=[...a,{...t,id:`${Date.now()}-${Math.random().toString(36).slice(2,6)}`,ts:Date.now()}];return x(l),l.slice(-P)})},[]),E=(0,o.useCallback)(t=>{u(a=>{if(a.some(n=>n.id===t.id))return a;let l=[...a,t];return x(l),l.slice(-P)})},[]),p=(0,o.useCallback)(()=>{C(!1),m.current&&clearTimeout(m.current)},[]),b=(0,o.useCallback)(()=>{if(c.current)try{c.current.close()}catch{}let t=Z();g("connecting");let a;try{a=new WebSocket(t)}catch{g("closed"),r("Could not reach cloud chat gateway","error");return}c.current=a,a.onopen=()=>{g("open")},a.onmessage=l=>{try{let n=JSON.parse(l.data);n.type==="init"?(A(!!n.online),Array.isArray(n.history)&&n.history.length>0&&(u(n.history),x(n.history))):n.type==="device_status"?A(!!n.online):n.type==="status"&&n.status==="thinking"?C(!0):n.type==="response"?(p(),n.message?E(n.message):typeof n.content=="string"&&h({role:n.role||"agent",text:n.content,image:n.image})):n.type==="message"&&n.message?E(n.message):n.type==="photo"?(p(),h({role:"agent",text:n.caption||"",image:n.image_url||n.image})):n.type==="cleared"&&(u([]),x([]))}catch{p()}},a.onclose=()=>{g("closed"),p(),y.current&&clearTimeout(y.current),y.current=setTimeout(()=>{b()},3e3)},a.onerror=()=>{g("closed")}},[p,h,E,r]);(0,o.useEffect)(()=>(b(),()=>{c.current&&c.current.close(),m.current&&clearTimeout(m.current),y.current&&clearTimeout(y.current)}),[b]),(0,o.useEffect)(()=>{D.current=v},[v]);let W=()=>{let t=T.trim();if(!t)return;let a=c.current;if(!a||a.readyState!==WebSocket.OPEN){r("Gateway disconnected \u2014 connecting...","warning"),b();return}a.send(JSON.stringify({type:"message",content:t})),h({role:"user",text:t}),B(""),C(!0);let l=Date.now();I.current=l,m.current=setTimeout(()=>{D.current&&I.current===l&&(p(),h({role:"system",text:"No reply within 60s \u2014 the agent may be busy (tool calls) or unreachable."}))},Y)},V=()=>{u([]),x([]),c.current&&c.current.readyState===WebSocket.OPEN&&c.current.send(JSON.stringify({type:"clear"}))};return(0,e.jsxs)("div",{className:s.wrap,children:[(0,e.jsxs)("div",{className:s.head,children:[(0,e.jsxs)("div",{style:{display:"flex",alignItems:"center",gap:10},children:[(0,e.jsx)("div",{className:`espet-logomark ${s.logo}`,children:(0,e.jsx)("img",{src:"/static/brand/espet_logo.jpg",alt:"ESPET logo",width:40,height:40})}),(0,e.jsxs)("div",{children:[(0,e.jsx)("div",{style:{fontWeight:600},children:"ESPET \xB7 Desktop Console"}),(0,e.jsx)("div",{style:{fontSize:12,color:"#737373"},className:"espet-mono",children:"channel: cloud_link \xB7 session: dashboard"})]}),(0,e.jsx)(j,{tone:i!=="open"?"danger":f?"success":"warning",pulse:i==="connecting"||i==="open"&&!f,children:i!=="open"?i==="connecting"?"Connecting\u2026":"Gateway Offline":f?"ESP32 Online":"ESP32 Offline (Gateway ready)"})]}),(0,e.jsxs)(z,{children:[w.length>0&&(0,e.jsx)(S,{size:"small",onClick:V,icon:(0,e.jsx)(d,{name:"trash",size:14}),children:"Clear"}),(0,e.jsx)(_,{title:"Reconnect to cloud chat gateway",children:(0,e.jsx)(S,{size:"small",type:i==="open"?"default":"primary",icon:(0,e.jsx)(d,{name:"refresh",size:14}),onClick:b,children:i==="open"?"Reconnect":"Connect"})})]})]}),i==="open"&&!f&&(0,e.jsx)(M,{style:{marginBottom:8},type:"warning",showIcon:!0,message:"ESP32 \u786C\u4EF6\u8BBE\u5907\u672A\u8FDE\u63A5\u3002\u672C\u5730\u7F51\u5173\u5DF2\u5C31\u7EEA\uFF0C\u8BF7\u4E3A ESP32 \u901A\u7535\u5E76\u786E\u4FDD\u5176\u8FDE\u4E0A Wi-Fi\u3002"}),i!=="open"&&(0,e.jsx)(M,{style:{marginBottom:8},type:i==="connecting"?"info":"error",showIcon:!0,message:i==="connecting"?"\u6B63\u5728\u8FDE\u63A5\u5230\u672C\u5730\u4E91\u7F51\u5173\u2026":"\u4E0E\u672C\u5730\u7F51\u5173\u65AD\u5F00\u8FDE\u63A5\uFF0C\u6B63\u5728\u5C1D\u8BD5\u91CD\u65B0\u8FDE\u63A5\u2026"}),(0,e.jsxs)("div",{className:s.list,ref:N,children:[w.length===0&&(0,e.jsx)("div",{className:s.system,children:"\u684C\u9762\u7AEF\u4E3B\u63A7\u53F0\u5DF2\u5C31\u7EEA\u3002\u6D88\u606F\u5C06\u901A\u8FC7 Cloud Link \u4E13\u5C5E\u901A\u9053\u53D1\u9001\u7ED9 ESP32 Agent\u3002"}),w.map(t=>t.role==="system"?(0,e.jsx)("div",{className:s.system,children:t.text},t.id):(0,e.jsxs)("div",{className:k(s.row,t.role==="user"&&s.rowUser),children:[(0,e.jsx)(O,{size:32,style:{flex:"none",background:t.role==="user"?"rgba(255,255,255,0.16)":"linear-gradient(135deg,#f5f5f5,#a3a3a3)",color:t.role==="user"?"#f5f5f5":"#050505"},children:t.role==="user"?(0,e.jsx)(d,{name:"user",size:16}):(0,e.jsx)(d,{name:"bot",size:16})}),(0,e.jsxs)("div",{className:k(s.bubble,t.role==="user"?s.userBubble:s.agentBubble),children:[t.image&&(0,e.jsx)("div",{style:{marginBottom:t.text?10:0},children:(0,e.jsx)(J,{src:t.image,alt:"Captured photo",style:{maxHeight:340,maxWidth:"100%",borderRadius:12,objectFit:"contain",border:"1px solid rgba(255, 255, 255, 0.12)",background:"rgba(0, 0, 0, 0.35)"}})}),t.role==="agent"?(0,e.jsx)(F,{children:t.text}):(0,e.jsx)("span",{style:{whiteSpace:"pre-wrap"},children:t.text})]})]},t.id)),v&&(0,e.jsxs)("div",{className:s.row,children:[(0,e.jsx)(O,{size:32,style:{flex:"none",background:"linear-gradient(135deg,#f5f5f5,#a3a3a3)",color:"#050505"},children:(0,e.jsx)(d,{name:"bot",size:16})}),(0,e.jsx)("div",{className:k(s.bubble,s.agentBubble),children:(0,e.jsxs)(z,{children:[(0,e.jsx)($,{size:"small"}),(0,e.jsx)("span",{style:{color:"#a3a3a3"},children:"thinking\u2026"})]})})]})]}),(0,e.jsxs)("div",{className:s.composer,children:[(0,e.jsx)(U.TextArea,{value:T,autoSize:{minRows:1,maxRows:5},placeholder:i==="open"?f?"Message ESPET\u2026":"ESP32 \u79BB\u7EBF\u4E2D\uFF0C\u6D88\u606F\u5C06\u5728\u8BBE\u5907\u8FDE\u4E0A\u540E\u91CD\u4F20\u2026":"\u8FDE\u63A5\u4E2D\u2026",disabled:i!=="open",onChange:t=>B(t.target.value),onPressEnter:t=>{t.shiftKey||(t.preventDefault(),W())},style:{background:"transparent",border:"none",boxShadow:"none",color:"#e8e8e8"}}),(0,e.jsx)(S,{type:"primary",disabled:i!=="open"||!T.trim(),icon:(0,e.jsx)(d,{name:"send",size:15}),onClick:W,children:"Send"})]})]})}export{se as ChatView};
