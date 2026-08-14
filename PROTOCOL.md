# Levoit Superior 6000S (LEH-S602S-WUS) — ESP↔MCU UART Protocol

Reverse-engineered from firmware analysis plus logic-analyzer captures of the
stock firmware. Raw captures and decoders are in `research/`.

## Hardware

| | |
|---|---|
| Wi-Fi module | **ESP32-C3-SOLO-1** (39-pin, FCC ID `2AC7Z-ESPC3SOLO`) |
| Appliance MCU | **SinOne SC95F8616P** — 1T-8051, 64 KB flash, touch-key peripheral, LQFP48, silkscreen `MCU1` |
| Link | **UART1, 115200 8N1**, no flow control |
| ESP pins | **TX = GPIO19, RX = GPIO18** (module pins 28 and 27) |
| Console | UART0 on GPIO21/GPIO20, broken out on header `J1` |

The 6000S is an **evaporative** humidifier — fan and pump, no ultrasonic mist.

### GPIO16/17 is a trap

Other Levoit humidifiers (LV600S, and the `acvigue` component) use GPIO16/17.
Those are **classic ESP32** devices, where flash sits on GPIO6–11 and 16/17 are
free. On the ESP32-C3 the flash moved to GPIO12–17, and the SOLO-1 module does
not bring 16/17 out at all — there is no pad. Configuring a UART there wedges
the CPU by contending with the flash bus it executes from.

GPIO18/19 are the C3's USB Serial/JTAG D−/D+ pads, so
`USB_SERIAL_JTAG_USB_PAD_ENABLE` must be cleared before use.

## Frame format

```
A5 | type | seq | len16_LE | cksum | payloadType[3] | result | TLV...
 0     1      2     3   4      5        6   7   8       9      10+
```

* `type` — `0x22` SEND, `0x12` ACK, `0x52` ERROR
* `len16` — little-endian, counts everything after byte 5
* `cksum` — `(255 - sum(all frame bytes except byte[5])) & 0xFF`
* `payloadType` — byte 6 is always `0x02` (device family = humidifier); bytes
  7–8 are the opcode, little-endian. Air purifiers use `0x01` here.
* Body is a leading result byte at **byte 9**, then `key, length, value`
  records starting at **byte 10**. This holds in both directions.

Get that offset wrong and the damage is not obvious: a walk that starts one
byte late re-synchronises after a few records, so most keys still decode and
only the first handful are silently corrupted. Check that the walk consumes the
frame *exactly*, and spot-check low-numbered keys against a known value.

Every SEND is answered with an ACK carrying the same payloadType. The MCU
reports state **unsolicited** on change; polling is optional.

## Commands (ESP → MCU)

| opcode | payload | meaning |
|---|---|---|
| `0x5000` | `k01=01/00` | power on/off |
| `0x500F` | `k01=01/00` | display on/off |
| `0x5018` | `k01..k04` | display/brightness config (sent at boot) |
| `0x5019` | `k01=<u32 LE seconds>` | timer; `0` cancels |
| `0x5140` | `k01=01/00` | child lock |
| `0x5531` | *(none)* | status request |
| `0x5532` | `k01=N` | mode: 1 Manual, 2 Sleep, 3 Humidity, 4 Auto |
| `0x5533` | `k01=1..9` | fan speed |
| `0x5536` | `k01=NN` | target humidity, raw percent |
| `0x5537` | `k01=N` / `k02=N` | `auto_preference` / `humidity_preference` |
| `0x5544` | *(empty body)* | reset filter |
| `0x5546` | `k01` / `k02` / `k03` | dry-on-power-off / run dry cycle (1 low, 2 high) / dry-on-water-shortage |

Example — fan speed 5:
`A5 22 11 07 00 8F 02 33 55 00 01 01 05`

### Modes, and why "Away" was wrong

The stock ESP firmware carries a mode-name pointer table at `.rodata`
`0x3C0F0518`, and the index into it *is* the wire mode value:

| value | name | notes |
|---|---|---|
| 0 | `auto` | never seen on the wire |
| 1 | `manual` | confirmed |
| 2 | `sleep` | confirmed |
| 3 | `humidity` | confirmed; target jumps to 75 |
| 4 | `autoPro` | confirmed; the auto family |
| 5 | `dry` | never reported in `0x0B` — detect via fan `0` |

Indices 1–3 match the wire exactly, which is what pins the mapping. **Mode 4 is
auto, not "Away"** — the firmware has no "away" humidifier mode at all, and its
two auto entry points are `enterAutoMode` and `enterAutoProMode`.

The app never sends mode 4 alone. One button press emits `0x5532 k01=04` and
then `0x5537 k01=<pref>` as a second frame 14–52 ms later:

| `auto_preference` | target | fan |
|---|---|---|
| 1 | 50 % | 1 |
| 2 | 55 % | 5 |

So the app's two auto buttons are one wire mode with two preferences, and
modelling them as separate "Away" and "Auto Preset" controls was a mistake.
`0x5537 k02` is `humidity_preference` (status `0x21`), which picks fan 9 vs
fan 5 within Humidity mode — the same trick, not yet exposed as an entity.

## Status report (MCU → ESP, `0x5530`)

| key | len | meaning |
|---|---|---|
| `0x02` | 1 | power |
| `0x03` | 1 | **water tank missing** (hall sensor) |
| `0x04` | 1 | **low water** (float switch on the pump harness) |
| `0x05` | 2 | display — `01 01` lit, `00 00` off, `01 00` unit powered off |
| `0x07` | 1 | humidifying — requires power AND mode≠0 AND tank present |
| `0x08` | 1 | target humidity, percent |
| `0x09` | 1 | **current humidity, %RH** |
| `0x0A` | 1 | temperature, whole °C (lags `0x1E`) |
| `0x0B` | 1 | **mode** — echoes the `0x5532` value |
| `0x0C` | 1 | **fan speed** 1–9; `0` while powered = filter-dry cycle |
| `0x13` | 1 | `auto_preference` (from `0x5537 k01`); parks at the fan speed outside mode 4 |
| `0x14` | 5 | timer: bytes[0:2] = seconds remaining, uint16 LE. Doubles as the filter-dry countdown (30 min cycle) |
| `0x15` | 1 | fan speed mirror |
| `0x16` | 1 | rated filter life, days (constant 120) |
| `0x17` | 1 | **filter life remaining, percent** — read 65, reset to 100 by `0x5544` |
| `0x18` | 1 | child lock |
| `0x1E` | 2 | **temperature ×10**, uint16 LE — always Celsius |
| `0x1F` | 1 | dry-on-water-shortage *setting* (not a sensor) |
| `0x21` | 1 | `humidity_preference` (from `0x5537 k02`) |

Unidentified: `0x00`, `0x01` (3 bytes), `0x06`, `0x11`, `0x20`, `0x27` (4 bytes).

Weekly cleaning reminder and temperature unit are **app/cloud settings** — they
generate no UART traffic and cannot be exposed through this link.

## Method notes

Sensor fields were identified by forcing a change and diffing consecutive
status frames:

* **humidity / temperature** — breathe on the sensor; `0x09` walked 39→95 and
  `0x1E` tracked 24.0→26.7 °C
* **tank presence** — wave a magnet at the hall sensor; `0x03` toggled over six
  cycles
* **low water** — *tape* the magnet so `0x03` is pinned, then disconnect the
  float switch; the unit starts, runs ~0.9 s, stops and raises `0x04`
* **commands** — drive the device from the VeSync app. Physical button presses
  are handled MCU-locally and only reported, so they never reveal command
  frames. Two full sessions of button-mashing produced zero commands.

### Wrong calls, and why

`0x1F` was initially read as low water because it was 1 in every tank-off
capture and 0 in every tank-on one. It is actually the dry-on-water-shortage
setting — it defaulted on after an NVS erase and the app pushed it off during
re-pairing, which coincided with fitting the tank. Two variables moved together
and the wrong one got the credit.

`0x0C` was initially read as a mode enum (1=Sleep, 5=Auto, 9=Manual) because
those modes happen to select fan speeds 1, 5 and 9. It is the fan speed.

Both errors came from inferring across captures instead of within one. Change
one variable, in one recording, and watch for the edge.

The opposite mistake is just as easy. `0x17` (filter life) was first written off
as "unchanged by a filter reset" because it was flat *within* the reset capture
— but the reset landed before that capture's first status frame, so the whole
recording was already post-reset. The 65→100 step was only visible by comparing
captures. Check both, every time.

A fourth kind of error has nothing to do with the captures. The ESPHome
component decoded the same frames correctly for humidity, temperature, mode and
filter life while starting its TLV walk one byte late, because the walk
re-aligns. The keys it destroyed were the low-numbered ones: `0x03` read a
constant `0x00` (so tank-missing could never fire), `0x04` and `0x05` were
skipped entirely in most frames, and `0x02` landed on the display byte, which
tracks power closely enough to look right. A decoder that is right about
fifteen fields can still be wrong about the first four.
