import{F as l,I as i}from"./chunk-HLESZX2U.js";import{d}from"./chunk-NM6J3ND7.js";var o=d(l(),1),g=i(({css:e,cx:r})=>({glass:e`
    background: rgba(255, 255, 255, 0.055);
    backdrop-filter: blur(14px) saturate(1.1);
    -webkit-backdrop-filter: blur(14px) saturate(1.1);
    border: 1px solid rgba(255, 255, 255, 0.09);
    border-radius: 16px;
  `,glassSoft:e`
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
  `,hoverable:e`
    transition: transform 0.18s ease, border-color 0.18s ease,
      background 0.18s ease;
    &:hover {
      transform: translateY(-2px);
      border-color: rgba(255, 255, 255, 0.28);
    }
  `,merge:(...a)=>r(...a.filter(Boolean)),card:e`
    padding: 20px;
    background: rgba(255, 255, 255, 0.055);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.09);
    border-radius: 16px;
  `}));function f({className:e,hover:r,...a}){let{styles:t,cx:n}=g();return(0,o.jsx)("div",{className:n(t.card,r&&t.hoverable,e),...a})}var b=i(({css:e})=>({base:e`
    display: inline-flex;
    align-items: center;
    gap: 5px;
    padding: 2px 10px;
    border-radius: 999px;
    font-size: 12px;
    font-weight: 500;
    line-height: 1.7;
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #d4d4d4;
    background: rgba(255, 255, 255, 0.05);
    white-space: nowrap;
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
  `}));function m({tone:e="default",className:r,children:a,...t}){let{styles:n,cx:p}=b(),s=e==="default"?"":n[e];return(0,o.jsx)("span",{className:p(n.base,s,r),...t,children:a})}var u=i(({css:e})=>({stack:e`
    display: flex;
    flex-direction: column;
    gap: 18px;
  `,grid:e`
    display: grid;
    gap: 16px;
  `,cols2:e`
    grid-template-columns: repeat(2, 1fr);
    @media (max-width: 720px) {
      grid-template-columns: 1fr;
    }
  `,cols3:e`
    grid-template-columns: repeat(3, 1fr);
    @media (max-width: 860px) {
      grid-template-columns: 1fr;
    }
  `,cardHead:e`
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  `,cardTitle:e`
    display: flex;
    align-items: center;
    gap: 9px;
    font-size: 1.02rem;
    font-weight: 600;
    color: #f5f5f5;
  `,cardSub:e`
    color: #a3a3a3;
    font-size: 0.86rem;
    margin: -6px 0 14px;
  `,sectionNote:e`
    display: flex;
    gap: 8px;
    align-items: flex-start;
    color: #a3a3a3;
    font-size: 0.85rem;
    padding: 12px 14px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
  `,empty:e`
    display: grid;
    place-items: center;
    gap: 10px;
    padding: 40px 20px;
    color: #737373;
    text-align: center;
  `,rowItem:e`
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 14px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
  `,grow:e`
    flex: 1 1 auto;
    min-width: 0;
  `,name:e`
    font-weight: 500;
    color: #e5e5e5;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  `,meta:e`
    font-size: 0.8rem;
    color: #737373;
    margin-top: 2px;
  `})),c=i(({css:e})=>({table:e`
    display: flex;
    flex-direction: column;
    gap: 2px;
  `,row:e`
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
    padding: 7px 2px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.045);
    &:last-child {
      border-bottom: none;
    }
  `,k:e`
    color: #a3a3a3;
    font-size: 0.86rem;
  `,v:e`
    color: #e5e5e5;
    font-family: var(--espet-font-mono);
    font-size: 0.85rem;
    text-align: right;
  `}));function h({rows:e}){let{styles:r}=c();return(0,o.jsx)("div",{className:r.table,children:e.map((a,t)=>(0,o.jsxs)("div",{className:r.row,children:[(0,o.jsx)("span",{className:r.k,children:a.k}),(0,o.jsx)("span",{className:r.v,children:a.v})]},t))})}export{f as a,m as b,u as c,h as d};
