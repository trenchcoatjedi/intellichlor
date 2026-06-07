# AGENTS.md — IntelliChlor ESPHome External Component

Guidance for LLM coding agents working in this repository. Read this before editing.

## What this is

An **ESPHome external component** that talks to a Pentair **IntelliChlor** salt-water
chlorinator (SWG — Salt Water Generator) over **RS-485**. Normally the IntelliChlor is
driven by a Pentair automation panel (EasyTouch/IntelliCenter). This component lets an
ESP impersonate that controller ("takeover mode") so it can read status and set the
chlorine output percentage directly, exposing everything to Home Assistant.

This is **not** a standalone firmware — it is a reusable component dropped into a user's
own ESPHome YAML via `external_components`. There is no build system, test suite, or CI
in this repo; it is compiled by ESPHome inside a consuming project.

## Repository layout

```
components/intellichlor/
  __init__.py                     # hub component: config schema, UART + flow-control pin, time_id
  intellichlor.h / .cpp           # core logic: framing, send queue, RX parser, polling, boost timer
  sensor.py                       # salt_ppm, water_temp, output_percent, status, error, set_percent, boost_remaining
  binary_sensor.py                # fault bits (no_flow, low_salt, ... check_pcb)
  text_sensor.py                  # version, firmware_version
  number/                         # swg_percent (0-100%, the writable output setpoint)
  switch/                         # takeover_mode (enable/disable controller takeover)
  select/                         # swg_boost (Off/6h/12h/24h/48h boost selector)
  button/                         # end_boost (cancel an active boost early)
  notes.txt                       # raw protocol capture / reverse-engineering notes
README.md                         # one line
```

The hub class is `INTELLICHLORComponent` (`PollingComponent` + `uart::UARTDevice`,
`MULTI_CONF = True`). Sub-entities attach to it via `SUB_SENSOR` / `SUB_SWITCH` /
`SUB_NUMBER` etc. macros declared in `intellichlor.h`, and each platform `.py` wires its
entities to the hub through `CONF_INTELLICHLOR_ID`.

## How to use it (consumer YAML shape)

```yaml
external_components:
  - source: github://wolfson292/intellichlor    # or local path
    components: [intellichlor]

uart:
  id: swg_uart
  tx_pin: ...
  rx_pin: ...
  baud_rate: 9600          # IntelliChlor RS-485 is 9600 8N1
  # parity NONE, stop_bits 1 are required (enforced by FINAL_VALIDATE_SCHEMA)

intellichlor:
  id: swg
  uart_id: swg_uart
  flow_control_pin: GPIOXX   # optional: DE/RE pin for half-duplex RS-485 transceiver
  update_interval: 60s

sensor:
  - platform: intellichlor
    intellichlor_id: swg
    salt_ppm:       { name: "Salt PPM" }
    water_temp:     { name: "SWG Water Temp" }    # NOTE: reported in °F (see below)
    output_percent: { name: "SWG Output %" }      # cell-reported actual output (0x16 §5.2)
    status:         { name: "SWG Status" }
    error:          { name: "SWG Error" }
    set_percent:    { name: "SWG Set Percent" }

binary_sensor:
  - platform: intellichlor
    intellichlor_id: swg
    no_flow:       { name: "SWG No Flow" }
    low_salt:      { name: "SWG Low Salt" }
    very_low_salt: { name: "SWG Very Low Salt" }   # raw protocol: "high salt"
    clean:         { name: "SWG Cleaning" }        # raw protocol: "clean cell"
    high_current:  { name: "SWG High Current" }
    low_volts:     { name: "SWG Low Volts" }
    low_temp:      { name: "SWG Low Temp" }
    check_pcb:     { name: "SWG Check PCB" }

text_sensor:
  - platform: intellichlor
    intellichlor_id: swg
    version:          { name: "SWG Version" }        # model string, e.g. Intellichlor--60
    firmware_version: { name: "SWG Firmware" }        # cell firmware, e.g. 1.8 (0x16 §5.2)

number:
  - platform: intellichlor
    intellichlor_id: swg
    swg_percent: { name: "SWG Output %" }

switch:
  - platform: intellichlor
    intellichlor_id: swg
    takeover_mode: { name: "SWG Takeover" }
```

## Protocol (the important part)

RS-485, 9600 8N1, half-duplex. The ESP plays the role of the Pentair controller.

### Frame format

```
0x10 0x02 | <payload bytes...> | <checksum> | 0x10 0x03
  header        command/data       1 byte       footer
```

- **Checksum** = 8-bit additive sum (mod 256) of the header bytes **and** the payload
  bytes (computed on the *unstuffed* bytes). The footer is **not** included. Example:
  `10 02 50 14 00` → `0x10+0x02+0x50+0x14+0x00 = 0x76`.
- **DLE byte-stuffing (protocol §3):** any `0x10` inside the ADDR/CMD/DATA/CKS region is
  sent as the two bytes `10 00`; the framing `10 02` / `10 03` are literal. `send_command_()`
  stuffs on TX; `readline_()` un-stuffs on RX (collapses `10 00` → `10`). This replaced an
  old one-off "pad byte when pct==16" hack — `set_percent_` now always sends `{0x50,0x11,pct}`
  and the general stuffing emits the identical `10 02 50 11 10 00 83 10 03` for 16%.
- **RX checksum validation:** `readline_()` recomputes the checksum over the unstuffed bytes
  and drops the frame (logging a warning, leaving the send queue intact so the command
  retries) if it does not match the received CKS byte.
- Built in `send_command_()` (`intellichlor.cpp`). It assembles the full framed packet
  into a `std::vector<uint8_t>` and pushes it onto `send_queue_`; it does **not** write to
  the UART directly.

### Commands the ESP sends (payload starts with `0x50` = address of the chlorinator)

| Purpose      | Payload bytes              | Notes |
|--------------|----------------------------|-------|
| Takeover     | `0x50 0x00 0x00`           | claims the controller role (spec §5 "Probe", `0x00`) |
| Set percent  | `0x50 0x11 <pct>`          | `<pct>` 0–100; `0x10` (16%) is DLE-stuffed automatically by `send_command_` |
| Get version  | `0x50 0x14 0x00`           | hardware-confirmed (notes.txt) but **not in spec §5** — leave as-is |
| Get temp     | `0x50 0x15 0x00`           | hardware-confirmed (notes.txt) but **not in spec §5** — leave as-is |

### Responses the ESP parses (parsed in `readline_()`, dispatched on `buffer[3]`)

| `buffer[3]` | Response   | Decoding |
|-------------|------------|----------|
| `0x03`      | Version    | ASCII model string, bytes `[5 .. pos-3]` → `version` text sensor (e.g. `Intellichlor--60`). Real replies carry a leading `0x00` at `[4]` that the spec §7 example omits |
| `0x16`      | Status     | `buffer[4]` = water temp **in °F** (spurious `0` ignored, triggers `run_again_`). Per spec §5.2 also carries output % (`buffer[5]`, → `output_percent` sensor) and firmware version (`buffer[6].[7]`, → `firmware_version` text sensor); both length-guarded (`pos>=8`, `pos>=10`) since short replies are temp-only |
| `0x12`      | Set/status | `salt_ppm = buffer[4] * 50`; `buffer[5]` = error/status bitfield (see below) |
| `0x01`      | Hello/Ack  | `status = buffer[4]` (the data byte, spec §7) when present (`pos>=7`) → `status` sensor |

### Error / status bitfield (`buffer[5]` of the `0x12` response)

What the **current .cpp** publishes (aligned to protocol **spec §5.4**):

| Bit | Mask | Binary sensor   | spec §5.4 label |
|-----|------|-----------------|-----------------|
| 0   | 0x01 | `no_flow`       | No Flow         |
| 1   | 0x02 | `low_salt`      | Low Salt        |
| 2   | 0x04 | `very_low_salt` | Very Low Salt   |
| 3   | 0x08 | `high_current`  | High Current    |
| 4   | 0x10 | `clean`         | Clean Cell      |
| 5   | 0x20 | `low_volts`     | Low Voltage     |
| 6   | 0x40 | `low_temp`      | Cold Water      |
| 7   | 0x80 | `check_pcb`     | (not in §5.4; hardware-observed extra, kept) |

⚠️ **This mapping diverges from the `notes.txt` hardware capture at bits 3 and 4.** The
raw capture (and the user's own `Error:80` / Check-PCB sample) documents the field as:

```
0x01 no flow | 0x02 low salt | 0x04 high salt | 0x08 clean cell | 0x10 high current
0x20 low volts | 0x40 low temp | 0x80 check PCB | 0xFF off
```

i.e. `notes.txt` has **0x08 = clean cell, 0x10 = high current** — the opposite of spec §5.4.
The code was deliberately set to follow the **spec** (owner's decision). If on real hardware
a "clean cell" condition lights up the `high_current` entity (or vice versa), the spec is
wrong for this cell and bits 3/4 should be swapped back to the `notes.txt` order. The full
raw value is always available via the `error` sensor, so a consumer can decode it manually.

## Runtime behavior

- **Polling:** `update()` (every `update_interval`, default 60s) and `loop()` both funnel
  into `read_all_info()`, which is rate-limited to ~once/second via `last_loop_timestamp_`.
  Each cycle: if takeover is on → send Takeover + SetPercent; always → GetVersion +
  GetTemp.
- **Send queue / timing:** commands are queued, not sent inline. `loop()` drains one
  packet every >100 ms (to avoid colliding with the real bus traffic), toggling
  `flow_control_pin_` around the write for half-duplex DE/RE control. A matching RX
  response pops the queue; otherwise it retries up to the per-command retry count, then
  drops. Queue is purged if it exceeds 64 entries.
- **Takeover semantics:** with `takeover_mode` **off**, the component only reads (passive
  monitor of the existing controller). With it **on**, the ESP actively asserts the output
  percentage from `swg_percent` every cycle. Changing `swg_percent` or `takeover_mode`
  calls back into `read_all_info()` to push the new state promptly.
- **`swg_percent` persistence:** `SWGPercentNumber` is a `Component` that flash-backs its
  value via an `ESPPreferenceObject` (saved in `control()`, restored in `setup()`), so the
  commanded output % survives a reboot/OTA instead of resetting to 0. Its setup priority is
  one above the parent's so the restored value is in place before the first poll reads it.
- **Boost:** the `swg_boost` select (Off/6h/12h/24h/48h) calls `start_boost(hours)` /
  `cancel_boost()` on the hub; the `end_boost` button and selecting `Off` both cancel. While
  `boost_active_`, `read_all_info()` sends `set_percent_(100)` instead of `swg_percent` — but
  only inside the takeover-on branch, so **boost has no effect unless takeover is ON** (it
  never toggles the switch; it logs a warning if armed while off). `loop()` runs `tick_boost_()`
  (expiry → `cancel_boost()`, plus a minute-resolution `boost_remaining` sensor). The boost
  end-time is mirrored to flash as an absolute epoch (`BoostPref`, guarded by `#ifdef USE_TIME`
  + the optional `time_id`); `try_restore_boost_()` resumes it after a reboot once the RTC is
  valid. Without a `time_id`/`USE_TIME`, boost is a millis() timer that cancels on reboot.

## Previously-fixed bugs (history / context)

These were all fixed on the `fix/component-bugs` branch (one commit each). Listed here so
agents understand the rationale and don't reintroduce them:

1. **Null-deref if optional entities are not configured** — `read_all_info()` (reached from
   `setup()`), `set_swg_percent()`, and the response handlers in `readline_()` dereferenced
   optional `*_sensor_` / switch / number pointers unconditionally. Now all guarded with
   `!= nullptr`. Keep new entity accesses guarded too.
2. **Typo no-op in `setup()`** — `last_debug_timestamp_ - millis();` (`-` instead of `=`).
   Now an assignment.
3. **Retry counter never persisted** — `loop()` incremented a local copy of the queue
   front, so the attempt count never advanced and exhausted commands were both never
   dropped *and* re-sent one extra time. Now takes the front by reference, increments in
   place, and only transmits in the non-exhausted branch.
4. **`data->size()` in the send log** — `data` is a `std::vector` (no `operator->`); broke
   the build at `VERY_VERBOSE`. Now `data.size()`.
5. **Inert fault sensors** — `high_current`, `low_volts`, `check_pcb`, and the `swg_debug`
   text sensor had their `publish_state` calls commented out. Now published (guarded), and
   the status bitfield was re-mapped to the `notes.txt` layout (this moved `clean` from
   bit 4 to bit 3 and put `high_current` on bit 4). **The bit decode still wants hardware
   confirmation.**
6. **Copy-paste variable name** — `binary_sensor.py` used `ld2410_component`; renamed to
   `intellichlor_component`.
7. **Misleading `UNIT_CELSIUS` import** — temp is reported in °F; the unused Celsius import
   was removed.

### Protocol-spec alignment pass (later changes)

A second pass brought the wire handling in line with `docs/IntelliChlor_Protocol.md`:

8. **DLE byte-stuffing (§3)** — `send_command_` now stuffs `0x10`→`10 00` in the
   ADDR/CMD/DATA/CKS region; `readline_` un-stuffs on RX. Removed the old "pad byte when
   pct==16" special case in `set_percent_` (the general stuffing emits identical bytes).
9. **RX un-stuffing + robust framing** — `readline_`'s byte loop was rewritten around a
   `static bool esc` escape flag instead of the fragile `buffer[pos]==0x03 &&
   buffer[pos-1]==0x10` footer test, so a stuffed `0x10` data byte no longer shifts/corrupts
   the payload. Footer bytes are still stored in-buffer so the handler offsets are unchanged.
10. **RX checksum validation (§4)** — complete frames are checksum-checked before dispatch;
    a mismatch logs a warning and drops the frame **without** popping the send queue (so the
    command retries).
11. **`0x16` extension (§5.2)** — the Status reply now also yields output % (`output_percent`
    sensor) and firmware version (`firmware_version` text sensor), length-guarded.
12. **`0x01` handler** — publishes the data byte `buffer[4]` (guarded) instead of the
    always-`0x01` command byte `buffer[3]`.
13. **Status bitmask → spec §5.4** — bits 3/4 swapped to `high_current`/`clean` per the spec.
    ⚠️ This **diverges from the `notes.txt` hardware capture**; see the bitfield section.

Still-open consideration: temperature is published in °F via a `"°F"` unit string. If you
ever want Celsius, convert both the value and the unit together.

## Conventions for edits

- This is a header-only-style ESPHome component split into `.h`/`.cpp`. New entities follow
  the existing pattern: add a `SUB_*` macro in `intellichlor.h`, a `CONF_*` + schema entry
  in the relevant platform `.py`, and a `publish_state` in `readline_()`.
- Keep the three layers in sync when adding a field: **C++ member/parser**, **Python schema
  (`CONF_*` + `to_code`)**, and **example YAML**.
- Don't introduce blocking `delay()`s in `loop()`/`update()` — ESPHome warns above 30 ms,
  and the existing `log_hex` helpers already trip this (see the "took a long time" warnings
  in `notes.txt`). Prefer the non-blocking send queue.
- `CODEOWNERS = ["@wolfson292"]`. `MULTI_CONF = True` — multiple IntelliChlor hubs per
  device are allowed.
