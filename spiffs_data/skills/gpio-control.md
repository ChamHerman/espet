# GPIO Control

Control and monitor GPIO pins on the ESP32-S3 for digital I/O.

## Hardware Pin Mapping
Hardware components are fixed to specific GPIO pins:
- **GPIO 40**: WHITE LED (Output: 1 = ON, 0 = OFF)
- **GPIO 39**: GREEN Push Button (Input Pull-Up: 0 = Pressed, 1 = Released)
- **GPIO 38**: WHITE Push Button (Input Pull-Up: 0 = Pressed, 1 = Released)
- **GPIO 21**: SSD1306 OLED SCL / SCK
- **GPIO 47**: SSD1306 OLED SDA
- **GPIO 41 & 42**: Shared I2S Clocks (BCLK=41, WS=42 for INMP441 Mic & MAX98357A Amp)
- **GPIO 2**: MAX98357A Amp DIN
- **GPIO 1**: INMP441 Mic SD

## Rules & Tool Priority
1. **TOOL FIRST MANDATE**: For any hardware action (e.g. "turn off LED", "turn on LED", "check green button"), call `gpio_write` or `gpio_read` FIRST before returning any text.
2. **NEVER ask for pin numbers**. Pin mapping is fixed as listed above.
3. When asked to "turn on the white LED", immediately execute tool call `gpio_write {"pin": 40, "state": 1}`.
4. When asked to "turn off the white LED", immediately execute tool call `gpio_write {"pin": 40, "state": 0}`.
5. When asked to check green button, immediately execute tool call `gpio_read {"pin": 39}`.
6. When asked to check white button, immediately execute tool call `gpio_read {"pin": 38}`.

## Examples

### Example 1: Turn off White LED
User: "Turn off the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 40, "state": 0}`
→ Output: `{"status":"success","pin":40,"state":0}`
→ Response: "The white LED on GPIO 40 has been turned OFF."

### Example 2: Turn on White LED
User: "Open the white LED light"
→ FIRST STEP: Invoke tool `gpio_write {"pin": 40, "state": 1}`
→ Output: `{"status":"success","pin":40,"state":1}`
→ Response: "The white LED on GPIO 40 has been turned ON."

### Example 3: Check Green Button
User: "Check if the green button is pressed"
→ FIRST STEP: Invoke tool `gpio_read {"pin": 39}`
→ Output: `{"status":"success","pin":39,"state":0}`
→ Response: "The green button on GPIO 39 is currently PRESSED (LOW)."
