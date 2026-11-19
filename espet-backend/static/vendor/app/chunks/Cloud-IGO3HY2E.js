import{a as v,b as N,c as K}from"./chunk-M6Q6TWUZ.js";import{a as J}from"./chunk-74S6OUS6.js";import{G as $,J as U,R as l,Z as j,a as se,m as p,r as M,x as c}from"./chunk-B5OUD7EE.js";import{d as S}from"./chunk-NM6J3ND7.js";var o=S(se(),1);var e=S($(),1),oe="espet_secret_device_key_2026";async function r(t,n={}){let s=await fetch(t,{...n,headers:{"Content-Type":"application/json",Authorization:`Bearer ${oe}`,...n.headers||{}}});if(!s.ok){let f=s.statusText;try{let h=await s.json();f=h.detail||h.error||f}catch{}throw new Error(f)}return s.json()}var ie=U(({css:t})=>({stack:t`
    display: flex;
    flex-direction: column;
    gap: 20px;
  `,statusGrid:t`
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 14px;
    margin-bottom: 8px;
  `,statusBox:t`
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    padding: 14px 16px;
  `,statusLabel:t`
    font-size: 11px;
    color: #737373;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-weight: 500;
  `,statusVal:t`
    font-size: 14px;
    font-weight: 500;
    margin-top: 6px;
    display: flex;
    align-items: center;
    gap: 8px;
  `,formGroup:t`
    margin-bottom: 16px;
    &:last-child {
      margin-bottom: 0;
    }
  `,fieldLabel:t`
    display: block;
    font-size: 13px;
    font-weight: 500;
    margin-bottom: 6px;
    color: #f5f5f5;
  `,fieldHint:t`
    font-size: 12px;
    color: #737373;
    margin-top: 5px;
  `,toggleBox:t`
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 18px;
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    margin-bottom: 18px;
    gap: 16px;
  `,btnRow:t`
    display: flex;
    gap: 10px;
    align-items: center;
    justify-content: flex-end;
    margin-top: 18px;
  `,table:t`
    width: 100%;
    border-collapse: collapse;
    margin-top: 12px;
    font-size: 13px;
    th,
    td {
      text-align: left;
      padding: 10px 12px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    }
    th {
      color: #737373;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      font-weight: 600;
    }
  `,unbindBtn:t`
    color: #eda7a7;
    background: none;
    border: none;
    padding: 4px 8px;
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: opacity 0.15s;
    &:hover {
      text-decoration: underline;
      opacity: 0.85;
    }
  `,bindForm:t`
    display: grid;
    grid-template-columns: 1fr 1fr auto;
    gap: 10px;
    margin-top: 14px;
    @media (max-width: 640px) {
      grid-template-columns: 1fr;
    }
  `}));function ue(){let{styles:t}=ie(),n=K(),{toast:s}=j(),[f,h]=(0,o.useState)(!0),[V,L]=(0,o.useState)(!1),[W,C]=(0,o.useState)(!1),[F,B]=(0,o.useState)(!1),[_,I]=(0,o.useState)(!1),[x,P]=(0,o.useState)(""),[w,D]=(0,o.useState)("https://api.openai.com/v1"),[k,z]=(0,o.useState)(""),[T,R]=(0,o.useState)(""),[g,q]=(0,o.useState)(!1),[y,Q]=(0,o.useState)([]),[A,X]=(0,o.useState)({}),[E,O]=(0,o.useState)(""),[G,H]=(0,o.useState)(""),m=(0,o.useCallback)(async()=>{try{let a=await r("/api/cloud/gateway-config");I(!!a.cloud_gateway_enabled),P(a.telegram_bot_token||""),D(a.llm_base_url||"https://api.openai.com/v1"),z(a.llm_api_key||""),R(a.llm_model||""),q(!!a.poller_running);let i=await r("/api/cloud/devices");Q(i.connected||[]);let u=await r("/api/cloud/bindings");X(u.bindings||{})}catch(a){s("Failed to load cloud gateway: "+a.message,"error")}finally{h(!1)}},[s]);(0,o.useEffect)(()=>{m()},[m]);let Y=async()=>{let a=x.trim();if(!a){s("Enter a bot token first","error");return}C(!0);try{let i=await r("/api/cloud/test-telegram",{method:"POST",body:JSON.stringify({token:a})});i.ok&&i.bot?s(`Token valid! Bot: @${i.bot.username} (${i.bot.first_name})`,"success"):s("Telegram check failed: "+(i.error||"unknown error"),"error")}catch(i){s("Telegram test failed: "+i.message,"error")}finally{C(!1)}},Z=async()=>{let a=k.trim(),i=w.trim(),u=T.trim();if(!a||!i){s("Enter API key and Base URL first","error");return}B(!0);try{let d=await r("/api/cloud/test-llm",{method:"POST",body:JSON.stringify({api_key:a,base_url:i,model:u})});d.ok?s(`LLM OK! [${d.model}] replied: "${d.reply}"`,"success"):s("LLM check failed: "+(d.error||`HTTP ${d.status}`),"error")}catch(d){s("LLM test failed: "+d.message,"error")}finally{B(!1)}},ee=async()=>{L(!0);try{let a={cloud_gateway_enabled:_,telegram_bot_token:x.trim(),llm_base_url:w.trim(),llm_api_key:k.trim(),llm_model:T.trim()};await r("/api/cloud/gateway-config",{method:"PUT",body:JSON.stringify(a)}),s("Gateway & LLM configuration saved!","success"),await m()}catch(a){s("Save failed: "+a.message,"error")}finally{L(!1)}},te=async()=>{let a=E.trim(),i=G.trim();if(!a||!i){s("Enter both Chat ID and Device ID","error");return}try{await r("/api/cloud/bind",{method:"POST",body:JSON.stringify({chat_id:a,device_id:i})}),O(""),H(""),s(`Bound chat ${a} \u2192 ${i}`,"success"),await m()}catch(u){s("Bind failed: "+u.message,"error")}},ae=async a=>{try{await r("/api/cloud/unbind",{method:"POST",body:JSON.stringify({chat_id:String(a)})}),s(`Unbound chat ${a}`,"success"),await m()}catch(i){s("Unbind failed: "+i.message,"error")}},b=Object.keys(A);return(0,e.jsxs)("div",{className:t.stack,children:[(0,e.jsxs)(v,{children:[(0,e.jsxs)("div",{className:n.styles.cardHead,children:[(0,e.jsxs)("div",{className:n.styles.cardTitle,children:[(0,e.jsx)(l,{name:"server",size:16})," Live Status"]}),(0,e.jsx)(J,{tone:g?"success":"neutral",pulse:g,children:g?"Gateway Active":"Gateway Idle"})]}),(0,e.jsx)("p",{className:n.styles.cardSub,children:"Current runtime state of the gateway background worker and device WebSocket links."}),(0,e.jsxs)("div",{className:t.statusGrid,children:[(0,e.jsxs)("div",{className:t.statusBox,children:[(0,e.jsx)("div",{className:t.statusLabel,children:"Telegram Poller"}),(0,e.jsx)("div",{className:t.statusVal,children:(0,e.jsx)(N,{tone:g?"success":"default",children:g?"Active \xB7 Polling":"Stopped / Disabled"})})]}),(0,e.jsxs)("div",{className:t.statusBox,children:[(0,e.jsx)("div",{className:t.statusLabel,children:"Connected ESP32 Devices"}),(0,e.jsx)("div",{className:t.statusVal,children:(0,e.jsx)(N,{tone:y.length>0?"success":"default",children:y.length>0?`${y.length} online (${y.join(", ")})`:"0 online (device disconnected)"})})]}),(0,e.jsxs)("div",{className:t.statusBox,children:[(0,e.jsx)("div",{className:t.statusLabel,children:"Chat \u21C4 Device Bindings"}),(0,e.jsx)("div",{className:t.statusVal,children:(0,e.jsxs)(N,{tone:b.length>0?"success":"default",children:[b.length," bound"]})})]})]})]}),(0,e.jsxs)(v,{children:[(0,e.jsx)("div",{className:n.styles.cardHead,children:(0,e.jsxs)("div",{className:n.styles.cardTitle,children:[(0,e.jsx)(l,{name:"bot",size:16})," Telegram Bot Gateway"]})}),(0,e.jsx)("p",{className:n.styles.cardSub,children:"The server long-polls Telegram on this token and relays messages over WebSocket to the bound ESP32. The device never creates Telegram HTTPS sockets."}),(0,e.jsxs)("div",{className:t.toggleBox,children:[(0,e.jsxs)("div",{children:[(0,e.jsx)("div",{style:{fontWeight:500,fontSize:14,color:"#f5f5f5"},children:"Enable Cloud Telegram Gateway"}),(0,e.jsx)("div",{className:t.fieldHint,children:"When off, the backend does not poll Telegram."})]}),(0,e.jsx)(M,{options:[{label:"Disabled",value:!1},{label:"Enabled",value:!0}],value:_,onChange:a=>I(!!a)})]}),(0,e.jsxs)("div",{className:t.formGroup,children:[(0,e.jsx)("label",{className:t.fieldLabel,children:"Telegram Bot Token"}),(0,e.jsx)(c.Password,{value:x,onChange:a=>P(a.target.value),placeholder:"123456789:ABCdefGhIJKlmNoPQRstuVWXyz",className:"espet-mono"}),(0,e.jsx)("div",{className:t.fieldHint,children:"From @BotFather. Saved on server \u2014 never transmitted to ESP32 firmware."})]}),(0,e.jsx)("div",{className:t.btnRow,children:(0,e.jsx)(p,{onClick:Y,loading:W,icon:(0,e.jsx)(l,{name:"check",size:14}),children:"Test Bot Token"})})]}),(0,e.jsxs)(v,{children:[(0,e.jsx)("div",{className:n.styles.cardHead,children:(0,e.jsxs)("div",{className:n.styles.cardTitle,children:[(0,e.jsx)(l,{name:"spark",size:16})," Server-Side LLM Relay"]})}),(0,e.jsx)("p",{className:n.styles.cardSub,children:"The ESP32 agent builds chat completion prompts; the backend injects the API key and forwards them to the provider. The device never holds the LLM key."}),(0,e.jsxs)("div",{className:t.formGroup,children:[(0,e.jsx)("label",{className:t.fieldLabel,children:"API Base URL"}),(0,e.jsx)(c,{value:w,onChange:a=>D(a.target.value),placeholder:"https://api.openai.com/v1",className:"espet-mono"}),(0,e.jsx)("div",{className:t.fieldHint,children:"OpenAI-compatible endpoint (OpenAI, DeepSeek, OpenRouter, Ollama, LM Studio, vLLM)."})]}),(0,e.jsxs)("div",{className:t.formGroup,children:[(0,e.jsx)("label",{className:t.fieldLabel,children:"API Key"}),(0,e.jsx)(c.Password,{value:k,onChange:a=>z(a.target.value),placeholder:"sk-\u2026",className:"espet-mono"}),(0,e.jsx)("div",{className:t.fieldHint,children:"Stored safely on this server. Never shared with the microcontroller."})]}),(0,e.jsxs)("div",{className:t.formGroup,children:[(0,e.jsx)("label",{className:t.fieldLabel,children:"Model Override (optional)"}),(0,e.jsx)(c,{value:T,onChange:a=>R(a.target.value),placeholder:"gpt-4o-mini, deepseek-chat, qwen/qwen3.8-omni-flash:free\u2026",className:"espet-mono"}),(0,e.jsx)("div",{className:t.fieldHint,children:"If left empty, the model requested by the device firmware is preserved."})]}),(0,e.jsx)("div",{className:t.btnRow,children:(0,e.jsx)(p,{onClick:Z,loading:F,icon:(0,e.jsx)(l,{name:"spark",size:14}),children:"Test LLM Connection"})})]}),(0,e.jsxs)(v,{children:[(0,e.jsx)("div",{className:n.styles.cardHead,children:(0,e.jsxs)("div",{className:n.styles.cardTitle,children:[(0,e.jsx)(l,{name:"send",size:16})," Telegram Chat \u21C4 Device Bindings"]})}),(0,e.jsx)("p",{className:n.styles.cardSub,children:"Incoming Telegram messages from a chat ID are forwarded only to the matching ESP32 device ID. Auto-binds on the first message if exactly one device is online."}),(0,e.jsxs)("table",{className:t.table,children:[(0,e.jsx)("thead",{children:(0,e.jsxs)("tr",{children:[(0,e.jsx)("th",{children:"Telegram Chat ID"}),(0,e.jsx)("th",{children:"ESP32 Device ID"}),(0,e.jsx)("th",{style:{width:80},children:"Action"})]})}),(0,e.jsx)("tbody",{children:b.length===0?(0,e.jsx)("tr",{children:(0,e.jsx)("td",{colSpan:3,style:{color:"#737373",textAlign:"center",padding:24},children:"No bindings registered yet."})}):b.map(a=>(0,e.jsxs)("tr",{children:[(0,e.jsx)("td",{className:"espet-mono",children:a}),(0,e.jsx)("td",{className:"espet-mono",children:A[a]}),(0,e.jsx)("td",{children:(0,e.jsx)("button",{className:t.unbindBtn,onClick:()=>ae(a),children:"Unbind"})})]},a))})]}),(0,e.jsxs)("div",{className:t.bindForm,children:[(0,e.jsx)(c,{value:E,onChange:a=>O(a.target.value),placeholder:"Chat ID (e.g. 123456789)",className:"espet-mono"}),(0,e.jsx)(c,{value:G,onChange:a=>H(a.target.value),placeholder:"Device ID (e.g. espet-device)",className:"espet-mono"}),(0,e.jsx)(p,{onClick:te,icon:(0,e.jsx)(l,{name:"plus",size:14}),children:"Bind"})]})]}),(0,e.jsxs)("div",{className:t.btnRow,style:{marginTop:10},children:[(0,e.jsx)(p,{onClick:m,icon:(0,e.jsx)(l,{name:"refresh",size:14}),children:"Reload"}),(0,e.jsx)(p,{type:"primary",onClick:ee,loading:V,icon:(0,e.jsx)(l,{name:"save",size:14}),style:{paddingInline:26},children:"Save Configuration & Restart Gateway"})]}),(0,e.jsxs)("div",{className:n.styles.sectionNote,children:[(0,e.jsx)(l,{name:"info",size:16}),(0,e.jsx)("span",{children:"Hybrid Mode keeps secrets and heavy network polling on your local server, while the ESP32 focuses on real-time voice, hardware control, and local sensors over the lightweight LAN WebSocket protocol."})]})]})}export{ue as CloudView};
