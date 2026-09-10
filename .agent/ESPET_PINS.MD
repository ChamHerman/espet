# ESPET / MimiClaw Hardware Pinout Reference

| Peripheral / Signal | Component | Pin | Protocol / Details |
|---|---|---|---|
| **OLED Clock (SCL)** | SSD1306 OLED Display | **GPIO 2** | I2C0 SCL (400 kHz, pull-up) |
| **OLED Data (SDA)** | SSD1306 OLED Display | **GPIO 1** | I2C0 SDA (400 kHz, pull-up) |
| **Green Push Button** | Tactile Switch | **GPIO 41** | Input Pull-Up (0 = Pressed, PTT Audio Record) |
| **White Push Button** | Tactile Switch | *Unassigned* | Currently disabled / unassigned (was GPIO 38) |
| **Status LED** | Discrete White LED | **GPIO 42** | Output (1 = HIGH / ON, 0 = LOW / OFF) |
| **Audio I2S BCLK** | INMP441 Mic + MAX98357A Amp | **GPIO 38** | I2S0 Master Bit Clock (SCK / BCLK) |
| **Audio I2S WS** | INMP441 Mic + MAX98357A Amp | **GPIO 39** | I2S0 Word Select / Frame Clock (WS / LRC) |
| **Audio Speaker Out** | MAX98357A Amplifier | **GPIO 21** | I2S0 Data Output (`DOUT` / `DIN`) |
| **Audio Mic Data In** | INMP441 Microphone | **GPIO 40** | I2S0 Data Input (`DIN` / `SD`) |
| **Mic / Periph Power** | Power Rails | **5V / 3.3V** | VCC |
| **Ground** | Ground Rails | **GND** | Ground (INMP441 GND + L/R to GND for Left channel) |
| **Camera DVP Bus** | OV2640 Sensor | **GPIO 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18** | DVP parallel bus & SCCB |
| **Serial Console** | UART0 (CP2102/CH340) | **GPIO 43 (TX), 44 (RX)** | 115200 baud interactive REPL console |
| **Native USB** | ESP32-S3 USB/JTAG | **GPIO 19 (D-), 20 (D+)** | Native USB flashing & JTAG |
