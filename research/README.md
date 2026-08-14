# Captures and decoder

Logic-analyzer captures of the stock 6000S firmware talking to its appliance
MCU, plus the decoder used to turn them into the key map in
[../PROTOCOL.md](../PROTOCOL.md).

Captures were taken with a Saleae probe on the ESP32-C3's UART1 pins
(TX = GPIO19, RX = GPIO18) while driving the unit from its front panel and
from the VeSync app.

## Files

| File | What was happening |
|---|---|
| `6000s_mcu_uart_gpio18-19.*` | First confirmed capture on the correct pins — boot and idle |
| `6000s_mcu_uart_power_cycles.*` | Power on/off from the panel |
| `6000s_mcu_uart_modes.csv` | Cycling every mode |
| `6000s_mcu_uart_settings.csv` | Target humidity, fan speed, timer, child lock, display |
| `6000s_mcu_uart_filter_modes.csv` | Filter dry cycles and filter reset |
| `6000s_mcu_uart_float_switch.csv` | Lifting the tank and tripping the float switch |
| `6000s_mcu_uart_hall.csv` | Hall sensor / tank presence |
| `6000s_mcu_uart_breath.csv` | Breathing on the sensor to move humidity |
| `6000s_mcu_uart_app_commands.csv` | Commands issued from the VeSync app |
| `6000s_mcu_uart_isolated_commands.csv` | One command per capture window, to attribute keys |
| `*_decoded.txt` | `decode_full.py` output for the corresponding capture |

`.csv` files are Saleae Async Serial exports with two analyzers — `ch0` is the
ESP TX line, `ch1` is the MCU TX line. `.sal` files are the native Saleae
project for the same capture, where one exists.

## Decoding

```bash
uv run python decode_full.py 6000s_mcu_uart_modes.csv
```

It prints every checksum-valid `A5` frame in both directions, and for status
reports only the KLV keys that changed since the previous report. Keys that
change constantly (`0x09`, `0x0A`, `0x14`, `0x1E`) are filtered as noise —
edit `NOISY` in the script to see them.

## Watching a live device

Once the ESPHome component is running, the node itself is the faster loop: set
the logger to `DEBUG` and it prints `TX op=…` and `RX status: …` for every
frame, so a new opcode can be mapped over the air with the **Probe Command**
entity instead of re-capturing for each button press.

## Not included

Vendor firmware images, the Binary Ninja and Ghidra databases, and decompiler
output are derived from Levoit/VeSync copyrighted code and are not
redistributed here. `PROTOCOL.md` describes what was learned from them.
