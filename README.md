# ESPHome component for the Levoit Superior 6000S

Local control of the **Levoit Superior 6000S** (`LEH-S602S-WUS`) evaporative
humidifier from Home Assistant, with no VeSync cloud account and no internet
access required.

The stock unit puts an ESP32-C3 in front of the appliance MCU and talks to it
over a private UART protocol. This replaces the ESP firmware with ESPHome and
reimplements that protocol, so every function on the front panel — plus a few
the app doesn't expose — becomes a Home Assistant entity.

The protocol was recovered from firmware analysis and logic-analyzer captures
of the stock firmware. It is written up in [PROTOCOL.md](PROTOCOL.md), with the
raw captures and a decoder in [`research/`](research/).

## What you get

| Type | Entities |
|---|---|
| Sensor | Humidity, Temperature, Fan Speed, Filter Life %, Filter Rated Life |
| Binary sensor | Water Tank Missing, Low Water, Humidifying |
| Switch | Power, Display, Child Lock, Auto Dry on Power Off, Auto Dry on Water Shortage |
| Number | Target Humidity, Fan Speed, Timer |
| Select | Mode — Off / Manual / Sleep / Humidity / Auto / Auto Pro / Filter Dry |
| Button | Reset Filter, Filter Dry (High) |

Mode is a single non-optimistic entity: it always shows what the MCU last
reported, not what was last asked for.

## Hardware

| | |
|---|---|
| Wi-Fi module | ESP32-C3-SOLO-1 (FCC ID `2AC7Z-ESPC3SOLO`) |
| Appliance MCU | SinOne SC95F8616P (1T-8051), silkscreen `MCU1` |
| MCU link | UART1, 115200 8N1, **TX = GPIO19, RX = GPIO18** |
| Console | UART0 on GPIO21/GPIO20, header `J1` |

> **GPIO16/17 is a trap.** Other Levoit humidifiers (LV600S, and the `acvigue`
> component) use GPIO16/17 — those are classic ESP32 parts. On the ESP32-C3 the
> flash sits on GPIO12–17 and the SOLO-1 module doesn't bring 16/17 out at all.
> Configuring a UART there wedges the CPU against the flash bus it executes
> from. See [PROTOCOL.md](PROTOCOL.md).

GPIO18/19 are the C3's USB Serial/JTAG pads, so the component clears
`USB_SERIAL_JTAG_USB_PAD_ENABLE` in `setup()` before claiming them.

## Installation

You need physical access to the board to flash the first time — there is no
OTA path from stock firmware. Serial goes to header `J1` (UART0). Back up the
original flash before you overwrite it.

```bash
git clone https://github.com/alxgmpr/esphome-levoit-6000s
cd esphome-levoit-6000s/esphome
cp secrets.yaml.example secrets.yaml   # then edit it
esphome run levoit-6000s.yaml
```

`external_components` points at `components/` relative to the YAML, so the
config works in place with no extra setup. After the first flash, updates go
over the air.

## Poking at the protocol

The config exposes a **Probe Command** text entity that injects an arbitrary
MCU frame at runtime, so you can explore opcodes without recompiling. The value
is `<opcode_hex> <klv_hex>`:

```
5531          # request a status report
5000 010101   # power on
```

With the logger at `DEBUG`, the node prints every frame in both directions
(`TX op=…` / `RX status: …`) and warns on `MCU rejected opcode …`, which is
enough to map a new opcode without touching a logic analyzer.

## Status

Everything in the table above is confirmed working on a `LEH-S602S-WUS`
running this component. Unknowns that remain are noted in
[PROTOCOL.md](PROTOCOL.md) — chiefly a handful of status keys that change
without a known trigger.

Only the US model has been tested. Other regional variants very likely speak
the same protocol, but that is untested.

## Related

- [`acvigue/esphome-levoit-air-purifier`](https://github.com/acvigue/esphome-levoit-air-purifier)
  — the Levoit *air purifier* family. Same vendor, different framing and
  different pins; it will not drive a 6000S.

## Disclaimer

Reflashing the module voids the warranty and takes the unit off the VeSync app
for good. Nothing here is affiliated with, endorsed by, or supported by Levoit
or VeSync. No vendor firmware, disassembly, or decompiler output is
redistributed in this repository.

## License

[MIT](LICENSE).
