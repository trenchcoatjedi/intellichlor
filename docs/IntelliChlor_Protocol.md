# IntelliChlor / iChlor (SCG) RS-485 Protocol

Reverse-engineered byte-level specification for Pentair's salt chlorine generator (SCG) bus.

**Sources.** This spec is assembled from two independent Pentair controller firmwares that drive
the same physical IC40 cell, so the wire format is cross-validated:

- **IntelliChem** v1.080 — Atmel **ATmega128** (AVR), `ICHEM_v1.080.a90`. Small, fully traced;
  the authority for the **byte layer** (framing, stuffing, checksum, timing).
- **IntelliCenter** v3.008 — ARM/Linux C++/Qt app (`elf2`). The authority for the **command layer**
  (full message set, field semantics).

Each fact below is tagged with where it was confirmed. The cell is a **slave at address `0x50`**;
the controller is the **bus master**.

---

## 1. Physical layer

| Parameter | Value |
|---|---|
| Medium | RS-485, half-duplex, 2-wire |
| Baud / framing | **9600 8N1** (standard IntelliChlor rate) |
| Roles | Master transmits a full frame, then listens; the cell only answers when addressed |
| Cell address | **`0x50`** (fixed). Replies carry address **`0x00`** (to the controller) |

> Controller wiring: IntelliChem drives the cell on **USART1** (`ichlor_transaction` busy-waits on
> `UCSR1A & 0x40` = TX-complete); its USART0 is a *different* bus (§8). IntelliCenter uses a
> `SM485Port` serial state machine (§9). Both reach the same wire format below.

---

## 2. Frame format

Every frame, both directions, is DLE/STX … DLE/ETX framed:

```
10 02 | ADDR | CMD | DATA... | CKS | 10 03
└─┬─┘   └─┬─┘  └┬─┘  └──┬──┘   └┬┘   └─┬─┘
 DLE STX  addr  cmd   payload  cksum  DLE ETX
```

| Byte(s) | Name | Notes |
|---|---|---|
| `10 02` | Start | DLE (`0x10`) + STX (`0x02`). Literal, never stuffed. |
| `ADDR` | Address | `0x50` = to the cell; `0x00` = to the controller. |
| `CMD` | Command / message code | See §5. |
| `DATA` | Payload | Length depends on `CMD` (§5): commands 1 byte; replies 2 / 4 / 16 bytes. |
| `CKS` | Checksum | See §4. |
| `10 03` | End | DLE (`0x10`) + ETX (`0x03`). Literal, never stuffed. |

> IntelliChem builds the TX frame from an 8-byte flash template at `0x329d`:
> `10 02 50 00 00 FF 10 03` (`00 00 FF` = cmd/data/checksum placeholders). IntelliCenter builds
> frames in code (no template).

---

## 3. DLE byte-stuffing

`0x10` (DLE) marks framing, so any `0x10` appearing **inside the `ADDR/CMD/DATA/CKS` region** is
escaped:

- **Encode (TX):** a body `0x10` byte is sent as the two bytes **`10 00`**.
- **Decode (RX):** `10` followed by `00` inside the body → collapse to a single `10`. A `10`
  followed by `02`/`03` is real framing.
- The framing DLEs (`10 02`, `10 03`) are emitted/parsed literally — **not** stuffed.

> *(IntelliChem `ichlor_transaction` @ `0x90bb`: stuffs only cmd/data/checksum positions; RX
> collapses a `0x00` after a `0x10`.)*

---

## 4. Checksum

```
CKS = (sum of every byte from the leading DLE 0x10 through the last DATA byte) & 0xFF
    = (0x10 + 0x02 + ADDR + CMD + DATA...) & 0xFF
```

The leading `10 02` **are** included; the trailing `10 03` is **not**. Computed on **unstuffed**
bytes.

---

## 5. Message reference (all commands & responses)

`CMD` is frame offset 3. Receivers also filter on `ADDR` (offset 1). "Confirmed in" shows which
firmware this was read from.

| CMD | Name | Direction | Payload | Meaning | Confirmed in |
|---|---|---|---|---|---|
| `0x00` | **Probe** | ctlr → cell | 1 B (`0x00`) | Presence check; elicits `0x01` | IntelliChem |
| `0x01` | **Hello / Ack** | cell → ctlr | — | Presence ack / keepalive | IntelliCenter |
| `0x03` | **WhoAreYou** | cell → ctlr | 16 B | ASCII **model string** (§5.3) | IntelliCenter |
| `0x11` | **Set Output** | ctlr → cell | 1 B (`0..100`) | Set generation %; also triggers a salt/status reply | Both |
| `0x12` | **Data** | cell → ctlr | 2 B | salt + alarms (§5.1) | Both |
| `0x16` | **Status** | cell → ctlr | 4 B | temperature, output %, version (§5.2) | Both* |

\* IntelliCenter decodes `0x16` as Status (§5.2). The older IntelliChem AVR also accepts `0x16` as a
reply to its `0x11` poll but handles it with legacy per-length logic (treating a byte as a ×100 salt
value); **treat IntelliCenter's §5.2 decode as authoritative** and verify field offsets against a
real cell if you must support both.

`0x11` is the workhorse: setting the level and reading back salt/status are one transaction
(§6). To read without changing output, re-send `0x11` with the current percentage. The cell may also
emit `0x01`/`0x03` (Hello/WhoAreYou) as part of its discovery/keepalive handshake.

### 5.1 Data reply (`0x12`) — salt + alarms

```
10 02 00 12 <SALT> <STATUS> <CKS> 10 03
```

- **Salt (ppm)** = `SALT × 50`. *(IntelliChem displays `"Salt: %u ppm"`; IntelliCenter logs
  `[IntelliChlor] Rx Data salt ppm( %d ) Alarms( 0x%x )`.)*
- **STATUS** = alarm bitmask (§5.4).

### 5.2 Status reply (`0x16`) — temperature / output % / version

```
10 02 00 16 <TEMP> <PCT_OUT> <VER_MAJ> <VER_MIN> <CKS> 10 03
```

| Byte | Field |
|---|---|
| `[0]` `TEMP` | water temperature |
| `[1]` `PCT_OUT` | current generation output % |
| `[2]` `VER_MAJ` | firmware version, major |
| `[3]` `VER_MIN` | firmware version, minor |

*(IntelliCenter `ichlor_rx_status_0x16` @ `0x9745e8`, log
`Rx Status temperature( %d ) percent out( %d ) version( %d.%d )`.)*

### 5.3 WhoAreYou reply (`0x03`) — model string

```
10 02 00 03 <16 ASCII bytes> <CKS> 10 03
```

16-byte ASCII model string, e.g. **`Intellichlor--40`** — the trailing `--NN` is the cell size
(`40` = IC40, `60` = IC60, …). *(IntelliCenter `ichlor_rx_whoareyou_0x03` @ `0x972a9c`, log
`Rx whoareyou : %s`.)*

### 5.4 STATUS / alarm bitmask (in `0x12`)

| Bit | Value | Label |
|---|---|---|
| — | `0x00` | OK – No Errors |
| 0 | `0x01` | No Flow |
| 1 | `0x02` | Low Salt |
| 2 | `0x04` | Very Low Salt |
| 3 | `0x08` | High Current |
| 4 | `0x10` | Clean Cell |
| 5 | `0x20` | Low Voltage |
| 6 | `0x40` | Cold Water |

Multiple bits may be set. *(IntelliChem string table @ ~`0x0962`, shown as `"Status Code=%XH"`.)*

> **Note on "version / temperature / salt %":** version comes from `0x16` bytes `[2].[3]` and the
> `0x03` model string; temperature from `0x16` byte `[0]` (on IntelliChem it only surfaces as the
> `Cold Water` alarm bit). There is no "salt %": salt is **ppm** (`0x12` × 50). "Percent" is the
> commanded/actual **generation level** (`0x11` data / `0x16` byte `[1]`).

---

## 6. Transaction sequence & timing

A poll is a synchronous request/response *(IntelliChem `ichlor_transaction`)*:

1. Build the frame; set `CMD`/`DATA`; compute `CKS` (§4).
2. Transmit with DLE-stuffing (§3).
3. Wait for TX-complete (`UCSR1A & 0x40`), spin-timeout ≈ 3000 loops.
4. Switch to RX; read the reply (DLE-unstuffing), bounded poll.
5. Validate reply `CMD` (§5) **and** checksum.
6. **Retry once** on a bad/missing reply; two failures → cell offline
   (`"No response from Chlorinator / -- TIMEOUT --"`).

Practical master: poll every 1–5 s with `0x11` + desired output %; treat no-reply-after-1-retry as
offline.

---

## 7. Worked examples (hex, before stuffing)

Commands (`ADDR=0x50`):

| Action | Frame |
|---|---|
| Probe | `10 02 50 00 00 62 10 03` |
| Set 0 % | `10 02 50 11 00 73 10 03` |
| Set 20 % | `10 02 50 11 14 87 10 03` |
| Set 50 % | `10 02 50 11 32 A5 10 03` |
| Set 100 % | `10 02 50 11 64 D7 10 03` |

Replies (`ADDR=0x00`):

| Meaning | Frame |
|---|---|
| Probe ack (`0x01`) | `10 02 00 01 00 13 10 03` |
| Data: 3900 ppm, OK (`0x4E`×50) | `10 02 00 12 4E 00 72 10 03` |
| Data: 2550 ppm, Low Salt (`0x33`×50, `0x02`) | `10 02 00 12 33 02 59 10 03` |
| Status: 78°, 50 %, v1.8 | `10 02 00 16 4E 32 01 08 B1 10 03` |
| WhoAreYou: `Intellichlor--40` | `10 02 00 03 49 6E 74 65 6C 6C 69 63 68 6C 6F 72 2D 2D 34 30 <CKS> 10 03` |

`CKS(set 50 %) = (10+02+50+11+32) & FF = A5`. `CKS(status) = (10+02+00+16+4E+32+01+08) & FF = B1`.

---

## 8. The *other* IntelliChem bus (USART0) — not IntelliChlor

IntelliChem's USART0 speaks a different, non-DLE protocol (to an EasyTouch/IntelliTouch controller).
Parser: `usart0_controller_rx_parser` (`code:80ab`).

```
ED 8C | LEN(1..8) | DATA[LEN] | CKS | 8D     ; (LEN + sum(DATA) + CKS) & 0xFF == 0
```

A Pentair "A5" template (`FF 00 FF A5 00 10 00 01 01 92`) and an `Acu-Trol` string also exist in
flash. Not the salt cell — don't confuse the buses.

---

## 9. IntelliCenter lower-level 485 stack (architecture)

IntelliCenter parses bytes through a layered, Qt signal/slot stack (from `elf2` `.dynsym`):

```
QSerialPort bytes
 → SM485PortA / SM485PortB        two RS-485 ports; per-byte state machine assembles + de-stuffs
 → UartRxMessageT                 assembled frame struct
 → RS485CoreSingle::ReceivedPacket(DEST_PORT, UartRxMessageT)   validates, emits "packet ready"
 → PacketRouter                   demuxes by device, Qt-signals the handler:
      notify_to_ichlor  notify_to_ichem  notify_to_pump  notify_to_heater
      notify_to_remote  notify_to_keepalive  notify_to_discovery  notify_to_OTAControl …
 → ichlor_rx_dispatch (0x974724)  the §5 command switch
```

- **Two ports** (`SM485PortA`/`B`) mirror the AVR's two USARTs, but one `SM485Port` class frames
  **all** Pentair protocols multiplexed on the bus (IntelliChlor `10 02…10 03`, A5 `FF…A5`, pump,
  heater, IntelliChem, remotes, keepalive, discovery, OTA); `PacketRouter` demuxes by device.
- **Parsed-frame (PacketItem) offsets** used by handlers: `+0x01` source addr, `+0x03` command,
  `+0x88` payload, `+0x120` payload length, `+0x116` "has-command" flag.
- TX: handler → `PacketRouter::transmit_to_router` → `RS485CoreSingle::TxPacket` → `SM485Port`. All
  public 485 methods are **Qt signals** (`QMetaObject::activate`), so the concrete
  stuffing/checksum code lives in runtime-connected `SM485Port` slots and is not statically
  call-graph-reachable. `qChecksum` (Qt CRC-16) is linked but used for **OTA/sync**, not the IC40
  frame.
- **Wire format = §2–§4.** Driving the same physical IC40, IntelliCenter's on-wire framing,
  stuffing, and sum-checksum are necessarily the bytes traced from the IntelliChem AVR; it adds only
  the richer command layer (§5).

---

## 10. Firmware cross-reference

**IntelliChem AVR** — open `ghidra_project_annotated/` (ATmega128, word addresses):

| Function | Addr | Role |
|---|---|---|
| `ichlor_transaction` | `0x90bb` | Build/stuff/send `0x00`/`0x11`, receive+validate, return salt ppm |
| `ichlor_validate_response` | `0x91de` | Reply-code validity (`0x01`/`0x12`/`0x16`) |
| `ichlor_info_screen` | `0x3330` | "Chlorinator Info" UI: poll + show salt/status |
| `ichlor_control_poll` | `0x503a` | Operational poll / output control + detection |
| `usart1_rx_isr` / `usart1_tx_isr` | `0x76b6` / `0x7710` | Cell-bus byte I/O |
| `usart0_controller_rx_parser` | `0x80ab` | `ED 8C … 8D` parser (other bus, §8) |
| TX frame template | flash `0x329d` | `10 02 50 00 00 FF 10 03` |

**IntelliCenter ARM** — open `firmware_zoo/intellicenter_3008/ghidra_ic` (`elf2`):

| Function | Addr | Role |
|---|---|---|
| `ichlor_rx_dispatch` | `0x974724` | Command switch (`0x01`/`0x03`/`0x12`/`0x16`) |
| `ichlor_rx_data_0x12` | `0x974468` | salt + alarms |
| `ichlor_rx_status_0x16` | `0x9745e8` | temperature + output % + version |
| `ichlor_rx_whoareyou_0x03` | `0x972a9c` | 16-byte model string |
| `RS485CoreSingle::ReceivedPacket` / `TxPacket` | `0x9e8af0` / `0x9e8ab0` | frame in/out |
| `PacketRouter::*` | `0x9e74…` | device demux |

*Reverse-engineered from `ICHEM_v1.080.a90` (byte layer) and IntelliCenter `elf2` v3.008 (command
layer). Each row in §5 is tagged with its source.*
