#!/usr/bin/env python3
"""Alternate saved-game software renderer CPU measurements; retain raw evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.tournaments.cli import parse_cpu_ids, positive_integer, validate_native_resource_result
from tools.tournaments.resources import PROFILES, plan_allocations


def execution_request(cpu_ids, profile, compatibility_modes, participants=None, memory_bytes=None):
    """Pure admission for bundled renderer harnesses; older harnesses are explicit."""
    if profile not in PROFILES or any(mode not in ('require_profile', 'legacy_argv')
                                     for mode in compatibility_modes.values()):
        raise ValueError('invalid execution intent or compatibility')
    if cpu_ids is None:
        if any(mode == 'require_profile' for mode in compatibility_modes.values()) or participants is not None or memory_bytes is not None:
            raise ValueError('profile-aware rendering requires explicit --cpu-ids')
        allocation = None
    else:
        allocation = plan_allocations(cpu_ids, 1, participants_per_game=participants,
                                      memory_planning_budget_bytes=memory_bytes).allocations[0]
    count = allocation.participants if allocation else None
    flags = {}
    for role, mode in compatibility_modes.items():
        flags[role] = (['--compute-threads', str(count)] if count is not None else []) if mode == 'legacy_argv' else [
            '--execution-profile', profile, '--compute-threads', str(count)] + (
            ['--memory-budget-bytes', str(memory_bytes)] if memory_bytes is not None else [])
    return {'requested_execution_profile': profile, 'compatibility_modes': compatibility_modes,
            'cpu_ids': list(cpu_ids) if cpu_ids is not None else None,
            'participants': count, 'memory_planning_bytes': memory_bytes,
            'memory_enforcement': 'soft_planning_only', 'flags': flags,
            'requested_renderer': 'software',
            'cpu_enforcement': 'taskset_inherited_mask' if cpu_ids is not None else 'explicit_legacy_not_enforced'}


def validate_execution_output(stdout, receipt, role):
    if receipt['compatibility_modes'][role] == 'legacy_argv':
        counts = re.findall(r'^compute_threads=([0-9]+)$', stdout, re.MULTILINE)
        if len(counts) > 1 or (counts and receipt['participants'] is not None
                              and int(counts[0]) != receipt['participants']):
            raise ValueError('legacy renderer actual participants differ from fixed caller budget')
        return {'scope': 'explicit_legacy_resolved_plan_unknown',
                'compute_threads': int(counts[0]) if counts else None,
                'participant_proof': 'actual_marker' if counts else 'requested_only_actual_unknown'}
    plans = re.findall(r'^execution_resources=(\{.*\})$', stdout, re.MULTILINE)
    if len(plans) != 1:
        raise ValueError('renderer harness must emit exactly one resolved execution_resources marker')
    result = {'execution_resources': json.loads(plans[0])}
    for key in ('compute_threads', 'compute_resolved_threads'):
        values = re.findall(r'^' + key + r'=([0-9]+)$', stdout, re.MULTILINE)
        if len(values) != 1:
            raise ValueError('renderer harness missing/duplicate actual participant marker: ' + key)
        result[key] = int(values[0])
    scope = validate_native_resource_result(result, receipt, role, receipt['participants'])
    return {'scope': scope, **result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument('--execution-profile', choices=PROFILES, default='fast-simulation')
    parser.add_argument('--execution-compatibility', choices=('require_profile', 'legacy_argv'), default='require_profile')
    parser.add_argument('--baseline-execution-compatibility', choices=('require_profile', 'legacy_argv'))
    parser.add_argument('--cpu-ids', help='Caller-owned fixed logical CPU IDs, e.g. 0-5')
    parser.add_argument('--compute-participants', type=positive_integer, help='Owner-inclusive count within the fixed CPU mask')
    parser.add_argument('--memory-planning-budget-bytes', type=positive_integer)
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
    execution = execution_request(parse_cpu_ids(args.cpu_ids) if args.cpu_ids is not None else None,
        args.execution_profile, {'baseline': args.baseline_execution_compatibility or args.execution_compatibility,
                                 'candidate': args.execution_compatibility},
        args.compute_participants, args.memory_planning_budget_bytes)
    if execution['cpu_ids'] is not None:
        if not hasattr(os, 'sched_getaffinity') or not set(execution['cpu_ids']).issubset(os.sched_getaffinity(0)):
            parser.error('requested CPU IDs are not within the observed caller allocation')
    if args.frames < 1 or args.warmup < 0 or args.repeat < 1:
        parser.error("frames/repeat must be positive; warmup must be nonnegative")
    if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", args.resolution):
        parser.error("resolution must be WxH")
    args.output.mkdir(parents=True, exist_ok=True)
    results = {"hardware": platform.platform(), "processor": platform.processor(), "machine": platform.machine(),
               "arguments": {k: str(v) for k, v in vars(args).items()}, 'execution_request': execution,
               'binaries_sha256': {role: hashlib.sha256(getattr(args, role).read_bytes()).hexdigest()
                                   for role in ('baseline', 'candidate')}, "runs": [], "summary": []}
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
                        command = [str(getattr(args, variant).resolve()), "--renderer", "software", "--window-size", args.resolution, "--mute", "--no-fullscreen"]
                        command += execution['flags'][variant]
                        if execution['cpu_ids'] is not None:
                            command = ['taskset', '-c', ','.join(map(str, execution['cpu_ids'])), *command]
                        run = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                             stderr=subprocess.STDOUT, text=True, timeout=300,
                                             cwd=Path(__file__).resolve().parents[1])
                    (args.output / f"{label}.log").write_text(run.stdout)
                    if run.returncode:
                        raise RuntimeError(f"{label} failed ({run.returncode}); inspect its log")
                    resolved = validate_execution_output(run.stdout, execution, variant)
                    match = re.search(r"process_cpu_ms_per_frame=([0-9.]+)", run.stdout)
                    if not match:
                        raise RuntimeError(f"{label} did not report CPU time")
                    value = float(match[1])
                    cpu[variant].append(value)
                    results["runs"].append({"label": label, "variant": variant, "scenario": scenario,
                                            "fixture": str(fixture), "command": command,
                                            "environment": {k: v for k, v in env.items() if k.startswith("PROFILE_") or k in (
                                                'GLOB2_RENDER_EXECUTION', 'GLOB2_RENDER_WORKERS', 'GLOB2_GRADIENT_BACKEND',
                                                'GLOB2_GRADIENT_ALGORITHM', 'GLOB2_GRADIENT_VERIFY')},
                                            'execution_validation': resolved,
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
