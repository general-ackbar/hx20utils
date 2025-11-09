#!/usr/bin/env python3
"""
bit_time_tools.py
-----------------
Compute duration of a bitstream-encoded binary file where bit durations differ by value,
and map a time offset to byte/bit position (with optional hex dump).

Default timings (can be overridden via CLI flags):
- 1-bit (high): 1080 microseconds
- 0-bit (low):   545 microseconds

Assumes 8 bits per byte, no gaps. Bit order inside each byte is MSB-first for timing accumulation.
Reports both MSB-first and LSB-first bit indices for convenience.

# 1) Total varighed
python3 bit_time_tools.py <fil> duration [--us-one 1080] [--us-zero 545] [--details]

# 2) Find byte-/bit-position for et tids-offset (sekunder)
python3 bit_time_tools.py <fil> offset --time 12.0 [--us-one 1080] [--us-zero 545] [--dump] [--around 16]

# 3) Hexdump omkring et byte-offset
python3 bit_time_tools.py <fil> hexdump --offset 2000 [--length 16]




"""

import sys
import argparse
from pathlib import Path
from bisect import bisect_right
from typing import List, Tuple

def count_bits(data: bytes) -> Tuple[int, int]:
    ones = sum(b.bit_count() for b in data)
    total = len(data) * 8
    zeros = total - ones
    return ones, zeros

def per_byte_durations_us(data: bytes, us_one: int, us_zero: int) -> List[int]:
    """
    Returns a list of total microseconds contributed by each byte (sum over its 8 bits).
    MSB-first order is used for summation (which doesn't matter for per-byte totals).
    """
    per = []
    for b in data:
        ones = b.bit_count()
        zeros = 8 - ones
        per.append(ones * us_one + zeros * us_zero)
    return per

def cumulative_us_per_byte(data: bytes, us_one: int, us_zero: int) -> List[int]:
    """
    Prefix sum (length = len(data)+1) of microseconds up to each byte boundary.
    cum[0] = 0, cum[i] = total µs of bytes [0, i).
    """
    per = per_byte_durations_us(data, us_one, us_zero)
    cum = [0]
    s = 0
    for v in per:
        s += v
        cum.append(s)
    return cum

def byte_bit_at_time(data: bytes, t_seconds: float, us_one: int, us_zero: int):
    """
    Given a time offset in seconds, return a dict with:
      - byte_index (0-based, clamped to last byte if beyond end)
      - bit_index_msb (0..7) inside the byte if within file, else 7 for last bit
      - bit_index_lsb (0..7) convenience mapping of the same bit if read LSB-first
      - bit_value (0 or 1)
      - t_before_s (time just before this bit starts)
      - t_after_s (time right after this bit)
      - reached_end (True if the offset is at or beyond total duration)
    """
    if not data:
        raise ValueError("Empty data.")

    t_us = max(0, int(round(t_seconds * 1_000_000)))
    cum = cumulative_us_per_byte(data, us_one, us_zero)
    total_us = cum[-1]

    if t_us >= total_us:
        # Past end: point to the last bit
        last_byte_index = len(data) - 1
        # find last set bit index for timing? We treat it as bit 7 (last in MSB order)
        # We still compute its actual last bit by iterating that byte
        b = data[last_byte_index]
        # find duration within last byte to locate final bit endpoint
        t_before = cum[-2]  # time before last byte
        # iterate MSB-first through bits to find the final bit
        last_bit_start = t_before
        last_bit_idx_msb = 7
        for i in range(8):
            dur = us_one if ((b >> (7 - i)) & 1) else us_zero
            if i < 7:
                last_bit_start += dur
            else:
                last_bit_idx_msb = i
                last_bit_end = last_bit_start + dur
        return {
            "byte_index": last_byte_index,
            "bit_index_msb": last_bit_idx_msb,
            "bit_index_lsb": 7 - last_bit_idx_msb,
            "bit_value": (b >> (7 - last_bit_idx_msb)) & 1,
            "t_before_s": last_bit_start / 1_000_000.0,
            "t_after_s": last_bit_end / 1_000_000.0,
            "reached_end": True,
            "total_duration_s": total_us / 1_000_000.0,
        }

    # Find byte via prefix sums (cum is sorted)
    # We want largest i such that cum[i] <= t_us
    byte_index = bisect_right(cum, t_us) - 1
    if byte_index < 0:
        byte_index = 0
    if byte_index >= len(data):
        byte_index = len(data) - 1

    t_before = cum[byte_index]
    b = data[byte_index]

    # Walk bits within the byte (MSB-first) to find the bit that spans t_us
    cur = t_before
    chosen_bit = 0
    for i in range(8):
        bit_val = (b >> (7 - i)) & 1
        dur = us_one if bit_val else us_zero
        if t_us < cur + dur:
            chosen_bit = i
            t_after = cur + dur
            return {
                "byte_index": byte_index,
                "bit_index_msb": chosen_bit,
                "bit_index_lsb": 7 - chosen_bit,
                "bit_value": bit_val,
                "t_before_s": cur / 1_000_000.0,
                "t_after_s": t_after / 1_000_000.0,
                "reached_end": False,
                "total_duration_s": total_us / 1_000_000.0,
            }
        cur += dur

    # If we fall through (e.g., t_us == cum[byte_index+1]), pick the last bit of this byte
    return {
        "byte_index": byte_index,
        "bit_index_msb": 7,
        "bit_index_lsb": 0,
        "bit_value": (b & 1),
        "t_before_s": (cum[byte_index+1] - (us_one if (b & 1) else us_zero))/1_000_000.0,
        "t_after_s": cum[byte_index+1]/1_000_000.0,
        "reached_end": False,
        "total_duration_s": total_us / 1_000_000.0,
    }

def hexdump(data: bytes, start: int, length: int = 16) -> str:
    end = min(len(data), max(0, start) + length)
    start = max(0, start)
    slice_ = data[start:end]
    # Build classic hex+ascii dump without addresses (or include offset)
    hex_bytes = [f"{b:02X}" for b in slice_]
    # Insert a space every byte and an extra group space every 8
    hex_groups = []
    for i, hb in enumerate(hex_bytes):
        sep = " "
        hex_groups.append(hb)
        if (i + 1) % 8 == 0 and (i + 1) != len(hex_bytes):
            hex_groups.append(" ")
    hex_part = " ".join(hex_groups)
    ascii_part = "".join(chr(b) if 32 <= b <= 126 else "." for b in slice_)
    return f"@{start:08X}  {hex_part:<48}  |{ascii_part}|"

def main():
    p = argparse.ArgumentParser(description="Bitstream timing tools (variable duration per bit value).")
    p.add_argument("file", type=Path, help="Binary file (8 bits per byte, no gaps).")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_dur = sub.add_parser("duration", help="Compute total duration and bit counts.")
    p_dur.add_argument("--us-one", type=int, default=1080, help="Microseconds per '1' bit (default: 1080)")
    p_dur.add_argument("--us-zero", type=int, default=545, help="Microseconds per '0' bit (default: 545)")
    p_dur.add_argument("--details", action="store_true", help="Print ones/zeros and totals")

    p_off = sub.add_parser("offset", help="Map time offset (seconds) to byte/bit and optional hex dump.")
    p_off.add_argument("--time", "-t", type=float, required=True, help="Time offset in seconds (e.g., 12.0)")
    p_off.add_argument("--us-one", type=int, default=1080, help="Microseconds per '1' bit (default: 1080)")
    p_off.add_argument("--us-zero", type=int, default=545, help="Microseconds per '0' bit (default: 545)")
    p_off.add_argument("--dump", action="store_true", help="Also print a 16-byte hex dump around the byte")
    p_off.add_argument("--around", type=int, default=16, help="Hex dump length (default: 16)")

    p_hex = sub.add_parser("hexdump", help="Hex dump around a given byte offset.")
    p_hex.add_argument("--offset", "-o", type=int, required=True, help="Byte offset (0-based)")
    p_hex.add_argument("--length", "-n", type=int, default=16, help="Number of bytes to dump (default: 16)")

    args = p.parse_args()

    data = args.file.read_bytes()

    if args.cmd == "duration":
        ones, zeros = count_bits(data)
        total_bits = len(data) * 8
        dur_us = ones * args.us_one + zeros * args.us_zero
        dur_s = dur_us / 1_000_000.0
        print(f"File: {args.file}  ({len(data)} bytes, {total_bits} bits)")
        print(f"Duration: {dur_s:.6f} s")
        if args.details:
            print(f"1-bits: {ones:,}  (each {args.us_one} µs)")
            print(f"0-bits: {zeros:,}  (each {args.us_zero} µs)")
            m = int(dur_s // 60)
            s = dur_s - 60*m
            print(f"MM:SS.mmm: {m:02d}:{s:06.3f}")

    elif args.cmd == "offset":
        info = byte_bit_at_time(data, args.time, args.us_one, args.us_zero)
        bidx = info["byte_index"]
        print(f"Time: {args.time:.6f} s")
        print(f"Total duration: {info['total_duration_s']:.6f} s")
        print(f"Reached end: {info['reached_end']}")
        print(f"Byte offset: {bidx} (0x{bidx:08X})")
        print(f"Bit in byte (MSB-first 7..0): {info['bit_index_msb']}")
        print(f"Bit in byte (LSB-first 0..7): {info['bit_index_lsb']}")
        print(f"Bit value: {info['bit_value']}")
        print(f"T before: {info['t_before_s']:.6f} s")
        print(f"T after : {info['t_after_s']:.6f} s")
        if args.dump:
            # Center around the target byte if possible
            around = max(1, args.around)
            start = max(0, bidx - around//2)
            print(hexdump(data, start, around))

    elif args.cmd == "hexdump":
        print(hexdump(data, args.offset, args.length))

if __name__ == "__main__":
    main()
