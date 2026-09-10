import{a as I,c as L}from"./chunk-OWFZGM2X.js";import{a as A}from"./chunk-OCCKVN4E.js";import{C as b,E as N,F as S,I as T,Q as l,S as C,T as E,V as k,a as G,m,r as y,t as g,w as x}from"./chunk-HLESZX2U.js";import{d as u}from"./chunk-NM6J3ND7.js";var a=u(G(),1);var e=u(S(),1),{Text:$}=N,v=18789,z={"2 fps":500,"1 fps":1e3,"1 / 2 s":2e3,"1 / 5 s":5e3},O=T(({css:t})=>({head:t`
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    margin-bottom: 14px;
  `,title:t`
    display: inline-flex;
    align-items: center;
    gap: 8px;
    font-weight: 600;
    font-size: 15px;
  `,frame:t`
    position: relative;
    border-radius: 16px;
    overflow: hidden;
    background: #000;
    border: 1px solid rgba(255, 255, 255, 0.12);
    aspect-ratio: 16 / 10;
    display: grid;
    place-items: center;
  `,img:t`
    width: 100%;
    height: 100%;
    object-fit: contain;
    display: block;
  `,off:t`
    display: grid;
    place-items: center;
    gap: 12px;
    padding: 30px;
    text-align: center;
    color: #737373;
    height: 100%;
  `,scan:t`
    position: absolute;
    top: 12px;
    left: 14px;
    z-index: 2;
  `,stopIcon:t`
    transform: rotate(180deg);
  `,controls:t`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    flex-wrap: wrap;
    margin-top: 16px;
  `,meta:t`
    margin-top: 10px;
    font-size: 12px;
    color: #737373;
  `}));function J(){let{styles:t}=O(),h=L(),{toast:P}=k(),[s,D]=(0,a.useState)(C()),[n,R]=(0,a.useState)(!1),[p,j]=(0,a.useState)("1 fps"),[c,B]=(0,a.useState)(null),[o,w]=(0,a.useState)(null),i=(0,a.useRef)(null),d=(0,a.useCallback)(()=>{let r=`http://${s}:${v}/camera.jpg?t=${Date.now()}`;w(null);let f=new Image;f.onload=()=>B({url:r,at:Date.now()}),f.onerror=()=>w(`No response from ${s}:${v} \u2014 is the device on this network, and does its firmware serve GET /camera.jpg?`),f.src=r},[s]);(0,a.useEffect)(()=>{if(!n){i.current&&window.clearInterval(i.current),i.current=null;return}return d(),i.current=window.setInterval(d,z[p]),()=>{i.current&&window.clearInterval(i.current),i.current=null}},[n,p,d]);let V=()=>{E(s),P("Device address saved \u2014 shared with Chat")};return(0,e.jsxs)("div",{className:h.styles.stack,children:[(0,e.jsxs)(I,{children:[(0,e.jsxs)("div",{className:t.head,children:[(0,e.jsxs)("span",{className:t.title,children:[(0,e.jsx)(l,{name:"camera",size:16})," ESP32-S3 camera"]}),(0,e.jsx)(A,{tone:n?o?"danger":"success":"neutral",pulse:n&&!!o,children:n?o?"Unreachable":"Live":"Paused"})]}),(0,e.jsxs)("div",{className:t.frame,children:[c&&!o?(0,e.jsx)("img",{className:t.img,src:c.url,alt:"ESP32 camera view"}):(0,e.jsxs)("div",{className:t.off,children:[(0,e.jsx)(l,{name:"camera",size:40}),(0,e.jsx)("div",{children:o??(0,e.jsxs)(e.Fragment,{children:["Press ",(0,e.jsx)("b",{children:"Go live"})," to start polling"," ",(0,e.jsxs)("span",{className:"espet-mono",children:["http://",s,":",v,"/camera.jpg"]})]})}),o&&(0,e.jsxs)($,{style:{fontSize:12,color:"#737373",maxWidth:560},children:["The camera endpoint ships with the firmware update described in"," ",(0,e.jsx)("span",{className:"espet-mono",children:".agent/NEXT_PLAN_BACKEND.MD \xA76"})," ","(a ",(0,e.jsx)("span",{className:"espet-mono",children:"GET /camera.jpg"})," handler on the device's existing HTTP server). Until then this view stays offline."]})]}),c&&!o&&n&&(0,e.jsxs)(b,{className:t.scan,bordered:!1,style:{background:"rgba(143, 217, 174, 0.12)",color:"#8fd9ae",border:"1px solid rgba(143, 217, 174, 0.3)"},children:["LIVE \xB7 ",p]})]}),(0,e.jsxs)("div",{className:t.controls,children:[(0,e.jsxs)(g.Compact,{style:{width:320,maxWidth:"100%"},children:[(0,e.jsx)(x,{value:s,onChange:r=>D(r.target.value),placeholder:"espet.local or 192.168.x.x",addonBefore:"http://",className:"espet-mono"}),(0,e.jsx)(m,{onClick:V,icon:(0,e.jsx)(l,{name:"save",size:14}),children:"Save"})]}),(0,e.jsxs)(g,{wrap:!0,children:[(0,e.jsx)(y,{options:Object.keys(z),value:p,onChange:r=>j(r)}),(0,e.jsx)(m,{onClick:d,icon:(0,e.jsx)(l,{name:"refresh",size:14}),children:"Snapshot"}),(0,e.jsx)(m,{type:"primary",onClick:()=>R(r=>!r),icon:(0,e.jsx)(l,{name:n?"send":"camera",size:14,className:n?t.stopIcon:void 0}),children:n?"Stop":"Go live"})]})]}),c&&(0,e.jsxs)("div",{className:t.meta,children:["Last frame: ",new Date(c.at).toLocaleTimeString()," \xB7"," ",(0,e.jsx)("span",{className:"espet-mono",children:s})]})]}),(0,e.jsxs)("div",{className:h.styles.sectionNote,children:[(0,e.jsx)(l,{name:"info",size:16}),(0,e.jsx)("span",{children:"Snapshots are fetched directly from the device over your LAN \u2014 the backend is not involved, and nothing leaves your network. The frame rate is intentionally low: the OV2640 + ESP32-S3 serve one JPEG per request."})]})]})}export{J as CameraView};
