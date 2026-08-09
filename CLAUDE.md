# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**Papagaio** — Arduino firmware for an ESP32 that records an IR or 433MHz RF
command and replays it later. Built to clone a fixed-code garage gate remote,
but the same firmware learns TV/AC infrared codes too. There is no
application-level build system — this is a single Arduino sketch
compiled/flashed with `arduino-cli` behind a `Makefile`.

The project began as a fork of BrincandoComIdeias' "Controle IR" (Q0962)
sketch; the IR record/replay layer is derived from it (MIT, see `LICENSE`).
It was made independent, so upstream is dead and irrelevant — never try to
merge or rebase against it.

- `firmware/firmware.ino` — the entire firmware (setup/loop, IR and RF
  record/send logic, serial command protocol). Arduino requires the sketch
  folder and the `.ino` to share a name, so renaming one means renaming both
  plus `SKETCH` in the `Makefile`.
- `firmware/PinDefinitionsAndMore.h` — vendored, unmodified helper from the
  IRremote library's examples. It sets per-platform defaults for
  `IR_RECEIVE_PIN`/`IR_SEND_PIN` (and feedback/tone pins) via a long
  `#if defined(...)` chain covering many boards/cores. Don't add project logic
  here — the `.ino` overrides its ESP32 defaults after including it (see
  below), which keeps this file a clean drop-in from upstream IRremote.
- `tools/serial_console.py` — the serial console behind `make monitor`; exists
  because the usual tools break on this board (see below).

## Conventions

Commit messages are written in **English**, following
[Conventional Commits](https://www.conventionalcommits.org):
`<type>[optional scope]: <description>`, with types like `feat`, `fix`,
`docs`, `refactor`, `chore`, `build`, `test`. Keep the subject imperative and
under ~72 characters; put the reasoning in the body.

```
feat(rf): store pulse length so replays match the original timing
fix(serial): stop buffering stdin so piped commands reach the board
docs: document the CH343 DTR/RTS download-mode trap
```

Note that the code itself — identifiers, comments, and the firmware's serial
output — is in Portuguese. Only commit messages are English; match the
surrounding language when editing a file.

## Build / flash / monitor

Use the `Makefile` — it wraps `arduino-cli` with the right FQBN, auto-detects
the serial port, and re-runs itself under `sg dialout` when the login session
lacks that group. `make help` lists everything.

```bash
make build     # compile only — the correctness check for this codebase
make flash     # compile + upload
make monitor   # interactive serial console (resets the board on connect)
make attach    # same console, but WITHOUT resetting (keeps an armed '#N' slot)
make reset     # reboot the board without reflashing
make ports     # list visible boards/ports
make setup     # (re)install the ESP32 core + IRremote/rc-switch
```

Override the port with `make flash PORT=/dev/ttyUSB0` if detection picks wrong.

There is no lint or test step — `make build` runs the full C++ compile against
the ESP32 toolchain and is what "passing" means here.

**Do not use `arduino-cli monitor`, `screen`, or `cat /dev/tty*` on this
board.** Its CH343 bridge asserts DTR/RTS on open, which drives the DevKit's
auto-reset circuit and boots the ESP32 into download mode
(`waiting for download`) instead of running the sketch — you get silence or
garbage bytes. `tools/serial_console.py` (what `make monitor` runs) clears
both lines immediately after `open()` and resets via an explicit EN pulse
instead. A healthy boot logs `boot:0x13 (SPI_FAST_FLASH_BOOT)`; `boot:0x3
(DOWNLOAD_BOOT)` means the board is stuck and needs `make reset`.

Flashing without `sudo` requires membership in the `dialout` group
(`sudo usermod -aG dialout $USER`, then a new login session).

## Architecture

**Single active "bus" state machine.** The firmware supports two independent
signal buses — InfraVermelho (`IrReceiver`/`IrSender` from IRremote) and RF
433MHz (`rfSwitch` from rc-switch) — but only one is active at a time via the
global `currentBus` (`'i'` or `'r'`), switched over serial. Record/send logic
in `loop()` branches on `currentBus` but shares the same state variables
(`estadoAtual`/`estadoAnt`/`index`) and the same serial command grammar for
both buses:

| Serial input | Effect |
|---|---|
| `i` / `r` | switch `currentBus` |
| `#` + `0-9` | arm recording into slot `index` on the active bus |
| `0-9` | replay the code stored in slot `index` on the active bus |
| `f` | cycle the RAW carrier frequency (`FREQUENCIAS[]`: 38/36/40/56 kHz) |
| `p` | cycle `repeticoesRaw`, how many times a RAW message is sent (1-4) |
| `t` + `0-9` | IR loopback self-test (`autoTesteIR`) |
| anything else | stop both receivers (idle) |

`f` and `p` are IR-only and affect **only** the RAW path, i.e. codes stored
with `protocol == UNKNOWN`. A demodulating IR receiver cannot measure the
original carrier, so 38kHz is a guess that happens to be right nearly always —
`f` exists for the exceptions and can only be resolved by trial.

`autoTesteIR` sends a slot while leaving the receiver running, so the board
captures its own emission and prints it for comparison against the original
recording. This is only valid on ESP32, where sending uses LEDC and receiving
uses a separate hardware timer; on platforms where both contend for one timer
it would not work.

**Two parallel 10-slot storage arrays**, one per bus, populated by
`storeIRCode`/`storeRFCode` and played back by `sendIRCode`/`sendRFCode`:
- `sStoredIRData[10]` — full `IRData` plus a raw-timing fallback buffer, for
  when IRremote can't decode a known protocol.
- `sStoredRFData[10]` — rc-switch's `{value, bitLength, protocol,
  pulseLength}`, enough to replay a **fixed-code** RF remote. This will not
  work against rolling-code (HCS301/KeeLoq) gate motors — that's a fundamentally
  different (and out of scope) capture technique.

**Pin overrides happen between the two `#include`s.** `IR_RECEIVE_PIN` /
`IR_SEND_PIN` are `#define`d by `PinDefinitionsAndMore.h` for ESP32 as `15`
and `4`. Because `IRremote.hpp` bakes `IR_SEND_PIN` into the send path at
compile time (ESP32 uses `SEND_PWM_BY_TIMER`), the actual pins are changed by
`#undef`-ing and re-`#define`-ing them in the `.ino` *after* including
`PinDefinitionsAndMore.h` but *before* including `IRremote.hpp`. `RF_RECEIVE_PIN`
/`RF_TRANSMIT_PIN` are plain `#define`s read at runtime by `rfSwitch.enableReceive()`/
`enableTransmit()`, no such ordering constraint.

Current pin map (all four signal pins deliberately share one physical header
row on the ESP32 DevKit — `3V3, D19, D21, D22, D23` — because only that row is
reachable on the user's protoboard; GND reaches this row via one bridge wire
from the opposite row's GND pin, and there is no VIN/5V on this row so every
module runs on 3.3V):

- `IR_RECEIVE_PIN` = 22, `IR_SEND_PIN` = 19
- `RF_RECEIVE_PIN` = 23, `RF_TRANSMIT_PIN` = 21

The IR transmitter LED hangs directly off `IR_SEND_PIN` with no transistor, so
it is capped by the pin's ~20mA against the 100-500mA a factory remote pulses.
Replay works but needs the LED aimed at the target's receiver window. **Every
"the replay doesn't work" symptom in this project so far has been optical
power, never software** — check aim and distance before touching timing or
carrier frequency. The `t` self-test exists to settle that question quickly.

When changing pins, avoid: `GPIO0/2/5/12/15` (boot strapping), `GPIO6-11`
(internal SPI flash on this board's silkscreen: CMD/SD0/SD1/SD2/SD3/CLK —
using them crashes the boot), and `GPIO1/3` (RX0/TX0, the USB serial
console this firmware's protocol runs over).

`LED_BUILTIN` is not defined by the generic `esp32:esp32:esp32` ("ESP32 Dev
Module") board variant, so the `.ino` guards it with `#ifndef LED_BUILTIN`
before using it for `STATUS_PIN`.
