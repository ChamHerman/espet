# GPIO Control

Control and monitor GPIO pins on the ESP32-S3 for digital I/O.

## Hardware Pin Mapping
Hardware components are fixed to specific GPIO pins:
- **GPIO 42**: WHITE LED (Output: 1 = ON, 0 = OFF)
- **GPIO 41**: GREEN Push Button (Input Pull-Up: 0 = Pressed, 1 = Released)
- **GPIO 2**: SSD1306 OLED SCL / SCK
- **GPIO 1**: SSD1306 OLED SDA
- **GPIO 38 & 39**: Shared I2S Clocks (BCLK=38, WS=39 for INMP441 Mic & MAX98357A Amp)
- **GPIO 21**: MAX98357A Amp DIN
- **GPIO 40**: INMP441 Mic SD

## Rules & Tool Priority
1. **TOOL FIRST MANDATE**: For any hardware action (e.g. "turn off LED", "turn on LED", "check green button"), call `gpio_write` or `gpio_read` FIRST before returning any text.
2. **NEVER ask for pin numbers**. Pin mapping is fixed as listed above.
3. When asked to "turn on the white LED", immediately execute tool call `gpio_write {"pin": 42, "state": 1}`.
4. When asked to "turn off the white LED", immediately execute tool call `gpio_write {"pin": 42, "state": 0}`.
5. When asked to check green button, immediately execute tool call `gpio_read {"pin": 41}`.

## Examples

### Example 1: Turn off White LED
User: "Turn off the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 42, "state": 0}`
→ Output: `{"status":"success","pin":42,"state":0}`
→ Response: "The white LED on GPIO 42 has been turned OFF."

### Example 2: Turn on White LED
User: "Open the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 42, "state": 1}`
→ Output: `{"status":"success","pin":42,"state":1}`
→ Response: "The white LED on GPIO 42 has been turned ON."

### Example 3: Check Green Button
User: "Check if the green button is pressed"
→ FIRST STEP: Invoke tool `gpio_read {"pin": 41}`
→ Output: `{"status":"success","pin":41,"state":0}`
→ Response: "The green button on GPIO 41 is currently PRESSED (LOW)."
