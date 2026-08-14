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

Every SEND is answered with an ACK carrying the same payloadType. Most ACKs are
a bare `len=10` with no body — but the ACK to a status request (`0x5531`) is
`len=96` and carries a **full status body**, laid out exactly like a `0x5530`
report (result byte at 9, TLVs from 10). So a poll gets its answer in the ACK
itself, not in a follow-up report:

```
ESP->MCU SEND seq=2 payloadType=0x023155 len=10
MCU->ESP ACK  seq=2 payloadType=0x023155 len=96   <- status body here
```

The MCU also reports state **unsolicited** on change, so polling is optional.

The stock ESP echoes the MCU's `seq` in its ACKs.

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
| anything else | *reports* the stored target, but hunts — see below | unstable |

So the app's two auto buttons are one wire mode with two preferences, and
modelling them as separate "Away" and "Auto Preset" controls was a mistake.
`0x5537 k02` is `humidity_preference` (status `0x21`), which picks fan 9 vs
fan 5 within Humidity mode — the same trick, not yet exposed as an entity.

**Mode 4 owns the target humidity.** `0x5536` sent while in mode 4 is accepted
and remembered, but `0x08` keeps reporting the preference's own target for as
long as mode 4 is active. Measured: in Humidity mode, setting 65 sticks at 65;
in Auto the same write reads back 50, and 55 on preference 2 — then switching to
Humidity brings the stored 65 back. So a target written from Auto is only
meaningful if you also leave Auto.

### Preference 3 looks like a custom auto target. It is not one.

The MCU accepts `k01` 0–9 and `0x13` echoes whatever it is given. Any value
outside 1–2 makes `0x08` report the user's stored target, which reads exactly
like "Auto with a setpoint you choose":

```
set target 45 in Humidity mode   0x0B=3  0x08=45
enter Auto (k01=1)               0x0B=4  0x08=50   preset overrides
send 0x5537 k01=3                0x0B=4  0x08=45   reported target follows
```

**The reported target follows; the control loop does not.** Measured back to
back at target 62 with RH 56–59:

| state | fan |
|---|---|
| mode 4, preference 3 | cycles `9 ↔ 0` every ~12 s, indefinitely |
| mode 3 (Humidity), same target | `5`, steady, zero transitions in 2+ min |

In the cycling state `0x07`, `0x09`, `0x0C` and `0x1E` all flip together — the
temperature swings 21.3 ↔ 21.9 °C with the fan, which is airflow over the
sensor, and `0x03`/`0x04` never move, so it is not a water cycle. The unit is
simply hunting: it never settles even though RH sits 5–6 % below the reported
target.

So preference 3 is a reporting artefact, not a feature. Treat 1 and 2 as the
only usable preferences, and use Humidity mode for a user-chosen target.

This also explains the stock capture showing mode 4 with `0x13=9` at target 55:
55 was that unit's stored target, not a third preset.

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
| `0x0C` | 1 | **actual fan** 0–9; `0` whenever the unit is idle, which is the resting state of Auto once RH reaches target |
| `0x13` | 1 | `auto_preference` (from `0x5537 k01`); only believable when it reads 1 or 2 — a stock capture caught it holding `9` in Manual |
| `0x14` | 5 | **not** the user timer — see below. bytes[0:2] = uint16 LE **filter-dry countdown**, byte[2] = dry-on-power-off setting, byte[3] = `1` while a dry cycle runs, `2` otherwise |
| `0x15` | 1 | fan **setpoint** — what was asked for, vs `0x0C` which is what the fan is doing |
| `0x16` | 1 | rated filter life, days (constant 120) |
| `0x17` | 1 | **filter life remaining, percent** — read 65, reset to 100 by `0x5544` |
| `0x18` | 1 | child lock |
| `0x1E` | 2 | **temperature ×10**, uint16 LE — always Celsius |
| `0x1F` | 1 | dry-on-water-shortage *setting* (not a sensor) |
| `0x21` | 1 | `humidity_preference` (from `0x5537 k02`) |

Unidentified: `0x00`, `0x01` (3 bytes), `0x06`, `0x11`, `0x20`, `0x27` (4 bytes).

### `0x14`'s counter freezes; it does not free-run

Measured across a full cycle on the live device. It is a 1 Hz countdown from
7200 (2 h) that ticks **only** while byte[3] is `1`, and freezes wherever it
stopped:

```
12:45:49 .. 12:50:49   counter=1792   byte[3]=02   flat for 5 min
12:50:56 dry starts    counter=7199   byte[3]=01
12:51:11               counter=7184   byte[3]=01   -15 over 15 s
12:51:25               counter=7170   byte[3]=01   -14 over 14 s
12:51:40               counter=7155   byte[3]=01   -15 over 15 s
12:51:47 mode change   counter=7149   byte[3]=02   cycle ends
12:51:57 .. 12:52:01   counter=7149   byte[3]=02   frozen
```

So the value outside a cycle is a leftover from the last one and means nothing.
Selecting any mode ends a running cycle early.

### Fan speed: nine values, three levels

The MCU accepts and echoes 1–9 verbatim (swept every step and read back on
`0x0C`/`0x15`), but the hardware only has three. Metered: 1–3 low, 4–6 medium,
7–9 high. The app's nine-position slider is cosmetic. Use 1/5/9.

`0x5533` is **ignored outside Manual** — sent in Auto Pro, the fan did not move
and `0x0B` stayed 4.

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

`0x14` was published as a user-facing "Timer Remaining" sensor, where it read a
flat 2 h forever. It does not react to `0x5019` at all — setting *and*
cancelling a timer left the counter running straight through it. The user timer
is write-only from this link's point of view; the only genuinely useful field in
`0x14` is byte[3], the filter-dry flag.

`0x15` was read as a mirror of `0x0C` because in Manual the two always agree.
They are different quantities: `0x15` is the setpoint, `0x0C` is what the fan is
actually doing. In `6000s_mcu_uart_modes.csv`, `0x0C` walks `01→09→00→01` across
mode changes while `0x15` never moves; the two only step together at t=32–43,
where the fan speed is being set by hand.

Filter-dry detection was keyed off "fan is 0", on the reasoning that the MCU
reports fan 0 rather than mode 5 during a dry cycle. Fan 0 only means the output
is idle, which is also the normal resting state in Auto once RH reaches target —
so the device announced "Filter Dry" every time Auto was satisfied. The same
pattern is in the stock capture (`t=7.749`, `0x0C 09→00` with mode still 4).
`0x14` byte[3] is the only reliable indicator.

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
