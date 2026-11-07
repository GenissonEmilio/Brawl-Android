"""Measure actual SurfaceFlinger presents and game-process PSS; no app data is read."""
import argparse
import json
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import time


def parse_latency(output):
    rows = output.splitlines()
    refresh = int(rows[0]) if rows and rows[0].isdigit() else None
    presents = set()
    for row in rows[1:]:
        fields = row.split()
        if len(fields) == 3 and all(f.isdigit() for f in fields):
            actual = int(fields[1])
            if 0 < actual < (1 << 63) - 1:
                presents.add(actual)
    return refresh, presents


def summarize(presents, baseline, elapsed):
    # Device timestamps are compared only with device timestamps. Rate uses the
    # complete host observation, including a frozen or idle beginning/end.
    times = sorted(t for t in presents if t > baseline)
    intervals = sorted((b - a) / 1e6 for a, b in zip(times, times[1:]))
    def percentile(fraction):
        return intervals[int((len(intervals) - 1) * fraction)]
    return {
        "observed_seconds": round(elapsed, 3),
        "present_count": len(times),
        "present_fps": round(len(times) / elapsed, 2),
        "interval_ms": ({"mean": round(statistics.mean(intervals), 3),
                         "p50": round(percentile(.5), 3), "p95": round(percentile(.95), 3),
                         "p99": round(percentile(.99), 3), "max": round(max(intervals), 3)}
                        if intervals else None),
        "intervals_over_25_ms_percent": (round(100 * sum(t > 25 for t in intervals) / len(intervals), 2)
                                         if intervals else None),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True, type=Path)
    parser.add_argument("--seconds", type=int, default=45)
    parser.add_argument("--package", default="com.genisson.brawl.android")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 300:
        parser.error("--seconds must be between 10 and 300")

    def shell(command):
        return subprocess.check_output([str(args.adb), "shell", command], encoding="utf-8", errors="replace")

    layers = shell("dumpsys SurfaceFlinger --list")
    match = re.search(r"RequestedLayerState\{([^\n]*SurfaceView\[" + re.escape(args.package)
                      + r"/[^\n]*\]\(BLAST\)#\d+) ", layers)
    if not match:
        raise SystemExit("Game SurfaceView is not active")
    layer = match.group(1)
    pid = shell("pidof " + shlex.quote(args.package + ":game")).strip()
    if not pid.isdigit():
        raise SystemExit("Game process is not active")
    latency_command = "dumpsys SurfaceFlinger --latency " + shlex.quote(layer)
    initial_refresh, initial = parse_latency(shell(latency_command))
    baseline = max(initial, default=0)
    presents, pss = set(), []
    refresh = {initial_refresh} if initial_refresh is not None else set()
    start = time.monotonic()
    next_memory = start
    while True:
        remaining = args.seconds - (time.monotonic() - start)
        if remaining <= 0:
            break
        time.sleep(min(0.55, remaining))
        period, samples = parse_latency(shell(latency_command))
        if period is not None:
            refresh.add(period)
        presents.update(t for t in samples if t > baseline)
        if time.monotonic() >= next_memory and time.monotonic() - start < args.seconds:
            memory = shell("dumpsys meminfo " + pid)
            found = re.search(r"TOTAL PSS:\s*(\d+)", memory)
            if found:
                pss.append(int(found.group(1)) / 1024)
            next_memory = time.monotonic() + 5
    elapsed = time.monotonic() - start
    result = {
        "metric": "SurfaceFlinger actual presents (not simulation FPS)",
        "requested_seconds": args.seconds,
        **summarize(presents, baseline, elapsed),
        "interval_scope": "between observed presentations only; boundary idle time is included in present_fps",
        "window_method": "initial device timestamp baseline; full host elapsed time including adb overhead",
        "refresh_period_ns": sorted(refresh),
        "game_pss_mib": {"samples": len(pss), "min": round(min(pss), 1),
                         "max": round(max(pss), 1), "last": round(pss[-1], 1)} if pss else None,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
