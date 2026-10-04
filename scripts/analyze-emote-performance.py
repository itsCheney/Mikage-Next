"""Summarize matched Emote diagnostic windows; this does not measure power.

Counters are cumulative. GPU stage samples can overlap, and slow point-read
logs are rate limited: neither is treated as an exhaustive frame-cost total.
"""
import argparse
import json
import re
import statistics
from collections import defaultdict
from pathlib import Path


def pairs(text):
    return {key: float(value) for key, value in re.findall(r"(\w+):(-?[\d.]+)", text or "")}


def timings(text):
    return {key: (int(count), int(ns)) for key, count, ns in
            re.findall(r"(\w+):(\d+)/(\d+)", text or "")}


def median(values):
    return statistics.median(values) if values else None


def summarize(path, start, end, session=None):
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    groups = defaultdict(list)
    for row in rows:
        if row.get("event") == "heartbeat.profile":
            groups[row.get("fields", {}).get("gameSession", "none")].append(row)
    if not groups:
        raise ValueError(f"No heartbeat.profile data in {path.name}")
    if session is None:
        session = max(groups, key=lambda key: max(
            (timings(row["fields"].get("emoteProfile")).get("prepare", (0, 0))[0] for row in groups[key]), default=0))
    profiles = sorted(groups.get(session, []), key=lambda row: row["unixTime"])
    if not profiles:
        raise ValueError(f"Session {session} not found")
    launch = next((row for row in rows if row.get("event") == "game.begin" and
                   row.get("fields", {}).get("gameSession") == session), profiles[0])
    origin = launch["unixTime"]
    selected = [row for row in profiles if start <= row["unixTime"] - origin <= end]
    if not selected:
        raise ValueError("No samples in selected window")
    count, prepare_ns, capture_ns, duration = 0, 0, 0, 0.0
    active = []
    for before, after in zip(profiles, profiles[1:]):
        if after not in selected:
            continue
        a, b = timings(before["fields"].get("emoteProfile")), timings(after["fields"].get("emoteProfile"))
        if "prepare" not in a or "prepare" not in b:
            continue
        draws = b["prepare"][0] - a["prepare"][0]
        ns = b["prepare"][1] - a["prepare"][1]
        dt = after["unixTime"] - before["unixTime"]
        if draws <= 0 or ns < 0 or not 0 < dt < 3:
            continue
        count += draws
        prepare_ns += ns
        capture_ns += max(0, b.get("capture", (0, 0))[1] - a.get("capture", (0, 0))[1])
        duration += dt
        active.append(after)
    stages, slow = defaultdict(list), []
    for row in rows:
        fields = row.get("fields", {})
        if fields.get("gameSession") != session or not start <= row["unixTime"] - origin <= end:
            continue
        message = fields.get("message", "")
        if row.get("event") != "runtime.message":
            continue
        values = {key: float(value) for key, value in re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", message)}
        if message.startswith("metal.gpuStages "):
            for key in ("meshVertexMS", "meshFragmentMS", "layerComputeMS", "blitMS"):
                if values.get(key, -1) >= 0:
                    stages[key].append(values[key])
        if message.startswith("metal.pointRead ") and "source=layer.hitTest " in message:
            slow.append(values.get("gpuSyncWaitMS", 0))
    first, last = selected[0]["fields"], selected[-1]["fields"]
    perf_first, perf_last = pairs(first.get("emotePerformanceProfile")), pairs(last.get("emotePerformanceProfile"))
    perf = {key: max(0, value - perf_first.get(key, value)) for key, value in perf_last.items()}
    environment = next((row["fields"] for row in rows if row.get("event") == "environment" and
                        row.get("run") == launch.get("run")), {})
    return {
        "session": session, "sourceRevision": environment.get("sourceRevision"),
        "mode": launch.get("fields", {}).get("emoteAnimationMode"),
        "options": launch.get("fields", {}).get("emotePerformanceOptions"),
        "selected_samples": len(selected), "active_seconds": duration, "prepare_calls": count,
        "prepare_ms_per_call": prepare_ns / count / 1e6 if count else None,
        "capture_encoding_ms_per_prepare": capture_ns / count / 1e6 if count else None,
        "reported_fps_median": median([float(row["fields"]["fps"]) for row in active]),
        "reported_cpu_frame_wall_ms_median": median([float(row["fields"]["cpuFrameTimeMS"]) for row in active]),
        "thermal_max": max(int(row["fields"].get("thermalState", 0)) for row in selected),
        "gpu_stage_samples": {key: {"count": len(values), "median_ms": median(values)} for key, values in stages.items()},
        "rate_limited_slow_hit_samples": {"count": len(slow), "median_gpu_wait_ms": median(slow),
                                           "max_gpu_wait_ms": max(slow) if slow else None},
        "performance_counter_deltas": perf,
        "limitations": "Heartbeat wall time includes waits; GPU stages overlap; slow-hit samples are rate limited. No energy/heat saving is inferred."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--session")
    parser.add_argument("--start", type=float, default=0, help="Seconds after game.begin")
    parser.add_argument("--end", type=float, default=float("inf"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.start < 0 or args.end <= args.start:
        parser.error("Use a nonnegative start and an end greater than start")
    result = {"candidate": summarize(args.log, args.start, args.end, args.session)}
    if args.baseline:
        baseline = result["baseline"] = summarize(args.baseline, args.start, args.end)
        result["change_percent"] = {}
        for key in ("prepare_ms_per_call", "capture_encoding_ms_per_prepare", "reported_cpu_frame_wall_ms_median"):
            before, after = baseline.get(key), result["candidate"].get(key)
            if before and after is not None:
                result["change_percent"][key] = (after / before - 1) * 100
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
