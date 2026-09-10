/* Theme + provider stack.

   NOTE: we use antd-style's ThemeProvider directly, NOT @lobehub/ui's. lobe-ui
   5.40.3's ThemeProvider crashes in a useMemo ("u is not a function") when
   driven outside its expected wrapper. antd-style's ThemeProvider is what
   createStyles / useTheme actually require and works cleanly with
   darkAlgorithm; lobe UI components (Avatar, Markdown, ChatInput, charts)
   consume the same antd-style theme context, so they render fine under it. */

import { ThemeProvider } from "antd-style";
import { theme as antdTheme } from "antd";
import type { ThemeConfig } from "antd";
import type { PropsWithChildren } from "react";

export const ESPET_COLORS = {
  /* monochrome canvas: black + white only. Two sanctioned hue exceptions —
     `blue` on primary buttons, and the dimmed semantic status hues
     (mint = success, red = failure, amber = warning). Everything else
     expresses itself through white brightness, never hue. */
  accent: "#e5e5e5",
  blue: "#93c5fd",
  success: "#8fd9ae",
  warning: "#e6cf94",
  danger: "#eda7a7",
  successBg: "rgba(143, 217, 174, 0.10)",
  warningBg: "rgba(230, 207, 148, 0.10)",
  dangerBg: "rgba(237, 167, 167, 0.10)",
  successBorder: "rgba(143, 217, 174, 0.28)",
  warningBorder: "rgba(230, 207, 148, 0.28)",
  dangerBorder: "rgba(237, 167, 167, 0.28)",
  violet: "#a3a3a3",
  amber: "#d4d4d4",
  red: "#a3a3a3",
  bg: "#050505",
  elevated: "#101010",
  text: "#f5f5f5",
  textMuted: "#a3a3a3",
  textDim: "#737373",
  border: "rgba(255,255,255,0.08)",
  glass: "rgba(255,255,255,0.055)",
  glassStrong: "rgba(255,255,255,0.08)",
};

const antdThemeConfig: ThemeConfig = {
  algorithm: antdTheme.darkAlgorithm,
  token: {
    colorPrimary: ESPET_COLORS.accent,
    colorInfo: "#d4d4d4",
    colorSuccess: ESPET_COLORS.success,
    colorWarning: ESPET_COLORS.warning,
    colorError: ESPET_COLORS.danger,
    colorBgBase: ESPET_COLORS.bg,
    colorBgContainer: ESPET_COLORS.elevated,
    borderRadius: 14,
    // one control height everywhere — inputs, selects, segmented, buttons (40)
    controlHeight: 40,
    fontFamily:
      'Outfit, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif',
    fontFamilyCode: "'JetBrains Mono', ui-monospace, monospace",
    fontSize: 15,
  },
  components: {
    Modal: { contentBg: "#0d0d0d", headerBg: "#0d0d0d" },
    Message: { contentBg: "#141414" },
    Card: { colorBgContainer: "rgba(255,255,255,0.055)" },
    Segmented: {
      // dim-blue selected segment (same family as primary buttons)
      itemSelectedBg: "rgba(147, 197, 253, 0.22)",
      itemSelectedColor: "#dbeafe",
    },
    Radio: {
      // solid radio buttons + checked dot — same dim-blue selection family
      colorPrimary: "rgba(147, 197, 253, 0.5)",
      buttonSolidCheckedBg: "rgba(147, 197, 253, 0.22)",
      buttonSolidCheckedColor: "#dbeafe",
      buttonSolidCheckedBorderColor: "rgba(147, 197, 253, 0.35)",
    },
    Button: {
      // roomier hit areas so labels never look hard-fitted
      paddingInline: 18,
      paddingInlineSM: 12,
      paddingInlineLG: 26,
      controlHeight: 40,
      controlHeightSM: 30,
      fontWeight: 500,
    },
  },
};

export function AppProviders({ children }: PropsWithChildren) {
  return (
    <ThemeProvider appearance="dark" themeMode="dark" theme={antdThemeConfig}>
      {children}
    </ThemeProvider>
  );
}
