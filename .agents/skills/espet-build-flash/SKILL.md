---
name: espet-build-flash
description: Build and flash the MimiClaw/ESPET ESP32-S3 firmware in this repo (espet) with the locally installed ESP-IDF v5.5.4. Use whenever the user asks to build, compile, flash, upload, deploy firmware, or test changes on the device — including phrases like "flash it", "build and upload", "put it on the board", "idf.py build", "verify it compiles", or after any edit to main/**, sdkconfig*, or partitions.csv.
---

# ESPET firmware build & flash

Builds and flashes the ESP32-S3 firmware in this repo using the local ESP-IDF
v5.5.4 install. The repo's `scripts/build_macos.sh` expects a different IDF
layout and `export.sh` fails on this machine (missing
`~/.espressif/espidf.constraints.v5.5.txt`), so drive `idf.py` directly with
the VS Code extension's Python venv — this is the only method verified to work
here.

## Environment (required for every command)

Run inside one `bash -c` block so the exports survive:

```bash
bash -c '
export IDF_PATH="$HOME/.espressif/v5.5.4/esp-idf"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/tools/python/v5.5.4/venv"
export IDF_TOOLS_PATH="$HOME/.espressif/tools"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$HOME/.espressif/tools/ninja/1.12.1:$PATH"
cd /Users/jeffwg/Documents/Project/espet
python "$IDF_PATH/tools/idf.py" build 2>&1 | tail -10
'
```

Notes:
- Invoke via `python "$IDF_PATH/tools/idf.py"`, **not** bare `idf.py` (the
  export script that would put `idf.py` on PATH fails here).
- If the venv path ever stops existing, re-read it from
  `.vscode/settings.json` → `idf.pythonBinPath` (strip `/bin/python`).

## Build

```bash
python "$IDF_PATH/tools/idf.py" build
```

Success looks like:
```
Project build complete. To flash, run: ...
mimiclaw.bin binary size 0x...  Smallest app partition is 0x300000 ... free.
```

- Report the binary size / partition-free line to the user.
- New `.c` files must be registered in `main/CMakeLists.txt` `SRCS` or the
  link fails — check that before blaming the toolchain.
- Repo rule (AGENTS.md): after editing `mimi_secrets.h`, `partitions.csv`, or
  `sdkconfig.defaults*`, run `idf.py fullclean` first, then build.

## Flash

1. **Free the serial port** — an `idf_monitor.py` / VS Code serial monitor
   process usually holds it and the flash fails with
   `Could not open /dev/cu.usbmodem..., the port is busy`. Find and kill it
   (tell the user you did; they can reopen the monitor):
   ```bash
   lsof 2>/dev/null | grep -i usbmodem | awk '{print $2}' | sort -u   # then kill <pid>
   ```
2. **Flash** (port from `.vscode/settings.json` → `idf.port`; re-check
   `/dev/cu.usbmodem*` exists first — the board sometimes drops off the bus):
   ```bash
   python "$IDF_PATH/tools/idf.py" -p /dev/cu.usbmodem5C4C2018651 flash
   ```
   The flash writes app + spiffs + srmodels per `build/flash_args` and ends
   with `Hard resetting via RTS pin... Done` — the device reboots into the new
   firmware automatically. No manual reset needed.
3. If the port vanishes mid-flash ("port is busy or doesn't exist"), ask the
   user to replug the board, re-check `ls /dev/cu.usb*`, retry.

## Serial monitor (optional)

```bash
python "$IDF_PATH/tools/idf_monitor.py" -p /dev/cu.usbmodem5C4C2018651 -b 115200 \
  --toolchain-prefix xtensa-esp32s3-elf- \
  "$PWD/build/mimiclaw.elf"
```

Quits with Ctrl+]. Prefer telling the user to run it in their own terminal so
it doesn't hold the port when the next flash happens.

## CI equivalence

GitHub Actions (`.github/workflows/build.yml`) builds in container
`espressif/idf:v5.5.2` with `idf.py set-target esp32s3 && idf.py fullclean &&
idf.py build`. A local build passing is a good proxy for CI; CI uses a
slightly older IDF patch version, so treat linker surprises there as toolchain
differences, not regressions.
