#!/usr/bin/env python3
"""Alternate saved-game renderer measurements (software or portable SDL); retain raw evidence."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import statistics
import subprocess
import tempfile

# Metrics parsed from each log; medians are summarized per variant.
METRICS = {
    "cpu_ms": r"process_cpu_ms_per_frame=([0-9.]+)",
    "frame_mean_ms": r"^frame mean_ms=([0-9.]+)",
    "frame_p95_ms": r"^frame mean_ms=[0-9.]+ median_ms=[0-9.]+ p95_ms=([0-9.]+)",
    "draw_mean_ms": r"^draw mean_ms=([0-9.]+)",
    "present_mean_ms": r"^present mean_ms=([0-9.]+)",
    "peak_rss_mib": r"peak_rss_bytes=([0-9]+)",
    "steady_rss_mib": r"^memory_end rss_bytes=([0-9]+)",
    "geometry_calls": r"portable_frame geometry_calls=([0-9.]+)",
    "texture_switches": r"portable_frame .* texture_switches=([0-9.]+)",
    "geometry_batches": r"portable_frame .* geometry_batches=([0-9.]+)",
    "texture_uploads": r"portable_frame .* texture_uploads=([0-9.]+)",
    "upload_kib": r"portable_frame .* upload_bytes=([0-9.]+)",
    "textures": r"portable_memory textures=([0-9]+)",
    "texture_mib": r"portable_memory textures=[0-9]+ texture_bytes=([0-9]+)",
    "source_surface_mib": r"portable_memory .* source_surface_bytes=([0-9]+)",
    "sprite_hd_mib": r"^sprite_hd cpu_bytes=([0-9]+)",
    "terrain_cache_mib": r"^terrain_cache bytes=([0-9]+)",
}
SCALE = {"peak_rss_mib": 2**20, "steady_rss_mib": 2**20, "texture_mib": 2**20,
         "source_surface_mib": 2**20, "sprite_hd_mib": 2**20, "terrain_cache_mib": 2**20, "upload_kib": 1024}


def parse(log):
    values = {}
    for name, pattern in METRICS.items():
        match = re.search(pattern, log, re.M)
        if match:
            values[name] = float(match[1]) / SCALE.get(name, 1)
    return values


def nvidia_process_mib(pid):
    """Per-process framebuffer MiB from nvidia-smi pmon (graphics clients included)."""
    if not shutil.which("nvidia-smi"):
        return None
    try:
        out = subprocess.run(["nvidia-smi", "pmon", "-s", "m", "-c", "1"], capture_output=True,
                             text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    total = None
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[1] == str(pid) and fields[3].isdigit():
            total = (total or 0) + int(fields[3])
    return total


def proc_status(pid):
    values = {}
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            key, _, rest = line.partition(":")
            if key in ("VmRSS", "VmHWM", "RssAnon", "RssFile"):
                values[key] = int(rest.split()[0]) * 1024
    except OSError:
        pass
    return values


def probe(command, env, cwd, timeout):
    """Stop at the profiling boundaries and sample memory there; timing is invalid."""
    env = dict(env, PROFILE_BOUNDARY_STOP="1")
    process = subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    samples, lines = {}, []
    for line in process.stdout:
        lines.append(line)
        match = re.match(r"PROFILE_BOUNDARY phase=(\w+)", line)
        if match:
            # The process raised SIGSTOP after flushing this line.
            samples[match[1]] = {"gpu_fb_mib": nvidia_process_mib(process.pid), **proc_status(process.pid)}
            os.kill(process.pid, signal.SIGCONT)
    process.wait(timeout=timeout)
    return process.returncode, "".join(lines), samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, help="omit to measure only the baseline")
    parser.add_argument("--save", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/software-renderer"))
    parser.add_argument("--renderer", choices=["software", "sdl"], default="software",
                        help="sdl measures the portable SDL_Renderer backend and always presents")
    parser.add_argument("--profile-env", action="append", default=[], metavar="PROFILE_NAME=VALUE",
                        help="extra PROFILE_* workload option for both variants (PROFILE_MODE=gui, PROFILE_PAUSED=1...)")
    parser.add_argument("--render-driver", help="SDL_RENDER_DRIVER for --renderer sdl (opengl, vulkan, gpu, software...)")
    parser.add_argument("--resolution", default="1280x800")
    parser.add_argument("--frames", type=int, default=240)
    parser.add_argument("--warmup", type=int, default=30)
    parser.add_argument("--repeat", type=int, default=7)
    parser.add_argument("--present", action="store_true")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--paused", action="store_true",
                        help="pause the simulation so frames isolate static-scene submission cost")
    parser.add_argument("--memory-probe", action="store_true",
                        help="add one boundary-stopped run per variant sampling RSS and NVIDIA per-process GPU memory")
    parser.add_argument("--hd-complete", action="store_true",
                        help="finish streaming optional HD layers before measuring (PROFILE_HD_COMPLETE)")
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
    if args.render_driver and args.renderer != "sdl":
        parser.error("--render-driver requires --renderer sdl")
    if any(not option.startswith("PROFILE_") or "=" not in option for option in args.profile_env):
        parser.error("--profile-env takes PROFILE_NAME=VALUE")
    variants = ("baseline", "candidate") if args.candidate else ("baseline",)
    args.output.mkdir(parents=True, exist_ok=True)
    results = {"hardware": platform.platform(), "processor": platform.processor(), "machine": platform.machine(),
               "arguments": {k: str(v) for k, v in vars(args).items()}, "runs": [], "summary": []}
    scenarios = [("native", "1", False), ("half", "0.5", False),
                 ("double", "2", False), ("fractional", "1", True)]
    if args.scenario:
        scenarios = [entry for entry in scenarios if entry[0] in args.scenario]
    root = Path(__file__).resolve().parents[1]

    def environment(fixture, zoom, fractional, variant, profile):
        env = os.environ.copy()
        for key in list(env):
            if key.startswith("PROFILE_"):
                del env[key]
        env.pop("SDL_RENDER_DRIVER", None)
        env.update(GLOB2_USER_DATA_DIR=profile, GLOB2_UI_SCALE="1", PROFILE_SAVE=str(fixture.resolve()),
                   PROFILE_ZOOM=zoom, PROFILE_FRAMES=str(args.frames),
                   PROFILE_WARMUP=str(args.warmup), PROFILE_CPU_SCOPES="1")
        if args.render_driver:
            env["SDL_RENDER_DRIVER"] = args.render_driver
        for option in args.profile_env:
            name, _, value = option.partition("=")
            env[name] = value
        if fractional:
            env["PROFILE_FRACTION"] = "1"
        if not args.present and args.renderer != "sdl":
            env["PROFILE_NO_PRESENT"] = "1"
        if args.visible:
            env["PROFILE_VISIBLE"] = "1"
        if args.hd_complete:
            env["PROFILE_HD_COMPLETE"] = "1"
        if args.paused:
            env["PROFILE_PAUSED"] = "1"
        if args.no_terrain_cache or (variant == "baseline" and args.baseline_no_terrain_cache):
            env["PROFILE_TERRAIN_CACHE"] = "0"
        if variant == "baseline" and args.baseline_preserve_frame:
            env["PROFILE_PRESERVE_FRAME"] = "1"
        return env

    def command(variant):
        return [str(getattr(args, variant).resolve()), "--renderer", args.renderer, "--window-size",
                args.resolution, "--mute", "--no-fullscreen"]

    # Each fixture/scenario gets interleaved pairs on identical hardware.
    # Reverse order on alternate pairs to limit startup/thermal order bias.
    for fixture_index, fixture in enumerate(args.save):
        for scenario, zoom, fractional in scenarios:
            measured = {variant: [] for variant in variants}
            for repeat in range(args.repeat):
                order = variants if repeat % 2 == 0 else tuple(reversed(variants))
                for variant in order:
                    label = f"{fixture_index}-{fixture.stem}-{scenario}-{repeat}-{variant}"
                    with tempfile.TemporaryDirectory(prefix="glob2-render-benchmark-") as profile:
                        env = environment(fixture, zoom, fractional, variant, profile)
                        if repeat == 0:
                            env["PROFILE_CAPTURE"] = str((args.output / f"{label}.bmp").resolve())
                        run = subprocess.run(command(variant), env=env, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True, timeout=300, cwd=root)
                    (args.output / f"{label}.log").write_text(run.stdout)
                    if run.returncode:
                        raise RuntimeError(f"{label} failed ({run.returncode}); inspect its log")
                    if "timing_valid=true" not in run.stdout:
                        raise RuntimeError(f"{label} reported invalid timing; remove profiler/interposition settings")
                    values = parse(run.stdout)
                    if "cpu_ms" not in values:
                        raise RuntimeError(f"{label} did not report CPU time")
                    measured[variant].append(values)
                    results["runs"].append({"label": label, "variant": variant, "scenario": scenario,
                                            "fixture": str(fixture), "command": command(variant),
                                            "environment": {k: v for k, v in env.items()
                                                            if k.startswith("PROFILE_") or k == "SDL_RENDER_DRIVER"},
                                            "cpu_ms": values["cpu_ms"], "metrics": values})
                    print(f"{label}: {values['cpu_ms']:.3f} CPU ms/frame, "
                          f"{values.get('frame_mean_ms', float('nan')):.3f} frame ms", flush=True)
            row = {"fixture": str(fixture), "scenario": scenario}
            for variant in variants:
                runs = measured[variant]
                row[variant] = {name: statistics.median(run[name] for run in runs)
                                for name in METRICS if all(name in run for run in runs)}
            # Retain the original summary keys for existing consumers.
            row["baseline_ms"] = row["baseline"]["cpu_ms"]
            row["cpu_runs"] = {variant: [run["cpu_ms"] for run in measured[variant]] for variant in variants}
            if args.candidate:
                row["candidate_ms"] = row["candidate"]["cpu_ms"]
                row["speedup"] = row["baseline_ms"] / row["candidate_ms"]
            if args.memory_probe:
                row["memory_probe"] = {}
                for variant in variants:
                    with tempfile.TemporaryDirectory(prefix="glob2-render-benchmark-") as profile:
                        env = environment(fixture, zoom, fractional, variant, profile)
                        code, log, samples = probe(command(variant), env, root, 300)
                    (args.output / f"{fixture_index}-{fixture.stem}-{scenario}-memory-{variant}.log").write_text(log)
                    if code:
                        raise RuntimeError(f"{variant} memory probe failed ({code})")
                    row["memory_probe"][variant] = samples
            results["summary"].append(row)
            text = " ".join(f"{name}={row['baseline'][name]:.3f}" for name in row["baseline"])
            print(f"{fixture.name}/{scenario} baseline: {text}", flush=True)
            if args.candidate:
                before, after = row["baseline_ms"], row["candidate_ms"]
                print(f"{fixture.name}/{scenario}: CPU {before:.3f} -> {after:.3f} ms ({before / after:.2f}x); "
                      + " ".join(f"{name} {row['baseline'][name]:.2f}->{row['candidate'].get(name, float('nan')):.2f}"
                                 for name in ("frame_mean_ms", "frame_p95_ms", "peak_rss_mib", "texture_mib")
                                 if name in row["baseline"]), flush=True)
            if args.memory_probe:
                print(f"{fixture.name}/{scenario} memory probe: {json.dumps(row['memory_probe'])}", flush=True)
            (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
