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
  __init__.py                     # hub component: config schema, UART + flow-control pin
  intellichlor.h / .cpp           # core logic: framing, send queue, RX parser, polling
  sensor.py                       # salt_ppm, water_temp, status, error, set_percent
  binary_sensor.py                # fault bits (no_flow, low_salt, ... check_pcb)
  text_sensor.py                  # version, swg_debug
  number/                         # swg_percent (0-100%, the writable output setpoint)
  switch/                         # takeover_mode (enable/disable controller takeover)
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
    salt_ppm:    { name: "Salt PPM" }
    water_temp:  { name: "SWG Water Temp" }    # NOTE: reported in °F (see below)
    status:      { name: "SWG Status" }
    error:       { name: "SWG Error" }
    set_percent: { name: "SWG Set Percent" }

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
    version:   { name: "SWG Version" }
    swg_debug: { name: "SWG Debug" }   # last parsed packet, as a string

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
  bytes. The footer is **not** included. Example: `10 02 50 14 00` →
  `0x10+0x02+0x50+0x14+0x00 = 0x76`.
- Built in `send_command_()` (`intellichlor.cpp`). It assembles the full framed packet
  into a `std::vector<uint8_t>` and pushes it onto `send_queue_`; it does **not** write to
  the UART directly.

### Commands the ESP sends (payload starts with `0x50` = address of the chlorinator)

| Purpose      | Payload bytes              | Notes |
|--------------|----------------------------|-------|
| Takeover     | `0x50 0x00 0x00`           | claims the controller role |
| Set percent  | `0x50 0x11 <pct>`          | `<pct>` 0–100; **pct == 16 needs a trailing `0x00` pad byte** (see `set_percent_`) |
| Get version  | `0x50 0x14 0x00`           | |
| Get temp     | `0x50 0x15 0x00`           | |

### Responses the ESP parses (parsed in `readline_()`, dispatched on `buffer[3]`)

| `buffer[3]` | Response   | Decoding |
|-------------|------------|----------|
| `0x03`      | Version    | ASCII string, bytes `[5 .. pos-3]` → `version` text sensor (e.g. `Intellichlor--60`) |
| `0x16`      | Temp       | `buffer[4]` = water temp **in °F**; a spurious `0` is ignored. Triggers an immediate re-poll (`run_again_`) |
| `0x12`      | Set/status | `salt_ppm = buffer[4] * 50`; `buffer[5]` = error/status bitfield (see below) |
| `0x01`      | Takeover   | `status = buffer[3]` → `status` sensor |

### Error / status bitfield (`buffer[5]` of the `0x12` response)

What the **current .cpp** publishes (aligned to `notes.txt`):

| Bit | Mask | Binary sensor   | `notes.txt` meaning |
|-----|------|-----------------|---------------------|
| 0   | 0x01 | `no_flow`       | no flow             |
| 1   | 0x02 | `low_salt`      | low salt            |
| 2   | 0x04 | `very_low_salt` | high salt           |
| 3   | 0x08 | `clean`         | clean cell          |
| 4   | 0x10 | `high_current`  | high current        |
| 5   | 0x20 | `low_volts`     | low volts           |
| 6   | 0x40 | `low_temp`      | low temp            |
| 7   | 0x80 | `check_pcb`     | check PCB           |

The `notes.txt` raw capture documents the field as:

```
0x01 no flow | 0x02 low salt | 0x04 high salt | 0x08 clean cell | 0x10 high current
0x20 low volts | 0x40 low temp | 0x80 check PCB | 0xFF off
```

⚠️ Two of the ESPHome **entity names disagree with the raw protocol labels**:
`very_low_salt` (bit 2) is actually *high salt*, and `clean` (bit 3) is *clean cell*.
The bit *positions* now match `notes.txt`, but the conf names were left unchanged to
avoid breaking existing YAML. The full raw value is also exposed via the `error` sensor,
so a consumer can always decode it themselves. **Confirm the bit decode against hardware
before trusting the labels.**

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
