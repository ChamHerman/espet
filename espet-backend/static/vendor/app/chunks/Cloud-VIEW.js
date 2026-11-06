import{a as I,b as M,c as L}from"./chunk-OWFZGM2X.js";import{a as A}from"./chunk-OCCKVN4E.js";import{C as b,F as S,I as T,Q as l,V as k,a as G,m as B,r as y,t as g,w as x}from"./chunk-HLESZX2U.js";import{d as u}from"./chunk-NM6J3ND7.js";var a=u(G(),1);var e=u(S(),1);var O=T(({css:t})=>({stack:t`
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
    &:last-child { margin-bottom: 0; }
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
    th, td {
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
  `}));var API_KEY="espet_secret_device_key_2026";async function fetchJSON(url,opts={}){let res=await fetch(url,{...opts,headers:{"Content-Type":"application/json",Authorization:`Bearer ${API_KEY}`,...(opts.headers||{})}});if(!res.ok){let err=res.statusText;try{let b=await res.json();err=b.detail||b.error||err}catch{}throw new Error(err)}return res.json()}function CloudView(){let{styles:s}=O(),layout=L(),{toast:t}=k(),[loading,setLoading]=(0,a.useState)(!0),[saving,setSaving]=(0,a.useState)(!1),[testingTg,setTestingTg]=(0,a.useState)(!1),[testingLlm,setTestingLlm]=(0,a.useState)(!1),[tgEnabled,setTgEnabled]=(0,a.useState)(!1),[tgToken,setTgToken]=(0,a.useState)(""),[llmUrl,setLlmUrl]=(0,a.useState)("https://api.openai.com/v1"),[llmKey,setLlmKey]=(0,a.useState)(""),[llmModel,setLlmModel]=(0,a.useState)(""),[pollerRunning,setPollerRunning]=(0,a.useState)(!1),[devices,setDevices]=(0,a.useState)([]),[bindings,setBindings]=(0,a.useState)({}),[newChatId,setNewChatId]=(0,a.useState)(""),[newDeviceId,setNewDeviceId]=(0,a.useState)("");let loadData=(0,a.useCallback)(async()=>{try{let cfg=await fetchJSON("/api/cloud/gateway-config");setTgEnabled(!!cfg.cloud_gateway_enabled);setTgToken(cfg.telegram_bot_token||"");setLlmUrl(cfg.llm_base_url||"https://api.openai.com/v1");setLlmKey(cfg.llm_api_key||"");setLlmModel(cfg.llm_model||"");setPollerRunning(!!cfg.poller_running);let devRes=await fetchJSON("/api/cloud/devices");setDevices(devRes.connected||[]);let bindRes=await fetchJSON("/api/cloud/bindings");setBindings(bindRes.bindings||{})}catch(err){t("Failed to load cloud gateway: "+err.message,"error")}finally{setLoading(!1)}},[t]);(0,a.useEffect)(()=>{loadData()},[loadData]);let onTestTg=async()=>{let token=tgToken.trim();if(!token){t("Enter a bot token first","error");return}setTestingTg(!0);try{let res=await fetchJSON("/api/cloud/test-telegram",{method:"POST",body:JSON.stringify({token})});if(res.ok&&res.bot){t(`Token valid! Bot: @${res.bot.username} (${res.bot.first_name})`,"success")}else{t("Telegram check failed: "+(res.error||"unknown error"),"error")}}catch(err){t("Telegram test failed: "+err.message,"error")}finally{setTestingTg(!1)}};let onTestLlm=async()=>{let key=llmKey.trim(),url=llmUrl.trim(),model=llmModel.trim();if(!key||!url){t("Enter API key and Base URL first","error");return}setTestingLlm(!0);try{let res=await fetchJSON("/api/cloud/test-llm",{method:"POST",body:JSON.stringify({api_key:key,base_url:url,model})});if(res.ok){t(`LLM OK! [${res.model}] replied: "${res.reply}"`,"success")}else{t("LLM check failed: "+(res.error||`HTTP ${res.status}`),"error")}}catch(err){t("LLM test failed: "+err.message,"error")}finally{setTestingLlm(!1)}};let onSaveConfig=async()=>{setSaving(!0);try{let payload={cloud_gateway_enabled:tgEnabled,telegram_bot_token:tgToken.trim(),llm_base_url:llmUrl.trim(),llm_api_key:llmKey.trim(),llm_model:llmModel.trim()};await fetchJSON("/api/cloud/gateway-config",{method:"PUT",body:JSON.stringify(payload)});t("Gateway & LLM configuration saved!","success");await loadData()}catch(err){t("Save failed: "+err.message,"error")}finally{setSaving(!1)}};let onAddBind=async()=>{let cid=newChatId.trim(),did=newDeviceId.trim();if(!cid||!did){t("Enter both Chat ID and Device ID","error");return}try{await fetchJSON("/api/cloud/bind",{method:"POST",body:JSON.stringify({chat_id:cid,device_id:did})});setNewChatId("");setNewDeviceId("");t(`Bound chat ${cid} \u2192 ${did}`,"success");await loadData()}catch(err){t("Bind failed: "+err.message,"error")}};let onUnbind=async(cid)=>{try{await fetchJSON("/api/cloud/unbind",{method:"POST",body:JSON.stringify({chat_id:String(cid)})});t(`Unbound chat ${cid}`,"success");await loadData()}catch(err){t("Unbind failed: "+err.message,"error")}};let bindingKeys=Object.keys(bindings);return(0,e.jsxs)("div",{className:s.stack,children:[(0,e.jsxs)(I,{children:[(0,e.jsxs)("div",{className:layout.styles.cardHead,children:[(0,e.jsxs)("div",{className:layout.styles.cardTitle,children:[(0,e.jsx)(l,{name:"server",size:16})," Live Status"]}),(0,e.jsx)(A,{tone:pollerRunning?"success":"neutral",pulse:pollerRunning,children:pollerRunning?"Gateway Active":"Gateway Idle"})]}),(0,e.jsx)("p",{className:layout.styles.cardSub,children:"Current runtime state of the gateway background worker and device WebSocket links."}),(0,e.jsxs)("div",{className:s.statusGrid,children:[(0,e.jsxs)("div",{className:s.statusBox,children:[(0,e.jsx)("div",{className:s.statusLabel,children:"Telegram Poller"}),(0,e.jsx)("div",{className:s.statusVal,children:(0,e.jsx)(M,{tone:pollerRunning?"success":"default",children:pollerRunning?"Active \xB7 Polling":"Stopped / Disabled"})})]}),(0,e.jsxs)("div",{className:s.statusBox,children:[(0,e.jsx)("div",{className:s.statusLabel,children:"Connected ESP32 Devices"}),(0,e.jsx)("div",{className:s.statusVal,children:(0,e.jsx)(M,{tone:devices.length>0?"success":"default",children:devices.length>0?`${devices.length} online (${devices.join(", ")})`:"0 online (device disconnected)"})})]}),(0,e.jsxs)("div",{className:s.statusBox,children:[(0,e.jsx)("div",{className:s.statusLabel,children:"Chat \u21C4 Device Bindings"}),(0,e.jsx)("div",{className:s.statusVal,children:(0,e.jsx)(M,{tone:bindingKeys.length>0?"success":"default",children:`${bindingKeys.length} bound`})})]})]})]}),(0,e.jsxs)(I,{children:[(0,e.jsxs)("div",{className:layout.styles.cardHead,children:(0,e.jsxs)("div",{className:layout.styles.cardTitle,children:[(0,e.jsx)(l,{name:"bot",size:16})," Telegram Bot Gateway"]})}),(0,e.jsx)("p",{className:layout.styles.cardSub,children:"The server long-polls Telegram on this token and relays messages over WebSocket to the bound ESP32. The device never creates Telegram HTTPS sockets."}),(0,e.jsxs)("div",{className:s.toggleBox,children:[(0,e.jsxs)("div",{children:[(0,e.jsx)("div",{style:{fontWeight:500,fontSize:14,color:"#f5f5f5"},children:"Enable Cloud Telegram Gateway"}),(0,e.jsx)("div",{className:s.fieldHint,children:"When off, the backend does not poll Telegram."})]}),(0,e.jsx)(y,{options:[{label:"Disabled",value:!1},{label:"Enabled",value:!0}],value:tgEnabled,onChange:v=>setTgEnabled(!!v)})]}),(0,e.jsxs)("div",{className:s.formGroup,children:[(0,e.jsx)("label",{className:s.fieldLabel,children:"Telegram Bot Token"}),(0,e.jsx)(x,{type:"password",value:tgToken,onChange:r=>setTgToken(r.target.value),placeholder:"123456789:ABCdefGhIJKlmNoPQRstuVWXyz",className:"espet-mono"}),(0,e.jsx)("div",{className:s.fieldHint,children:"From @BotFather. Saved on server \u2014 never transmitted to ESP32 firmware."})]}),(0,e.jsx)("div",{className:s.btnRow,children:(0,e.jsx)(B,{onClick:onTestTg,loading:testingTg,icon:(0,e.jsx)(l,{name:"check",size:14}),children:"Test Bot Token"})})]}),(0,e.jsxs)(I,{children:[(0,e.jsxs)("div",{className:layout.styles.cardHead,children:(0,e.jsxs)("div",{className:layout.styles.cardTitle,children:[(0,e.jsx)(l,{name:"spark",size:16})," Server-Side LLM Relay"]})}),(0,e.jsx)("p",{className:layout.styles.cardSub,children:"The ESP32 agent builds chat completion prompts; the backend injects the API key and forwards them to the provider. The device never holds the LLM key."}),(0,e.jsxs)("div",{className:s.formGroup,children:[(0,e.jsx)("label",{className:s.fieldLabel,children:"API Base URL"}),(0,e.jsx)(x,{value:llmUrl,onChange:r=>setLlmUrl(r.target.value),placeholder:"https://api.openai.com/v1",className:"espet-mono"}),(0,e.jsx)("div",{className:s.fieldHint,children:"OpenAI-compatible endpoint (OpenAI, DeepSeek, OpenRouter, Ollama, LM Studio, vLLM)."})]}),(0,e.jsxs)("div",{className:s.formGroup,children:[(0,e.jsx)("label",{className:s.fieldLabel,children:"API Key"}),(0,e.jsx)(x,{type:"password",value:llmKey,onChange:r=>setLlmKey(r.target.value),placeholder:"sk-\u2026",className:"espet-mono"}),(0,e.jsx)("div",{className:s.fieldHint,children:"Stored safely on this server. Never shared with the microcontroller."})]}),(0,e.jsxs)("div",{className:s.formGroup,children:[(0,e.jsx)("label",{className:s.fieldLabel,children:"Model Override (optional)"}),(0,e.jsx)(x,{value:llmModel,onChange:r=>setLlmModel(r.target.value),placeholder:"gpt-4o-mini, deepseek-chat, qwen/qwen3.8-omni-flash:free\u2026",className:"espet-mono"}),(0,e.jsx)("div",{className:s.fieldHint,children:"If left empty, the model requested by the device firmware is preserved."})]}),(0,e.jsx)("div",{className:s.btnRow,children:(0,e.jsx)(B,{onClick:onTestLlm,loading:testingLlm,icon:(0,e.jsx)(l,{name:"spark",size:14}),children:"Test LLM Connection"})})]}),(0,e.jsxs)(I,{children:[(0,e.jsxs)("div",{className:layout.styles.cardHead,children:(0,e.jsxs)("div",{className:layout.styles.cardTitle,children:[(0,e.jsx)(l,{name:"send",size:16})," Telegram Chat \u21C4 Device Bindings"]})}),(0,e.jsx)("p",{className:layout.styles.cardSub,children:"Incoming Telegram messages from a chat ID are forwarded only to the matching ESP32 device ID. Auto-binds on the first message if exactly one device is online."}),(0,e.jsxs)("table",{className:s.table,children:[(0,e.jsx)("thead",{children:(0,e.jsxs)("tr",{children:[(0,e.jsx)("th",{children:"Telegram Chat ID"}),(0,e.jsx)("th",{children:"ESP32 Device ID"}),(0,e.jsx)("th",{style:{width:80},children:"Action"})]})}),(0,e.jsx)("tbody",{children:bindingKeys.length===0?(0,e.jsx)("tr",{children:(0,e.jsx)("td",{colSpan:3,style:{color:"#737373",textAlign:"center",padding:24},children:"No bindings registered yet."})}):bindingKeys.map(k=>(0,e.jsxs)("tr",{children:[(0,e.jsx)("td",{className:"espet-mono",children:k}),(0,e.jsx)("td",{className:"espet-mono",children:bindings[k]}),(0,e.jsx)("td",{children:(0,e.jsx)("button",{className:s.unbindBtn,onClick:()=>onUnbind(k),children:"Unbind"})})]},k))})]}),(0,e.jsxs)("div",{className:s.bindForm,children:[(0,e.jsx)(x,{value:newChatId,onChange:r=>setNewChatId(r.target.value),placeholder:"Chat ID (e.g. 123456789)",className:"espet-mono"}),(0,e.jsx)(x,{value:newDeviceId,onChange:r=>setNewDeviceId(r.target.value),placeholder:"Device ID (e.g. mimi-espet)",className:"espet-mono"}),(0,e.jsx)(B,{onClick:onAddBind,icon:(0,e.jsx)(l,{name:"plus",size:14}),children:"Bind"})]})]}),(0,e.jsxs)("div",{className:s.btnRow,style:{marginTop:10},children:[(0,e.jsx)(B,{onClick:loadData,icon:(0,e.jsx)(l,{name:"refresh",size:14}),children:"Reload"}),(0,e.jsx)(B,{type:"primary",onClick:onSaveConfig,loading:saving,icon:(0,e.jsx)(l,{name:"save",size:14}),style:{paddingInline:26},children:"Save Configuration & Restart Gateway"})]}),(0,e.jsxs)("div",{className:layout.styles.sectionNote,children:[(0,e.jsx)(l,{name:"info",size:16}),(0,e.jsx)("span",{children:"Hybrid Mode keeps secrets and heavy network polling on your local server, while the ESP32 focuses on real-time voice, hardware control, and local sensors over the lightweight LAN WebSocket protocol."})]})]})}export{CloudView};
