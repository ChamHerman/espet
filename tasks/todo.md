# ESPET Hardware GPIO Tools Task Checklist

## Phase 1: Foundation (GPIO Policy & Display)
- [x] **Task 1**: Update GPIO Policy Allowlist (`main/tools/gpio_policy.h`)
- [x] **Task 2**: Configure OLED I2C Pins (`main/tools/tool_oled.c`)
- [x] **Checkpoint 1**: Foundation Build Clean

## Phase 2: Dual Hardware Buttons (White Snap & Green PTT Mic)
- [x] **Task 3**: Implement Dual Button Interrupt Handlers (`main/tools/button_handler.c/h`)
- [x] **Task 4**: Configure White Status LED (`main/tools/tool_gpio.c`)
- [x] **Checkpoint 2**: Dual Buttons & LED Ready

## Phase 3: I2S Audio Drivers & Tools (Mic & Amp)
- [x] **Task 5**: Implement I2S Shared Audio Driver & Playback Tool (`main/tools/tool_audio.c/h`)
- [x] **Task 6**: Implement INMP441 Mic Stream & Record Tool (`main/tools/tool_audio.c/h`)
- [x] **Checkpoint 3**: Audio Tools Ready

## Phase 4: Integration & Manual Telegram Testing Verification
- [x] **Task 7**: Create Telegram & Hardware Verification Guide (`docs/telegram_hardware_testing.md`)
- [x] **Checkpoint 4**: End-to-End Verification Complete
