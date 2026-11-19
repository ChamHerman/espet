import{a as z,c as A}from"./chunk-M6Q6TWUZ.js";import{a as x}from"./chunk-74S6OUS6.js";import{C as P,D as i,F as I,G as k,J as F,R as m,S as d,U as E,V as L,Z as O,a as K,m as v,r as T,t as S}from"./chunk-B5OUD7EE.js";import{d as b}from"./chunk-NM6J3ND7.js";var s=b(K(),1);var e=b(k(),1),{Text:Q}=I,X=[2,5,8,12],Y=F(({css:a})=>({head:a`
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    margin-bottom: 14px;
  `,title:a`
    display: inline-flex;
    align-items: center;
    gap: 8px;
    font-weight: 600;
    font-size: 15px;
  `,frame:a`
    position: relative;
    border-radius: 16px;
    overflow: hidden;
    background: #000;
    border: 1px solid rgba(255, 255, 255, 0.12);
    aspect-ratio: 16 / 10;
    display: grid;
    place-items: center;
  `,img:a`
    width: 100%;
    height: 100%;
    object-fit: contain;
    display: block;
  `,off:a`
    display: grid;
    place-items: center;
    gap: 12px;
    padding: 30px;
    text-align: center;
    color: #737373;
    height: 100%;
  `,liveTag:a`
    position: absolute;
    top: 12px;
    left: 14px;
    z-index: 2;
  `,stopIcon:a`
    transform: rotate(180deg);
  `,controls:a`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-wrap: wrap;
    margin-top: 16px;
  `,telemetry:a`
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    margin-top: 12px;
  `,ownerPanel:a`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    flex-wrap: wrap;
    margin-top: 14px;
    padding: 12px;
    border-radius: 14px;
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.09);
  `,ownerAlert:a`
    color: #8fd9ae;
    border: 1px solid rgba(143, 217, 174, 0.28);
    background: rgba(143, 217, 174, 0.1);
    border-radius: 999px;
    padding: 6px 10px;
    font-size: 13px;
  `,meta:a`
    margin-top: 10px;
    font-size: 12px;
    color: #737373;
  `}));function Z(a){return a?.latest_device_id||a?.devices?.[0]?.device_id||"espet_s3_01"}function ce(){let{styles:a}=Y(),y=A(),{toast:$}=O(),[n,D]=(0,s.useState)("espet_s3_01"),[t,_]=(0,s.useState)(!1),[c,M]=(0,s.useState)(8),[f,V]=(0,s.useState)("processed"),[r,B]=(0,s.useState)(null),[h,R]=(0,s.useState)(null),[l,g]=(0,s.useState)(null),[N,C]=(0,s.useState)(0);(0,s.useEffect)(()=>{d.devices().then(o=>D(Z(o))).catch(()=>{})},[]),(0,s.useEffect)(()=>{d.ownerFaceStatus(n).then(R).catch(()=>{})},[n]),(0,s.useEffect)(()=>{let o=!1,p=()=>{d.cameraStatus(n).then(u=>{if(o)return;B(u),t&&u.has_frame&&u.last_frame_age_ms>5e3&&(g("Camera stream stalled; reconnecting..."),d.startCameraStream({device_id:n,fps:c}).catch(()=>{}),C(H=>H+1))}).catch(u=>!o&&g(u.message))};p();let q=window.setInterval(p,t?1e3:3e3);return()=>{o=!0,window.clearInterval(q)}},[n,c,t]);let j=t?`${E(n,f,c)}&r=${N}`:"",G=r?.detections?.length||0,w=t&&!!r?.has_frame&&(r?.last_frame_age_ms??0)>5e3,U=r?.owner_last_seen_ts?new Date(r.owner_last_seen_ts*1e3).toLocaleString():"Never",W=async()=>{if(g(null),t){_(!1),await d.stopCameraStream(n).catch(()=>{});return}(await d.startCameraStream({device_id:n,fps:c})).pushed||$("No ESP32 camera device online","error"),C(p=>p+1),_(!0)},J=async o=>{let p=o?"processed":"raw";V(p),await d.setCameraConfig({device_id:n,mode:p}).catch(()=>{})};return(0,e.jsxs)("div",{className:y.styles.stack,children:[(0,e.jsxs)(z,{children:[(0,e.jsxs)("div",{className:a.head,children:[(0,e.jsxs)("span",{className:a.title,children:[(0,e.jsx)(m,{name:"camera",size:16})," ESP32-S3 Camera"]}),(0,e.jsx)(x,{tone:t&&!l&&!w?"success":l||w?"danger":"neutral",pulse:t&&!l&&!w,children:w?"Stalled":t&&!l?"Live":l?"Error":"Paused"})]}),(0,e.jsxs)("div",{className:a.frame,children:[t?(0,e.jsx)("img",{className:a.img,src:j,alt:"ESP32 camera live stream",onError:()=>g("Camera stream unavailable"),onLoad:()=>g(null)},`${n}-${f}-${c}-${N}-${Number(t)}`):(0,e.jsxs)("div",{className:a.off,children:[(0,e.jsx)(m,{name:"camera",size:40}),(0,e.jsxs)("div",{children:["Press ",(0,e.jsx)("b",{children:"Go Live"})," to start the backend MJPEG camera stream."]}),(0,e.jsx)(Q,{style:{fontSize:12,color:"#737373",maxWidth:560},children:"The backend sends a stream-start control frame to the ESP32 only while this view is open."})]}),t&&!l&&(0,e.jsxs)(i,{className:a.liveTag,bordered:!1,style:{background:"rgba(143, 217, 174, 0.12)",color:"#8fd9ae",border:"1px solid rgba(143, 217, 174, 0.3)"},children:["LIVE \xB7 ",c," FPS \xB7 ",f==="raw"?"RAW":"CV"]})]}),(0,e.jsxs)("div",{className:a.controls,children:[(0,e.jsxs)(S,{wrap:!0,children:[(0,e.jsx)(T,{options:X.map(o=>`${o} FPS`),value:`${c} FPS`,onChange:o=>M(Number(String(o).split(" ")[0]))}),(0,e.jsxs)("span",{style:{display:"inline-flex",alignItems:"center",gap:8},children:[(0,e.jsx)(P,{checked:f==="processed",onChange:J}),(0,e.jsx)("span",{children:"AI Overlay"})]})]}),(0,e.jsxs)(S,{wrap:!0,children:[(0,e.jsx)(v,{href:L(n,f),target:"_blank",icon:(0,e.jsx)(m,{name:"refresh",size:14}),children:"Snapshot"}),(0,e.jsx)(v,{type:"primary",onClick:W,icon:(0,e.jsx)(m,{name:t?"send":"camera",size:14,className:t?a.stopIcon:void 0}),children:t?"Stop":"Go Live"})]})]}),(0,e.jsxs)("div",{className:a.ownerPanel,children:[(0,e.jsxs)(S,{wrap:!0,children:[(0,e.jsx)(x,{tone:r?.owner_detected?"success":h?.enrolled?"neutral":"warning",pulse:!!r?.owner_detected,children:r?.owner_detected?"Owner Detected":h?.enrolled?"Owner Enrolled":"Owner Not Set"}),r?.owner_detected&&(0,e.jsxs)("span",{className:a.ownerAlert,children:["Owner Detected \xB7 ",Math.round((r.owner_similarity||0)*100),"%"]}),(0,e.jsxs)(i,{bordered:!1,children:["Last Seen ",U]}),h?.error&&(0,e.jsx)(i,{bordered:!1,style:{color:"#eda7a7"},children:"Face AI Not Installed"})]}),(0,e.jsx)(v,{size:"small",onClick:()=>{location.hash="#overview"},children:"Owner Settings"})]}),(0,e.jsxs)("div",{className:a.telemetry,children:[(0,e.jsxs)(i,{bordered:!1,children:["Device ",(0,e.jsx)("span",{className:"espet-mono",children:n})]}),(0,e.jsxs)(i,{bordered:!1,children:["FPS ",r?.fps??0]}),(0,e.jsxs)(i,{bordered:!1,children:["Inference ",r?.inference_ms??0," ms"]}),(0,e.jsxs)(i,{bordered:!1,children:["Detections ",G]}),(0,e.jsxs)(i,{bordered:!1,children:["Viewers ",r?.viewer_count??0]}),(0,e.jsxs)(i,{bordered:!1,children:["Frame Age ",Math.round(r?.last_frame_age_ms??0)," ms"]}),(0,e.jsxs)(i,{bordered:!1,children:["Owner Similarity ",r?.owner_similarity??0]})]}),l&&(0,e.jsx)("div",{className:a.meta,children:l})]}),(0,e.jsxs)("div",{className:y.styles.sectionNote,children:[(0,e.jsx)(m,{name:"info",size:16}),(0,e.jsx)("span",{children:"Video frames stay on the LAN: ESP32 \u2192 FastAPI backend \u2192 browser. Streaming stops automatically when viewers disconnect."})]})]})}export{ce as CameraView};
