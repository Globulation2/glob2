#!/usr/bin/env python3
"""Alternate saved-game software renderer CPU measurements; retain raw evidence."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--save", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/software-renderer"))
    parser.add_argument("--resolution", default="1280x800")
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--warmup", type=int, default=30)
    parser.add_argument("--repeat", type=int, default=7)
    parser.add_argument("--present", action="store_true")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--no-terrain-cache", action="store_true", help="isolate the primitive optimization")
    parser.add_argument("--baseline-no-terrain-cache", action="store_true",
                        help="compare the same executable with uncached baseline terrain")
    parser.add_argument("--baseline-preserve-frame", action="store_true",
                        help="compare full redraw against a baseline retention copy")
    parser.add_argument("--scenario", action="append", choices=["native", "half", "double", "fractional"],
                        help="measure only these scenarios (default: all four)")
    args = parser.parse_args()
    if args.frames < 1 or args.warmup < 0 or args.repeat < 1:
        parser.error("frames/repeat must be positive; warmup must be nonnegative")
    if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", args.resolution):
        parser.error("resolution must be WxH")
    args.output.mkdir(parents=True, exist_ok=True)
    results = {"hardware": platform.platform(), "processor": platform.processor(), "machine": platform.machine(),
               "arguments": {k: str(v) for k, v in vars(args).items()}, "runs": [], "summary": []}
    scenarios = [("native", "1", False), ("half", "0.5", False),
                 ("double", "2", False), ("fractional", "1", True)]
    if args.scenario:
        scenarios = [entry for entry in scenarios if entry[0] in args.scenario]
    # Each fixture/scenario gets seven interleaved pairs on identical hardware.
    # Reverse order on alternate pairs to limit startup/thermal order bias.
    for fixture_index, fixture in enumerate(args.save):
        for scenario, zoom, fractional in scenarios:
            cpu = {"baseline": [], "candidate": []}
            for repeat in range(args.repeat):
                order = ("baseline", "candidate") if repeat % 2 == 0 else ("candidate", "baseline")
                for variant in order:
                    label = f"{fixture_index}-{fixture.stem}-{scenario}-{repeat}-{variant}"
                    with tempfile.TemporaryDirectory(prefix="glob2-render-benchmark-") as profile:
                        env = os.environ.copy()
                        for key in list(env):
                            if key.startswith("PROFILE_"):
                                del env[key]
                        env.update(GLOB2_USER_DATA_DIR=profile, GLOB2_UI_SCALE="1", PROFILE_SAVE=str(fixture.resolve()),
                                   PROFILE_ZOOM=zoom, PROFILE_FRAMES=str(args.frames),
                                   PROFILE_WARMUP=str(args.warmup), PROFILE_CPU_SCOPES="1")
                        if repeat == 0:
                            env["PROFILE_CAPTURE"] = str((args.output / f"{label}.bmp").resolve())
                        if fractional:
                            env["PROFILE_FRACTION"] = "1"
                        if not args.present:
                            env["PROFILE_NO_PRESENT"] = "1"
                        if args.visible:
                            env["PROFILE_VISIBLE"] = "1"
                        if args.no_terrain_cache or (variant == "baseline" and args.baseline_no_terrain_cache):
                            env["PROFILE_TERRAIN_CACHE"] = "0"
                        if variant == "baseline" and args.baseline_preserve_frame:
                            env["PROFILE_PRESERVE_FRAME"] = "1"
                        command = [str(getattr(args, variant).resolve()), "-G", "-s", args.resolution, "-m", "-F"]
                        run = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True, timeout=300,
                                             cwd=Path(__file__).resolve().parents[1])
                    (args.output / f"{label}.log").write_text(run.stdout)
                    if run.returncode:
                        raise RuntimeError(f"{label} failed ({run.returncode}); inspect its log")
                    match = re.search(r"process_cpu_ms_per_frame=([0-9.]+)", run.stdout)
                    if not match:
                        raise RuntimeError(f"{label} did not report CPU time")
                    value = float(match[1])
                    cpu[variant].append(value)
                    results["runs"].append({"label": label, "variant": variant, "scenario": scenario,
                                            "fixture": str(fixture), "command": command,
                                            "environment": {k: v for k, v in env.items() if k.startswith("PROFILE_")},
                                            "cpu_ms": value})
                    print(f"{label}: {value:.3f} CPU ms/frame", flush=True)
            before, after = statistics.median(cpu["baseline"]), statistics.median(cpu["candidate"])
            row = {"fixture": str(fixture), "scenario": scenario, "baseline_ms": before,
                   "candidate_ms": after, "speedup": before / after, "cpu_runs": cpu}
            results["summary"].append(row)
            print(f"{fixture.name}/{scenario}: {before:.3f} -> {after:.3f} ms ({before / after:.2f}x)", flush=True)
            (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
