#!/usr/bin/env python3
"""
FGN optimal-linear forecast daemon for the beam-hopping scheduler.

Protocol (line-based, stdin → stdout):
  Input  line: beam_id,s0,s1,...,s23   (beam ID followed by 24 samples)
  Output line: single float — the 1-step-ahead forecast

Usage:
  python3 fgn.py --hurst55=0.9 --hurst56=0.75 --hurst57=0.8

No normalisation is applied. Raw byte counts are used directly.
"""

import sys
import re
import argparse
import numpy as np
import scipy.linalg


WINDOW = 24


def fgn_autocov(k: int, H: float) -> float:
    k = abs(k)
    return 0.5 * (abs(k - 1) ** (2 * H) - 2 * k ** (2 * H) + (k + 1) ** (2 * H))


def build_optimal_weights(H: float, T: int) -> np.ndarray:
    cov_row = np.array([fgn_autocov(k, H) for k in range(T)])
    cov_matrix = scipy.linalg.toeplitz(cov_row)
    cross_cov = np.array([fgn_autocov(1 + k, H) for k in range(T)])
    return np.linalg.solve(cov_matrix, cross_cov)


def main():
    # Accept --hurstNN=<value> for any beam ID NN
    parser = argparse.ArgumentParser(description="FGN 1-step-ahead forecast daemon")
    args, unknown = parser.parse_known_args()

    beam_weights: dict[int, np.ndarray] = {}
    hurst_pattern = re.compile(r"^--hurst(\d+)=([\d.]+)$")
    for arg in unknown:
        m = hurst_pattern.match(arg)
        if m:
            beam_id = int(m.group(1))
            H = float(m.group(2))
            if not (0.0 < H < 1.0):
                sys.stderr.write(f"[fgn.py] WARNING: beam {beam_id} H={H} outside (0,1)\n")
            beam_weights[beam_id] = build_optimal_weights(H, WINDOW)
            sys.stderr.write(f"[fgn.py] beam {beam_id}: H={H}  weights precomputed\n")

    if not beam_weights:
        sys.stderr.write("[fgn.py] ERROR: no --hurstNN arguments provided\n")
        sys.exit(1)

    sys.stderr.write(f"[fgn.py] daemon ready  beams={sorted(beam_weights)}  window={WINDOW}\n")
    sys.stderr.flush()

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            print("0.0", flush=True)
            continue

        try:
            fields = [x.strip() for x in line.split(',') if x.strip()]
            beam_id = int(fields[0])
            values = np.array([float(x) for x in fields[1:]])
        except (ValueError, IndexError):
            print("0.0", flush=True)
            continue

        if beam_id not in beam_weights:
            # Unknown beam — fall back to mean
            result = float(np.mean(values)) if len(values) > 0 else 0.0
            print(f"{result:.6f}", flush=True)
            continue

        n = len(values)
        if n == 0:
            print("0.0", flush=True)
            continue

        if n < WINDOW:
            result = float(np.mean(values))
        else:
            window = values[-WINDOW:][::-1]
            mean_val = float(np.mean(window))
            # Subtract mean before applying weights (optimal predictor assumes zero-mean),
            # then add it back. Without this, sum(w)<1 for finite T causes systematic
            # undershoot proportional to (1 - sum(w)) * mean.
            result = max(0.0, mean_val + float(np.dot(beam_weights[beam_id], window - mean_val)))

        print(f"{result:.6f}", flush=True)


if __name__ == "__main__":
    main()
