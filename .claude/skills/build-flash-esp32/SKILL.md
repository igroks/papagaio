---
name: build-flash-esp32
description: Compile, flash and monitor the Papagaio ESP32 firmware via the project Makefile. Use whenever the user asks to build, compile, verify, flash, upload, gravar, reset the board, open the serial monitor, or test IR/RF commands against real hardware.
---

# Build / flash / monitor the Papagaio ESP32 firmware

Everything runs through the project `Makefile`, which wraps `arduino-cli`
(installed at `~/.local/bin/arduino-cli`) with the correct FQBN
(`esp32:esp32:esp32`), auto-detects the serial port, and transparently
re-invokes itself under `sg dialout` when the current login session doesn't
yet carry that group.

Run these from the repo root:

```bash
make build     # compile only — this is the correctness check, no tests exist
make flash     # compile + upload
make monitor   # interactive serial console (resets the board on connect)
make attach    # console WITHOUT reset — use to keep an armed '#N' slot alive
make reset     # reboot the board without reflashing
make ports     # list visible boards/ports
make deps      # show installed core + library versions
make setup     # (re)install ESP32 core and IRremote/rc-switch
make clean     # drop the arduino-cli build cache
```

Always `make build` after editing the sketch. A clean run ends with
`Sketch uses N bytes...`; any compiler output means the change broke the
build — fix it before suggesting a flash.

Override port detection with `make flash PORT=/dev/ttyUSB0`.

## Serial: the DTR/RTS trap

**Never use `arduino-cli monitor`, `screen`, `minicom`, or `cat /dev/ttyACM0`
on this board.** The CH343 bridge asserts DTR/RTS on open, which drives the
DevKit's auto-reset circuit and boots the ESP32 into *download mode* instead
of running the sketch. Symptoms: total silence, or a stream of invalid bytes.

`tools/serial_console.py` (behind `make monitor`) avoids this by clearing DTR
and RTS immediately after `open()` and resetting with an explicit EN pulse.

Reading the boot line tells you which state you're in:
- `boot:0x13 (SPI_FAST_FLASH_BOOT)` — healthy, sketch is running
- `boot:0x3 (DOWNLOAD_BOOT...)` / `waiting for download` — stuck; `make reset`

If upload fails to sync, it's usually a charge-only USB cable or another
program holding the port open.

## Driving the firmware non-interactively

To script a capture/replay test instead of handing the console to the user,
pipe commands into the console (it sends each line without the newline, and
exits on EOF):

```bash
# select IR bus, arm slot 0, then listen ~25s for the remote press
{ sleep 2; printf 'i\n'; sleep 1; printf '#0\n'; sleep 25; } | make monitor
```

Keep the pauses. The firmware reads one character per `loop()` and its `#`
handler does `delay(50)` before reading the slot digit, so commands shoved
back-to-back get interleaved and swallowed. The leading `sleep 2` lets the
board finish booting — bytes sent before `Serial.begin()` are lost.

Use `attach` (no reset) to reconnect once a slot is armed — `monitor` reboots
the board and clears it. Recorded slots live in RAM only and are lost on any
reset or power cycle.

The firmware's serial protocol (115200 baud, one char at a time): `i`/`r`
switch the active bus, `#` + `0-9` arms recording into that slot, a bare
`0-9` replays it. See `CLAUDE.md` for the pin map and architecture.
