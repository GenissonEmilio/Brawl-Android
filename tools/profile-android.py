"""Measure actual SurfaceFlinger presents and game-process PSS; no app data is read."""
import argparse
import json
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import time


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
    presents, pss, refresh = set(), [], set()
    start = time.monotonic()
    next_memory = start
    while time.monotonic() - start < args.seconds:
        rows = shell("dumpsys SurfaceFlinger --latency " + shlex.quote(layer)).splitlines()
        if rows and rows[0].isdigit():
            refresh.add(int(rows[0]))
        for row in rows[1:]:
            fields = row.split()
            if len(fields) == 3 and all(f.isdigit() for f in fields):
                actual = int(fields[1])  # desiredPresentTime, actualPresentTime, frameReadyTime
                if 0 < actual < (1 << 63) - 1:
                    presents.add(actual)
        if time.monotonic() >= next_memory:
            memory = shell("dumpsys meminfo " + pid)
            found = re.search(r"TOTAL PSS:\s*(\d+)", memory)
            if found:
                pss.append(int(found.group(1)) / 1024)
            next_memory = time.monotonic() + 5
        time.sleep(0.55)
    times = sorted(presents)
    # Remove the pre-existing ring's samples from before the requested observation period.
    if times:
        cutoff = times[-1] - args.seconds * 1_000_000_000
        times = [t for t in times if t >= cutoff]
    if len(times) < 3:
        raise SystemExit("SurfaceFlinger provided insufficient frame timestamps")
    intervals = sorted((b - a) / 1e6 for a, b in zip(times, times[1:]))
    def percentile(fraction):
        return intervals[min(len(intervals) - 1, int((len(intervals) - 1) * fraction))]
    result = {
        "metric": "SurfaceFlinger actual presents (not simulation FPS)",
        "requested_seconds": args.seconds,
        "observed_seconds": round((times[-1] - times[0]) / 1e9, 3),
        "present_count": len(times),
        "present_fps": round((len(times) - 1) * 1e9 / (times[-1] - times[0]), 2),
        "interval_ms": {"mean": round(statistics.mean(intervals), 3),
                        "p50": round(percentile(.5), 3), "p95": round(percentile(.95), 3),
                        "p99": round(percentile(.99), 3), "max": round(max(intervals), 3)},
        "intervals_over_25_ms_percent": round(100 * sum(t > 25 for t in intervals) / len(intervals), 2),
        "refresh_period_ns": sorted(refresh),
        "game_pss_mib": {"samples": len(pss), "min": round(min(pss), 1),
                         "max": round(max(pss), 1), "last": round(pss[-1], 1)} if pss else None,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
