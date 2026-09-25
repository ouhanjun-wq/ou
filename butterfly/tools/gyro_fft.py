#!/usr/bin/env python3
"""Capture gyro data from the butterfly over USB and plot its spectrum.

  pip install pyserial numpy matplotlib

  # 1) record 20 s while the butterfly flaps on the bench ("bench 0.6" in the CLI)
  python tools/gyro_fft.py capture --port COM5 --mode fft --seconds 20 --out fft.csv
  # 2) plot: before vs after the flap-synchronous notches, markers at f, 2f, 3f
  python tools/gyro_fft.py plot fft.csv --out fft.png

Modes: "fft" = 200 Hz decimated vs notched gyro (look at 0-100 Hz, the flapping band),
       "raw" = 1 kHz unfiltered gyro (look at servo / gear noise up to 500 Hz).
"""
import argparse
import sys
import time


def capture(args):
    import serial

    with serial.Serial(args.port, 115200, timeout=0.1) as ser:
        ser.write(b"log off\n")
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(f"log {args.mode}\n".encode())
        end = time.time() + args.seconds
        n = 0
        with open(args.out, "w") as f:
            while time.time() < end:
                line = ser.readline().decode(errors="ignore").strip()
                if line[:2] in ("F,", "R,", "# "):
                    f.write(line + "\n")
                    n += 1
        ser.write(b"log off\n")
    print(f"wrote {n} lines to {args.out}")


def load(path):
    import numpy as np

    rows, kind = [], None
    with open(path) as f:
        for line in f:
            if line.startswith("#") or len(line) < 3:
                continue
            parts = line.strip().split(",")
            kind = parts[0]
            try:
                rows.append([float(x) for x in parts[1:]])
            except ValueError:
                continue
    if not rows:
        sys.exit("no data rows found")
    width = min(len(r) for r in rows)
    data = np.array([r[:width] for r in rows])
    t = (data[:, 0] - data[0, 0]) * 1e-6          # t_us -> s
    fs = 1.0 / np.median(np.diff(t))
    return kind, data, fs


def psd(x, fs, nseg=1024):
    """Welch-style averaged, Hann-windowed amplitude spectrum."""
    import numpy as np

    x = x - np.mean(x)
    nseg = min(nseg, len(x))
    win = np.hanning(nseg)
    step = nseg // 2
    specs = [np.abs(np.fft.rfft(x[i:i + nseg] * win)) for i in range(0, len(x) - nseg + 1, step)]
    amp = np.mean(specs, axis=0) * 2 / win.sum()
    return np.fft.rfftfreq(nseg, 1 / fs), amp


def plot(args):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    kind, data, fs = load(args.csv)
    names = ["roll rate p", "pitch rate q", "yaw rate r"]
    fig, axes = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    flap = None
    if kind == "F" and data.shape[1] >= 8:
        flap = float(np.median(data[:, 7]))
    for k, ax in enumerate(axes):
        f, a = psd(data[:, 1 + k], fs)
        ax.semilogy(f, a, label="before (decimated)" if kind == "F" else "raw 1 kHz", lw=1)
        if kind == "F":
            f2, a2 = psd(data[:, 4 + k], fs)
            ax.semilogy(f2, a2, label="after notch f, 2f", lw=1)
        if flap and flap > 0.5:
            for h in (1, 2, 3):
                ax.axvline(h * flap, color="gray", ls="--", lw=0.8)
                ax.text(h * flap, ax.get_ylim()[1], f" {h}f", va="top", fontsize=8, color="gray")
        ax.set_ylabel(f"{names[k]} [deg/s]")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("frequency [Hz]")
    title = f"gyro spectrum, fs = {fs:.0f} Hz"
    if flap:
        title += f", flapping f = {flap:.2f} Hz"
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(args.out, dpi=120)
    print(f"saved {args.out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--port", required=True)
    c.add_argument("--mode", choices=["fft", "raw"], default="fft")
    c.add_argument("--seconds", type=float, default=20)
    c.add_argument("--out", default="gyro.csv")
    p = sub.add_parser("plot")
    p.add_argument("csv")
    p.add_argument("--out", default="gyro_fft.png")
    args = ap.parse_args()
    capture(args) if args.cmd == "capture" else plot(args)


if __name__ == "__main__":
    main()
