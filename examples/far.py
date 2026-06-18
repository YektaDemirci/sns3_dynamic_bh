#!/usr/bin/env python3
"""
ARFIMA(2,0,0) forecast daemon for the beam-hopping scheduler.

Protocol (line-based, stdin → stdout):
  Input  line: comma-separated floats, e.g. "0.0,1200.0,4400.0,..."
  Output line: single float — the 1-step-ahead forecast, e.g. "3210.5"

One input line → one output line. stdout is flushed after every reply.
Runs indefinitely until stdin closes.

Uses a single persistent R subprocess (loaded once at startup) so that the
arfima package is only initialised once, keeping per-call latency to ~20ms
instead of ~1s per call.
"""

import sys
import os
import subprocess
import numpy as np

LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "far_log.csv")
_log_file = None

def _get_log(n_samples: int):
    global _log_file
    if _log_file is None:
        _log_file = open(LOG_PATH, "w", buffering=1)
        header = ",".join(f"t-{n_samples - i}" for i in range(n_samples)) + ",forecast"
        _log_file.write(header + "\n")
    return _log_file

def _log(values: np.ndarray, forecast: float):
    f = _get_log(len(values))
    row = ",".join(f"{v:.4f}" for v in values) + f",{forecast:.4f}"
    f.write(row + "\n")


# ── Persistent R subprocess ────────────────────────────────────────────────
# R reads one CSV line from stdin, fits ARFIMA(2,0,0), writes the forecast,
# then waits for the next line.  Loading R + arfima happens once at startup.

_R_DAEMON_CODE = r"""
suppressPackageStartupMessages(library(arfima))
repeat {
  line <- readLines(con=stdin(), n=1L)
  if (length(line) == 0) break
  line <- trimws(line)
  if (nchar(line) == 0) { cat("0\n"); flush(stdout()); next }

  vals     <- suppressWarnings(as.numeric(strsplit(line, ",")[[1]]))
  vals     <- vals[!is.na(vals)]
  mean_val <- mean(vals)

  if (length(vals) < 3) {
    cat(mean_val, "\n"); flush(stdout()); next
  }

  # Normalise by mean only (not std). For LRD traffic the sample variance from
  # a short window is a noisy estimator (Var(x_bar) ~ n^(2d-1) not 1/n), so
  # dividing by std introduces a bias that does not cancel on unscaling.
  # Dividing by mean gives O(1) values for numerical stability and the mean
  # estimation error cancels to second order when the forecast is unscaled.
  scaled <- vals / mean_val
  result <- tryCatch({
    fit  <- arfima(scaled, order=c(2L, 0L, 0L))
    pred <- predict(fit, n.ahead=1L)
    fc   <- pred[[1]]$Forecast[1]
    max(0.0, fc * mean_val)
  }, error = function(e) mean_val)

  cat(result, "\n")
  flush(stdout())
}
"""

_r_proc = None

def _ensure_r():
    global _r_proc
    if _r_proc is not None and _r_proc.poll() is None:
        return _r_proc
    _r_proc = subprocess.Popen(
        ["Rscript", "--vanilla", "-"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    # Feed the daemon script and flush it — R will block on readLines()
    _r_proc.stdin.write(_R_DAEMON_CODE)
    _r_proc.stdin.flush()
    return _r_proc


def arfima_forecast(values: np.ndarray) -> float:
    mean_val = float(np.mean(values))

    csv = ",".join(repr(float(v)) for v in values)
    try:
        proc = _ensure_r()
        proc.stdin.write(csv + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline().strip()
        # arfima prints progress notes before the result — take the last numeric token
        tokens = [t for t in line.split() if t.replace('.','',1).replace('-','',1).replace('e','',1).replace('+','',1).lstrip('-').replace('.','',1).isdigit() or _is_numeric(t)]
        if tokens:
            return max(0.0, float(tokens[-1]))
        # if nothing parsed, drain until we get a plain number
        for _ in range(20):
            line = proc.stdout.readline().strip()
            if _is_numeric(line):
                return max(0.0, float(line))
    except Exception as e:
        sys.stderr.write(f"[far.py] R error: {e}\n")
        sys.stderr.flush()
    return mean_val


def _is_numeric(s: str) -> bool:
    try:
        float(s)
        return True
    except (ValueError, TypeError):
        return False


def main():
    _ensure_r()
    sys.stderr.write("[far.py] daemon ready (persistent R session)\n")
    sys.stderr.flush()

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            print("0.0", flush=True)
            continue

        try:
            values = np.array([float(x) for x in line.split(',') if x.strip()])
        except ValueError:
            print("0.0", flush=True)
            continue

        if len(values) < 3:
            result = float(np.mean(values)) if len(values) > 0 else 0.0
        else:
            result = arfima_forecast(values)

        _log(values, result)
        print(f"{result:.6f}", flush=True)


if __name__ == "__main__":
    main()
