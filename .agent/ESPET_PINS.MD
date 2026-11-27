# ESP32-S3 Hardware Pin Map (Authoritative — 2026-09 remap)

Source of truth for GPIO assignments on the ESP32-S3-WROOM-1-N16R8.
`main/mimi_config.h` and `main/tools/gpio_policy.h` are aligned to this remap.

## User-Facing Peripherals

| Peripheral | Component | GPIO (new) | Was | Notes |
|---|---|---|---|---|
| OLED Clock | SSD1306 | **2** | 21 | I2C SCL |
| OLED Data | SSD1306 | **1** | 47 | I2C SDA |
| Green Push Button | Tactile Switch | **41** | 39 | Input pull-up; press-hold = PTT voice record |
| Status LED (white) | Discrete LED | **42** | 40 | Output (1 = ON, 0 = OFF) |
| Audio BCLK / SCK | INMP441 + MAX98357A | **38** | 41 | I2S bit clock (shared mic + amp) |
| Audio WS / LRC | INMP441 + MAX98357A | **39** | 42 | I2S word select (shared mic + amp) |
| Mic Data In (SD) | INMP441 | **40** | 1 | I2S data input |
| Speaker Data Out (DIN) | MAX98357A | **21** | 2 | I2S data output |
| Mic Power | INMP441 VCC | **5V** | — | — |
| Mic Ground | INMP441 GND + L/R | **GND** | — | L/R tied low = left channel |

**WHITE push button: REMOVED** (old GPIO 38 → now the I2S BCLK line). The camera
snapshot trigger it used to provide no longer has a physical button; camera capture
must be driven by the agent (tool call) or a future replacement input.

## Camera Bus (OV2640 DVP)

Pins in `main/tools/tool_camera.c`: XCLK **15** · SIOD **4** · SIOC **5** ·
D7 **16** · D6 **17** · D5 **18** · D4 **12** · D3 **10** · D2 **8** · D1 **9** ·
D0 **11** · VSYNC **6** · HREF **7** · PCLK **13**

## Reserved Pins (blocked for user GPIO tools)

| Block | Pins | Reason |
|---|---|---|
| Octal Flash/PSRAM | 33–37 | Board wiring — never touch |
| Native USB JTAG | 19, 20 | Flashing + JTAG |
| UART0 Console | 43 (TX), 44 (RX) | Serial REPL (115200) |
| Camera DVP bus | 4–18 | Camera-only |

Active user-GPIO allowlist in `main/tools/gpio_policy.h` is `"41,42"` (Green Button
and White LED only; I2S, OLED I2C, and Camera DVP pins are protected).
