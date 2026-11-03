#!/usr/bin/env python3
"""Alternate render benchmark conditions and summarise key metrics (local evidence only)."""
import argparse, json, os, re, statistics, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "artifacts/bench"


def software(binary, assets, paused, out):
    env = dict(os.environ, GLOB2_USER_DATA_DIR=str(BENCH / "profile"),
               PROFILE_SAVE=str(BENCH / "game/final.game.gz"), PROFILE_MODE="map",
               PROFILE_WHOLE_MAP="1", PROFILE_OFFSET_X="1504", PROFILE_OFFSET_Y="1664",
               PROFILE_FRAMES="240", PROFILE_WARMUP="60", PROFILE_CLOUDS="0",
               GLOB2_RENDER_EXECUTION="immediate")
    if paused: env["PROFILE_PAUSED"] = "1"
    if assets: env["GLOB2_ASSET_DIR"] = str(assets)
    cmd = [str(binary), "--renderer", "software", "--no-fullscreen", "--mute", "--window-size", "1280x800"]
    return cmd, env


def torus(binary, assets, paused, out):
    env = dict(os.environ, GLOB2_USER_DATA_DIR=str(BENCH / "profile"),
               GLOB2_BENCH_GAME=str(BENCH / "game/final.game.gz"), GLOB2_BENCH_FRAMES="150",
               GLOB2_BENCH_WARMUP_FRAMES="60", GLOB2_BENCH_MODE="2D no clouds",
               GLOB2_BENCH_FLAT="1", GLOB2_BENCH_ZOOM="1", GLOB2_BENCH_PAN_X="47", GLOB2_BENCH_PAN_Y="52",
               GLOB2_RENDER_EXECUTION="immediate", GLOB2_BENCH_FRAME_TIMES="1")
    if paused: env["GLOB2_BENCH_PAUSE_PRESENTATION"] = "1"
    if assets: env["GLOB2_ASSET_DIR"] = str(assets)
    cmd = [str(binary), "--renderer", "legacy-gl", "--no-fullscreen", "--mute", "--window-size", "1280x800"]
    return cmd, env


def metrics(log):
    found = {}
    for label in ("draw", "frame"):
        m = re.search(rf"^{label} mean_ms=([0-9.]+) median_ms=([0-9.]+) p95_ms=([0-9.]+) p99_ms=([0-9.]+)", log, re.M)
        if m: found[label] = dict(zip(("mean", "median", "p95", "p99"), map(float, m.groups())))
    m = re.search(r"^2D no clouds median=([0-9.]+) ms p95=([0-9.]+) ms", log, re.M)
    if m:
        p99 = re.search(r"^2D no clouds p99=([0-9.]+) ms", log, re.M)
        found["draw"] = {"median": float(m.group(1)), "p95": float(m.group(2)), "p99": float(p99.group(1)) if p99 else None}
        found["frame"] = found["draw"]
        c = re.search(r"^2D no clouds process_cpu_median=([0-9.]+) ms", log, re.M)
        if c: found["cpu_ms"] = float(c.group(1))
        caches = re.findall(r"^FRAME_CACHE frame=\d+ .*?rebuilds=(\d+)", log, re.M)
        if caches: found["rebuilds"] = float(int(caches[-1]) - int(caches[0]))
    for scope in ("terrain_materials", "terrain_cache", "terrain"):
        m = re.search(rf"scope=render\.{scope} thread=main .*?mean_ns=([0-9.]+)", log)
        if m: found[scope + "_ms"] = float(m.group(1)) / 1e6
    for key, pattern in (("cpu_ms", r"process_cpu_ms_per_frame=([0-9.]+)"),
                         ("rebuilds", r"terrain_cache .*rebuilds=(\d+)"),
                         ("hd_prepares", r"terrain_hd .*?prepares=(\d+)"),
                         ("hd_fallback", r"terrain_hd .*? fallback_prepares=(\d+)"),
                         ("hd_warm_fallback", r"warmup_fallback_prepares=(\d+)"),
                         ("rss", r"peak_rss_bytes=(\d+)")):
        m = re.search(pattern, log)
        if m: found[key] = float(m.group(1))
    return found


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kind", choices=("software", "torus"), required=True)
    p.add_argument("--condition", action="append", required=True,
                   help="name=binary[,assets] ; assets dir optional")
    p.add_argument("--paused", action="store_true")
    p.add_argument("--repeat", type=int, default=3)
    p.add_argument("--tag", required=True)
    a = p.parse_args()
    out = BENCH / "runs" / a.tag
    out.mkdir(parents=True, exist_ok=True)
    results = {}
    for r in range(a.repeat):
        order = a.condition if r % 2 == 0 else list(reversed(a.condition))
        for spec in order:
            name, rest = spec.split("=", 1)
            binary, _, assets = rest.partition(",")
            cmd, env = (software if a.kind == "software" else torus)(ROOT / binary, assets and ROOT / assets, a.paused, out)
            log_path = out / f"{name}-{r}.log"
            with open(log_path, "w") as log:
                code = subprocess.run(cmd, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
            text = log_path.read_text(errors="replace")
            m = metrics(text)
            m["exit"] = code
            results.setdefault(name, []).append(m)
            print(name, r, json.dumps(m), flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=1))
    print("summary (median over repeats):")
    for name, runs in results.items():
        def med(f):
            v = [f(x) for x in runs if f(x) is not None]
            return statistics.median(v) if v else float("nan")
        def low(f):
            v = [f(x) for x in runs if f(x) is not None]
            return min(v) if v else float("nan")
        print(f"{name:>14} BEST draw={low(lambda x: x.get('draw', {}).get('median')):.2f} cpu={low(lambda x: x.get('cpu_ms')):.2f} "
              f"materials_ms={low(lambda x: x.get('terrain_materials_ms')):.2f} cache_ms={low(lambda x: x.get('terrain_cache_ms')):.2f}")
        print(f"{name:>14} draw_med={med(lambda x: x.get('draw', {}).get('median')):.2f} "
              f"draw_p95={med(lambda x: x.get('draw', {}).get('p95')):.2f} "
              f"frame_med={med(lambda x: x.get('frame', {}).get('median')):.2f} "
              f"cpu={med(lambda x: x.get('cpu_ms')):.2f} rebuilds={med(lambda x: x.get('rebuilds')):.0f} "
              f"materials_ms={med(lambda x: x.get('terrain_materials_ms')):.2f} cache_ms={med(lambda x: x.get('terrain_cache_ms')):.2f} "
              f"hd_fb={med(lambda x: x.get('hd_fallback')):.0f}/{med(lambda x: x.get('hd_prepares')):.0f}")


if __name__ == "__main__":
    main()
