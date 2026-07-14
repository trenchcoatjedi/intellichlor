# IntelliChlor ESPHome Component for Pentair PC100 Power Center

An [ESPHome](https://esphome.io/) device using an external component for talking to a **Pentair PC100 Power Center** over RS-485 and exposing it to Home Assistant — without a Pentair automation panel (EasyTouch / IntelliCenter). Forked from [wolfson292](https://github.com/wolfson292/intellichlor)

| Component | What it does |
|-----------|--------------|
| [`intellichlor`](#intellichlor) | Read & control a Pentair **IntelliChlor / iChlor** salt chlorine generator (SWG): salt level, water temp, faults, and the chlorine output %. The original component has "takeover mode" disabled at boot however without this mode, the component is unable to read most of the flags from the SCG passively. The YAML modifies this code to enable this mode at boot. The YAML also adds a deep sleep component and trigger button if you'd like to sleep the ESP32 when the SCG is powered down. |

---

## Hardware

- An [**ESP32-C3 Supermini module** from Tenstar Robot](https://tenstar.pro/robot-esp32-c3-supermini/)
- A [**MAX485 RS-485 transceiver**](https://www.amazon.com/ANMBEST-Transceiver-Arduino-Raspberry-Industrial-Control/dp/B088Q8TD4V)
  - ESP `tx_pin` → transceiver `DI`
  - ESP `rx_pin` ← transceiver `RO`
  - Optional `flow_control_pin` → `DE` + `!RE` tied together (for non-auto-direction
    modules in half-duplex).
- The IntelliChlor / Pentair bus is **9600 8N1** (parity `NONE`, 1 stop bit). The
  `intellichlor` component enforces this at validation time.

> ⚠️ **Mains & pool safety.** This involves wiring into pool equipment carrying mains
> voltage. Do the work with power off, and only if you're comfortable with it. This is
> unofficial, reverse-engineered software with **no affiliation with Pentair**. Use at
> your own risk.

## Build

- Cut off the RE pin from the MAX485
- Bridge the RE and DE pins on the MAX485 with solder
- Solder MAX485 DI pin to Supermini GPIO0
- Solder MAX485 DE pin to Supermini GPIO1
- Solder MAX485 RO pin to Supermini GPIO3
- Solder pins to Supermini 5V and Ground

## Wiring

- Supermini 5V to MAX485 VCC
- Supermini Ground to MAX485 GND
- MAX485 "B" to PC100 Power Center terminal block that corresponds to "Green" on PC100 PCB silkscreen
- MAX485 "A" to PC100 Power Center terminal block that corresponds to "Yellow" on PC100 PCB silkscreen
---

## Quick start

Point `external_components` at this repo and pull in the components you need:

```yaml
external_components:
  - source: github://trenchcoatjedi/intellichlor
    components: [intellichlor]      
    refresh: 1d
```

A complete, copy-paste-friendly single-device config lives in
[`example.yaml`](example.yaml). The sections below summarize each component.

> ESPHome caches external components. If a rebuild doesn't pick up changes, lower
> `refresh` or clear the `.esphome/` directory so it re-pulls.

---

## intellichlor

Talks to the IntelliChlor cell (a slave at bus address `0x50`). With **takeover off**
the ESP passively reads the cell as the existing controller polls it; with **takeover
on** the ESP impersonates the controller and asserts the output % itself.

```yaml
uart:
  id: swg_uart
  tx_pin: GPIO17
  rx_pin: GPIO18
  baud_rate: 9600
  parity: NONE        # required
  stop_bits: 1        # required

# Optional: a time source lets "boost" survive a reboot/OTA (stored as an
# absolute timestamp in flash).
time:
  - platform: sntp
    id: sntp_time

intellichlor:
  id: swg
  uart_id: swg_uart
  # flow_control_pin: GPIO8   # optional DE/!RE pin for half-duplex transceivers
  time_id: sntp_time          # optional, enables boost persistence
  update_interval: 60s
```

### Entities

**Sensors**

| Key | Description |
|-----|-------------|
| `salt_ppm` | Salt level, ppm |
| `water_temp` | Water temperature (**reported in °F**) |
| `output_percent` | Cell-reported *actual* generation % |
| `set_percent` | Last commanded output % |
| `boost_remaining` | Minutes left in an active boost |
| `status` | Status byte from the cell |
| `error` | Raw alarm/fault bitmask (decode manually if needed) |

**Binary sensors (fault bits):** `no_flow`, `low_salt`, `very_low_salt`,
`high_current`, `clean`, `low_volts`, `low_temp`, `check_pcb`.

**Text sensors:** `version` (model string, e.g. `Intellichlor--40`),
`firmware_version` (cell firmware, e.g. `1.8`).

**Controls**

| Platform | Key | Description |
|----------|-----|-------------|
| `number` | `swg_percent` | Output setpoint, 0–100% (persisted across reboot/OTA) |
| `switch` | `takeover_mode` | ON = ESP drives the cell; OFF = passive monitor |
| `select` | `swg_boost` | Run at 100% for a fixed time: `Off / 6h / 12h / 24h / 48h` |
| `button` | `end_boost` | Cancel an active boost early |

> **Boost only takes effect while `takeover_mode` is ON.** With takeover off the cell
> is driven by the real controller; boost will log a warning and do nothing.

> **Fault bit caveat:** the `high_current` / `clean` bits follow the protocol spec,
> which diverges from one hardware capture. If a real "clean cell" condition lights up
> "High Current" (or vice versa), see [`AGENTS.md`](AGENTS.md) — the raw `error` sensor
> always carries the unmodified value.

See the [example](example.yaml) for the full entity block.


## Building & testing

There's no standalone firmware here — these are components compiled by ESPHome inside a
consuming project. To compile-check the whole set against this clone:

```bash
esphome config  compile-test.yaml
esphome compile compile-test.yaml
```

[`compile-test.yaml`](compile-test.yaml) wires all four components together exactly as
on the live board (single bus → `uart_splitter` → chlorinator + pump + sniffer) and is
a good reference for a combined setup.

---

## Protocol

The reverse-engineered RS-485 wire format (framing, DLE byte-stuffing, checksum, command
set, and status decoding) is documented in
[`docs/IntelliChlor_Protocol.md`](docs/IntelliChlor_Protocol.md). Implementation notes
and history for component authors live in [`AGENTS.md`](AGENTS.md).

---

## License

[GNU General Public License v3.0](LICENSE).

Not affiliated with or endorsed by Pentair. "IntelliChlor", "IntelliCenter", and
"EasyTouch" are trademarks of their respective owners.
