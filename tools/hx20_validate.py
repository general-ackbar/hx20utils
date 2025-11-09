#!/usr/bin/env python3
import wave
import numpy as np
import argparse
import sys

def read_wav(path):
    with wave.open(str(path),'rb') as w:
        rate=w.getframerate(); sw=w.getsampwidth(); ch=w.getnchannels()
        data = w.readframes(w.getnframes())
    if sw==2:
        x = np.frombuffer(data, np.int16).astype(np.float32)/32768.0
    elif sw==1:
        x = (np.frombuffer(data, np.uint8).astype(np.float32)-128.0)/128.0
    else:
        raise SystemExit(f'Unsupported sample width: {sw}')
    if ch==2: x = x.reshape(-1,2).mean(axis=1)
    return rate, x

def pick_edges(x, rate, which="rise", mingap_us=200.0):
    # Derivative-based edge pick with selectable polarity.
    print("Using mmingap_us value: ", mingap_us)
    
    dx = np.diff(x, prepend=x[0])
    thr_pos = np.percentile(dx, 99.0)
    thr_neg = np.percentile(dx, 1.0)
    idxs = []
    if which in ("rise","both"):
        idxs.append(np.where(dx>=thr_pos)[0])
    if which in ("fall","both"):
        idxs.append(np.where(dx<=thr_neg)[0])
    if not idxs:
        return np.array([], dtype=np.int64)
    idx = np.sort(np.concatenate(idxs))
    # De-duplicate edges closer than mingap_us
    mingap = int(rate * (mingap_us*1e-6))
    picks = []
    last = -10**9
    for k in idx:
        if k - last >= mingap:
            picks.append(k); last = k
    return np.array(picks, dtype=np.int64)

def choose_threshold_us(intervals_us):
    if len(intervals_us)<4:
        return None
    p30, p70 = np.percentile(intervals_us, [30,70])
    c0, c1 = float(p30), float(p70)
    for _ in range(30):
        m = np.abs(intervals_us-c0) <= np.abs(intervals_us-c1)
        if m.sum()==0 or m.sum()==len(m): break
        c0 = float(intervals_us[m].mean())
        c1 = float(intervals_us[~m].mean())
    return (c0+c1)/2.0, c0, c1

def bits_from_edges(edges, rate, thr_us):
    dt_us = np.diff(edges)/rate*1e6
    return (dt_us>=thr_us).astype(np.uint8), dt_us

def decode_bytes(bits, off):
    out=[]; stops_ok=0; total=0
    for i in range(off, len(bits)-9+1, 9):
        bb = bits[i:i+9]
        data = bb[:8]; stop = bb[8]
        total+=1; stops_ok += int(stop==1)
        v=0
        for j,b in enumerate(data):
            if b: v|=(1<<j)
        out.append(v)
    return np.array(out,np.uint8), stops_ok, total

def find_preamble(byte_stream):
    arr = byte_stream
    idxs = np.where((arr[:-1]==0xFF) & (arr[1:]==0xAA))[0]
    return int(idxs[0]) if len(idxs) else None

def main():
    ap = argparse.ArgumentParser(description="HX-20 WAV validator (edge interval + FF AA preamble finder)")
    ap.add_argument("wav", help="input WAV file")
    ap.add_argument("--invert", action="store_true", help="invert signal before edge detection")
    ap.add_argument("--edges", choices=["rise","fall","both"], default="rise", help="which edges to detect (default: rise)")
    ap.add_argument("--mingap-us", type=float, default=200.0, help="minimum gap between edges in microseconds (default: 200)")
    ap.add_argument("--plot", action="store_true", help="plot a histogram of interval lengths (single chart)")
    args = ap.parse_args()
    rate, x = read_wav(args.wav)
    if args.invert:
        x = -x
    edges = pick_edges(x, rate, which=args.edges, mingap_us=args.mingap_us)
    if len(edges)<3:
        print("ERROR: too few edges detected.")
        sys.exit(2)
    dt_us = np.diff(edges)/rate*1e6
    choice = choose_threshold_us(dt_us)
    if choice is None:
        print("ERROR: could not estimate threshold.")
        sys.exit(3)
    thr, c0, c1 = choice
    bits, _ = bits_from_edges(edges, rate, thr)
    print(f"SampleRate: {rate} Hz  Duration: {len(x)/rate:.3f} s  Edges: {len(edges)}")
    print(f"Intervals: short≈{c0:.1f} µs  long≈{c1:.1f} µs  thr≈{thr:.1f} µs")
    best = None
    for off in range(9):
        bytes_arr, stops_ok, total = decode_bytes(bits, off)
        pos = find_preamble(bytes_arr)
        if pos is not None:
            best = (off, pos, stops_ok, total, bytes_arr)
            break
    if best is None:
        print("Preamble FF AA not found with current settings.")
        print("Tips: try --invert, --edges=fall|both, or tweak --mingap-us (e.g., 120..400).")
        if args.plot:
            import matplotlib.pyplot as plt
            plt.hist(dt_us, bins=200)
            plt.title("Edge interval histogram (µs)")
            plt.xlabel("µs"); plt.ylabel("count")
            plt.show()
        sys.exit(1)
    off, pos, stops_ok, total, arr = best
    print(f"Found FF AA at byte index {pos} (alignment offset {off}); stop-bit OK ratio: {stops_ok}/{total}")
    preview = arr[pos:pos+40]
    print("Preview bytes after preamble (first 40):")
    print(' '.join(f'{b:02X}' for b in preview))
    print("ASCII:", "".join(chr(b) if 32<=b<127 else "." for b in preview))
    if args.plot:
        import matplotlib.pyplot as plt
        plt.hist(dt_us, bins=200)
        plt.title("Edge interval histogram (µs)")
        plt.xlabel("µs"); plt.ylabel("count")
        plt.show()

if __name__ == "__main__":
    main()
