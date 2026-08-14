#!/usr/bin/env python3
"""Decode a Saleae Async Serial CSV export of the Levoit 6000S ESP<->MCU UART.

Expects two analyzers named so ch0 is the ESP TX line and ch1 the MCU TX line.
Prints every command frame and, for status reports, only the keys that changed.

    uv run --with nothing python decode_full.py <capture.csv>
"""
import csv, collections, sys

TYPES = {0x22: 'SEND', 0x12: 'ACK', 0x52: 'ERROR'}
# keys that change constantly and would drown out real state transitions
NOISY = {0x09, 0x0A, 0x1E, 0x14}


def frames(samples):
    """Yield (timestamp, raw_frame) for every checksum-valid A5 frame."""
    out, i = [], 0
    while i < len(samples):
        if samples[i][1] != 0xA5:
            i += 1
            continue
        if i + 6 > len(samples):
            break
        hdr = [x[1] for x in samples[i:i + 6]]
        total = 6 + (hdr[3] | (hdr[4] << 8))
        if i + total > len(samples):
            break
        f = [x[1] for x in samples[i:i + total]]
        if (255 - (sum(f) - f[5])) & 0xFF == f[5]:
            out.append((samples[i][0], f))
        i += total
    return out


def tlv(body):
    """Body is a leading result byte then key/length/value records."""
    d, i = {}, 1
    while i + 1 < len(body):
        k, ln = body[i], body[i + 1]
        if i + 2 + ln > len(body):
            break
        d[k] = bytes(body[i + 2:i + 2 + ln])
        i += 2 + ln
    return d


def main(path):
    channels = collections.defaultdict(list)
    for row in csv.DictReader(open(path)):
        if row['type'] != 'data':
            continue
        channels[row['name']].append((float(row['start_time']), int(row['data'], 16)))

    events = []
    for name, samples in channels.items():
        direction = 'ESP->MCU' if name.startswith('ch0') else 'MCU->ESP'
        for t, f in frames(samples):
            events.append((t, direction, f[1], bytes(f[6:9]).hex().upper(), f))
    events.sort(key=lambda e: e[0])

    ncmd = sum(1 for e in events if e[1] == 'ESP->MCU' and e[2] == 0x22)
    print(f"{len(events)} frames, {ncmd} commands\n")

    prev = None
    for t, direction, mtype, ptype, f in events:
        if direction == 'ESP->MCU' and mtype == 0x22:
            kv = " ".join(f"k{k:02X}={v.hex().upper()}" for k, v in tlv(f[9:]).items())
            raw = " ".join(f"{x:02X}" for x in f)
            print(f"\n>>> t={t:8.3f} CMD 0x{ptype}  {kv}\n    raw: {raw}")
        elif direction == 'MCU->ESP' and mtype == 0x22 and ptype == '023055':
            cur = tlv(f[9:])
            if prev is not None:
                changed = [(k, prev.get(k), v) for k, v in sorted(cur.items())
                           if prev.get(k) != v and k not in NOISY]
                if changed:
                    print(f"    t={t:8.3f} status: " + "  ".join(
                        f"0x{k:02X}:{o.hex().upper() if o else '--'}->{n.hex().upper()}"
                        for k, o, n in changed))
            prev = cur


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
