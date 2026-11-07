/* Exact-byte markdown source field + a live lobe-UI Markdown preview.
   THE unified markdown editor for the whole dashboard (DESIGN.MD §6): every
   .md input uses these "Edit | Preview" tabs — raw JetBrains Mono bytes in
   Edit, default-font rendered markdown in Preview. Used where content must
   round-trip verbatim to the device (the parser needs `# Title` as the
   literal first line and an 8 KB byte cap) — a WYSIWYG would reformat it. */

import { Markdown } from "@lobehub/ui";
import { createStyles } from "antd-style";
import { Tabs } from "antd";
import type { ReactNode } from "react";

/** Kept for view-side dirty comparisons that tolerate whitespace drift; the
 *  raw textarea itself round-trips exactly. */
export function canonical(s: string): string {
  return s.replace(/[ \t]+\n/g, "\n").replace(/\s+$/, "").replace(/^\n+/, "");
}

const useS = createStyles(({ css }) => ({
  label: css`
    font-size: 12px;
    color: #a3a3a3;
    margin-bottom: 4px;
  `,
  area: css`
    width: 100%;
    font-family: var(--espet-font-mono);
    font-size: 13px;
    line-height: 1.6;
    color: #e5e5e5;
    background: rgba(8,8,9, 0.6);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 12px;
    padding: 14px 16px;
    resize: vertical;
    min-height: 260px;
    outline: none;
    transition: border-color 0.15s ease;
    &:focus {
      border-color: rgba(255, 255, 255, 0.4);
    }
    &::placeholder {
      color: #525252;
    }
  `,
  preview: css`
    min-height: 260px;
    max-height: 60vh;
    overflow-y: auto;
    padding: 6px 4px;
  `,
  meta: css`
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    margin-top: 8px;
    font-size: 12px;
    color: #737373;
  `,
  over: css`
    color: #fafafa;
    font-weight: 600;
  `,
}));

export function SourceField({
  value,
  onChange,
  placeholder,
  rows = 14,
  byteLimit,
  hint,
  showPreview = true,
  label = "Content (markdown)",
}: {
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  rows?: number;
  /** UTF-8 byte cap; shows a live counter and turns red when exceeded. */
  byteLimit?: number;
  hint?: ReactNode;
  showPreview?: boolean;
  /** small caption above the tabs; pass "" to hide */
  label?: ReactNode;
}) {
  const { styles, cx } = useS();
  const bytes = new Blob([value]).size;
  const over = byteLimit != null && bytes > byteLimit;

  const editor = (
    <>
      <textarea
        className={styles.area}
        value={value}
        rows={rows}
        spellCheck={false}
        placeholder={placeholder}
        onChange={(e) => onChange(e.target.value)}
      />
      <div className={styles.meta}>
        <span>{hint}</span>
        {byteLimit != null && (
          <span className={cx("espet-mono", over && styles.over)}>
            {bytes} / {byteLimit} bytes
          </span>
        )}
      </div>
    </>
  );

  const field = (
    <>
      {label && <div className={styles.label}>{label}</div>}
      {showPreview ? (
        <Tabs
          size="small"
          defaultActiveKey="edit"
          items={[
            { key: "edit", label: "Edit", children: editor },
            {
              key: "preview",
              label: "Preview",
              children: (
                <div className={`${styles.preview}`}>
                  {value.trim() ? <Markdown>{value}</Markdown> : <span style={{ color: "#737373" }}>Nothing to preview yet.</span>}
                </div>
              ),
            },
          ]}
        />
      ) : (
        editor
      )}
    </>
  );

  return field;
}
