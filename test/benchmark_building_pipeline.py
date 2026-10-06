#!/usr/bin/env python3
"""Reproducible building refresh verification, quiet timing and gameplay auditing.

Inputs are tick-boundary saves; --fork-rule creates explicitly labelled experimental
branches without altering those saves. All raw commands and results are retained.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import gzip
import hashlib
import json
import os
import platform
import shutil
import subprocess
import struct
import time
import sys
import fcntl
from pathlib import Path

from benchmark_parallel_compute import execute, digest
from analyze_building_gradient_impact import distribution, open_csv


def environment():
    result = {"time": time.time(), "platform": platform.platform()}
    if Path("/proc/stat").exists():
        values = list(map(int, Path("/proc/stat").read_text().splitlines()[0].split()[1:]))
        result["busy_cpu_s"] = sum(values[:3] + values[5:8]) / os.sysconf("SC_CLK_TCK")
    return result


def save_case_progress(path, cases, delay):
    """Merge independent delay batches without dropping validated case records."""
    with path.with_suffix('.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        previous = json.loads(path.read_text()) if path.exists() else []
        rows = [r for r in previous if r['delay'] != delay] + cases
        temporary = path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(rows, indent=2) + '\n')
        temporary.replace(path)


def checksum(output):
    source = output / "game.replay.checksums"
    h = hashlib.sha256()
    opener = source.open if source.exists() else lambda mode: gzip.open(str(source) + ".gz", mode)
    with opener("rb") as stream:
        for block in iter(lambda: stream.read(2**20), b""):
            h.update(block)
    if source.exists():
        with source.open("rb") as src, gzip.open(str(source) + ".gz", "wb", compresslevel=6) as dst:
            shutil.copyfileobj(src, dst, 2**20)
        source.unlink()
    return h.hexdigest()


def run(binary, fixture, end_tick, output, workers, delay, audit=False, verify=False, intervention=None, resume=False):
    args = ["--load-game", str(fixture), "--ticks", str(end_tick), "--compute-experiments", "none",
            "--compute-threads", "4", "--gradient-workers", str(workers)]
    if not resume:
        args += ["--fork-rule", f"building-gradient-pipeline={int(delay != 0)}"]
        if delay:
            args += ["--fork-rule", f"buildingGradientDelay={delay}"]
    if audit:
        args += ["--telemetry", "building-gradient-impact"]
    if verify:
        args += ["--telemetry", "checksums", "--replay", "true", "--save", "final"]
        if resume:
            args += ["--save", "initial"]
    elif not audit:
        args += ["--benchmark-warmup", "0", "--telemetry", "building-gradient-timing"]
    if intervention:
        args += ["--gradient-counterfactual", intervention]
    if (output / "measurement.json").exists():
        previous = json.loads((output / "measurement.json").read_text())
        if previous["command"][2:-2] != args:
            raise RuntimeError(f"execution configuration changed: use a new evidence directory ({output})")
        return previous
    execution = output / 'execution.json'
    if execution.exists():
        row = json.loads(execution.read_text())
        if row['command'][2:-2] != args:
            raise RuntimeError(f'execution configuration changed: {output}')
    elif output.exists():
        output.rename(output.with_name(output.name + f"-incomplete-{time.time_ns()}"))
    if not execution.exists():
        before = environment()
        row = execute(binary, args, output)
        after = environment()
        row.update(environment_before=before, environment_after=after, workers=workers, delay=delay)
        if "busy_cpu_s" in before:
            row["background_cores"] = max(0, (after["busy_cpu_s"] - before["busy_cpu_s"] - row["cpu_s"]) / (after["time"] - before["time"]))
        execution.write_text(json.dumps(row, indent=2) + '\n')
    if verify:
        row["hashes"] = {"trace": checksum(output), "replay": digest(output / "game.replay"), "save": digest(output / "final.game")}
        if resume:
            row["hashes"]["initial"] = digest(output / "initial.game")
            if bool(row["result"]["building_gradient_pipeline"]) != bool(delay) or (delay and row["result"]["building_gradient_delay"] != delay):
                raise RuntimeError("resumed checkpoint publication rules differ")
    if (output / "building-gradient-timing.csv").exists():
        with (output / "building-gradient-timing.csv").open() as stream:
            samples = list(csv.DictReader(stream))
        row["tick_ns"] = distribution([int(s["tick_ns"]) for s in samples])
        row["deadline_wait_ns"] = distribution([int(s["deadline_wait_ns"]) for s in samples])
    if audit and not verify:
        summary = output / 'impact-summary.json'
        # Independent workloads must not serialize millions of CSV rows on
        # Python's GIL. Keep analysis outside the measured engine process.
        subprocess.run([sys.executable, str(Path(__file__).with_name('analyze_building_gradient_impact.py')),
                        str(output), '--output', str(summary)], check=True)
        row["impact"] = json.loads(summary.read_text())
    # Preserve raw evidence losslessly without retaining full cached-field replay headers.
    retained = [p for p in (output / 'game.replay', output / 'final.game', output / 'initial.game') if p.exists()] if verify else []
    for source in retained + list(output.glob("building-gradient-impact-*.csv")):
        with source.open("rb") as src, gzip.open(str(source) + ".gz", "wb", compresslevel=1 if verify else 6) as dst:
            shutil.copyfileobj(src, dst, 2**20)
        source.unlink()
    (output / "measurement.json").write_text(json.dumps(row, indent=2) + "\n")
    return row


def counterfactual_reference_end(audit):
    """One uninterrupted normal trace covers every retained decision horizon."""
    return max((int(case['tick']) + 512 for inventory in audit['case_inventory'].values()
                for case in inventory), default=None)


def verify_counterfactual_prefix(normal, fresh, intervention_tick):
    """Stream detailed traces; the two executions must match before intervention."""
    with gzip.open(normal / "game.replay.checksums.gz", "rb") as a, gzip.open(fresh / "game.replay.checksums.gz", "rb") as b:
        header, other = a.read(20), b.read(20)
        if header[:12] != other[:12] or header[:4] != b"GCS1":
            raise RuntimeError("counterfactual trace setup differs")
        teams, _, count, _ = struct.unpack_from("<4I", header, 4)
        compared = 0
        def equal(size):
            data = a.read(size)
            if len(data) != size or b.read(size) != data:
                raise RuntimeError("counterfactual diverged before intervention")
            return data
        for _ in range(count):
            tick_data = a.read(8)
            if len(tick_data) != 8:
                raise RuntimeError("truncated counterfactual trace")
            tick = struct.unpack_from("<I", tick_data)[0]
            if tick >= intervention_tick:
                return compared
            if b.read(8) != tick_data:
                raise RuntimeError("counterfactual diverged before intervention")
            for _ in range(teams):
                equal(4)
                for _ in range(2):
                    entities = struct.unpack("<I", equal(4))[0]
                    for _ in range(entities):
                        fields = struct.unpack_from("<I", equal(10), 6)[0]
                        equal(4 * fields)
            compared += 1
        raise RuntimeError("counterfactual trace never reached intervention")


def trace_frames(output):
    """Stream exact detailed-state frame hashes without retaining entity arrays."""
    with gzip.open(output / "game.replay.checksums.gz", "rb") as stream:
        header = stream.read(20)
        if len(header) != 20 or header[:4] != b"GCS1":
            raise RuntimeError("invalid detailed trace")
        teams, _, count, _ = struct.unpack_from("<4I", header, 4)
        for _ in range(count):
            h = hashlib.sha256()
            def read(size):
                data = stream.read(size)
                if len(data) != size:
                    raise RuntimeError("truncated detailed trace")
                h.update(data)
                return data
            tick_header = stream.read(8)
            if len(tick_header) != 8:
                raise RuntimeError('truncated detailed trace')
            # The aggregate checksum also covers save/header metadata, which
            # changes when loading a new-format checkpoint. Match the complete
            # team/entity records, as compare_save_continuation does.
            tick = struct.unpack_from("<I", tick_header)[0]
            for _ in range(teams):
                read(4)
                for _ in range(2):
                    for _ in range(struct.unpack("<I", read(4))[0]):
                        fields = struct.unpack_from("<I", read(10), 6)[0]
                        read(4 * fields)
            yield tick, h.digest()


def verify_trace_window(reference, resumed):
    with gzip.open(reference / 'game.replay.checksums.gz', 'rb') as a, gzip.open(resumed / 'game.replay.checksums.gz', 'rb') as b:
        if a.read(12) != b.read(12):
            raise RuntimeError('checkpoint trace schemas differ')
    source = iter(trace_frames(reference))
    compared = 0
    previous = None
    for tick, frame in trace_frames(resumed):
        while previous is None or previous[0] < tick:
            previous = next(source, None)
            if previous is None:
                raise RuntimeError("reference trace ends before resumed continuation")
        if previous != (tick, frame):
            raise RuntimeError("checkpoint continuation changes simulation state")
        compared += 1
    if not compared:
        raise RuntimeError("empty checkpoint continuation")
    return compared


def verify_optional_checkpoint(reference, resumed):
    """Reject a divergent optimization; malformed traces still abort the run."""
    try:
        return verify_trace_window(reference, resumed), None
    except RuntimeError as error:
        if str(error) != "checkpoint continuation changes simulation state":
            raise
        return 0, str(error)


def resumed_case_event(output, case):
    """Require one unambiguous match of all recorded decision fields."""
    events = set()
    with open_csv(output / "building-gradient-impact-decisions.csv") as stream:
        for row in csv.DictReader(stream):
            if int(row['tick']) > int(case['tick']):
                break
            if all(row.get(k) == str(v) for k, v in case.items() if k != 'event'):
                events.add(int(row['event']))
    return next(iter(events)) if len(events) == 1 else None


def verify_decision_prefix(normal, fresh, event):
    with open_csv(normal / 'building-gradient-impact-decisions.csv') as a, open_csv(fresh / 'building-gradient-impact-decisions.csv') as b:
        other = iter(csv.DictReader(b))
        compared = 0
        for row in csv.DictReader(a):
            if int(row['event']) >= event:
                return compared
            if next(other, None) != row:
                raise RuntimeError('resumed decisions differ before intervention')
            compared += 1
    raise RuntimeError('resumed decision event was not reached')


def case_observations(output, kind, case, horizon=None, event_offset=0):
    uid = int(case["fresh_choice"]) if kind == "hiring" else int(case["unit"])
    lifetime = int(case["fresh_identity"] if kind == "hiring" else case["unit_identity"])
    boundary = (int(case["tick"]), int(case["event"]) - event_offset)
    with open_csv(output / "building-gradient-impact-outcomes.csv") as stream:
        rows = [r for r in csv.DictReader(stream)
                if int(r["unit"]) == uid and int(r["unit_identity"]) == lifetime
                and int(r["building"]) == int(case["building"])
                and int(r["building_identity"]) == int(case["building_identity"])
                and (int(r["tick"]), int(r["event"])) >= boundary
                and (horizon is None or int(r["tick"]) < horizon)]
    endpoint = next((r for r in rows if r["kind"] in
                     ("delivered", "abandoned", "removed", "unfinished_trip")), None)
    end = (int(endpoint["tick"]), int(endpoint["event"])) if endpoint else (float('inf'), float('inf'))
    # A later hire after demand disappeared belongs to a new opportunity.
    # Preserve the original episode's censoring instead of reporting that hire
    # (or its delivery) as an exact delay of the audited decision.
    hiring_episode = None
    if kind in ('hiring', 'hiring_candidate'):
        episode_kind = 'missed_hire' if kind == 'hiring' else 'missed_hire_candidate'
        hiring_episode = next((r for r in rows if r['kind'] == episode_kind
            and int(r.get('censored', 0))
            and int(r['tick']) - int(r.get('elapsed_ticks', 0)) <= int(case['tick'])
            and ('fresh_resource' not in case or r.get('resource') == str(case['fresh_resource']))), None)
        if hiring_episode:
            cutoff = (int(hiring_episode['tick']), int(hiring_episode['event']))
            if cutoff < end:
                end = cutoff
                endpoint = dict(hiring_episode, kind='hiring_opportunity_censored')
    observed = {name: next((r for r in rows if r["kind"] == name
                            and (int(r["tick"]), int(r["event"])) <= end), None)
                for name in ("hired", "harvested", "market_acquired")}
    observed['acquired'] = next((r for r in rows if r['kind'] in ('harvested', 'market_acquired')
                                and (int(r['tick']), int(r['event'])) <= end), None)
    observed["delivered"] = endpoint if endpoint and endpoint["kind"] == "delivered" else None
    observed["trip_endpoint"] = endpoint
    observed['hiring_episode_endpoint'] = hiring_episode
    if horizon is not None and endpoint is None:
        observed['trip_endpoint'] = {'kind': 'horizon_censored', 'tick': str(horizon), 'censored': '1'}
    if kind not in ("hiring", "hiring_candidate"):
        observed["hired"] = None
    return observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--checkpoint", required=True, action="append", type=Path)
    parser.add_argument("--start-tick", required=True, action="append", type=int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--phase", choices=("verify", "timing", "audit", "counterfactual"), required=True)
    parser.add_argument("--counterfactual-checkpoints", action="store_true",
                        help="verify preceding checkpoints and translate observational event IDs for late cases")
    parser.add_argument("--counterfactual-checkpoint-binary", type=Path,
                        help="use a corrected checkpoint writer, accepting only continuations matching the frozen reference")
    parser.add_argument("--counterfactual-delay", type=int, choices=(2, 4, 8),
                        help="run one independent diagnostic delay batch; completed cases merge atomically")
    parser.add_argument("--counterfactual-event", action="append", default=[],
                        help="retain only a tick:event decision group; use distinct output roots for parallel groups")
    parser.add_argument("--ticks", type=int, default=2048)
    parser.add_argument("--repeats", type=int, default=10)
    parser.add_argument("--quiet-cores", type=float, default=.5)
    parser.add_argument("--workload-jobs", type=int, default=1,
                        help="independent workloads in parallel; audit and counterfactual phases only")
    args = parser.parse_args()
    selected_events = set()
    for value in args.counterfactual_event:
        pieces = value.split(':')
        if args.phase != 'counterfactual' or len(pieces) != 2 or not all(p.isdecimal() for p in pieces):
            parser.error('--counterfactual-event requires a nonnegative tick:event in counterfactual phase')
        selected_events.add(tuple(map(int, pieces)))
    if args.counterfactual_delay is not None and args.phase != 'counterfactual':
        parser.error('--counterfactual-delay requires the counterfactual phase')
    if args.counterfactual_checkpoint_binary and (args.phase != 'counterfactual' or not args.counterfactual_checkpoints):
        parser.error('--counterfactual-checkpoint-binary requires counterfactual checkpoints')
    if not 1 <= args.workload_jobs <= 8 or (args.workload_jobs != 1 and args.phase not in ("audit", "counterfactual")):
        parser.error("--workload-jobs accepts 1..8; parallel jobs require audit or counterfactual phase")
    if len(args.checkpoint) != len(args.start_tick):
        parser.error("one --start-tick is required for each checkpoint")
    binary = args.binary.resolve()
    checkpoint_binary = args.counterfactual_checkpoint_binary.resolve() if args.counterfactual_checkpoint_binary else binary
    checkpoint_tag = f'-{digest(checkpoint_binary)[:12]}' if checkpoint_binary != binary else ''
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    scenarios = [(f.resolve(), start) for f, start in zip(args.checkpoint, args.start_tick)]
    if len({f.name for f, _ in scenarios}) != len(scenarios):
        parser.error("checkpoint filenames must be unique within an evidence directory")
    manifest = {"binary": str(binary), "binary_sha256": digest(binary), "platform": platform.platform(),
                "fixtures": [{"path": str(f), "start_tick": start, "sha256": digest(f)} for f, start in scenarios],
                "arguments": vars(args) | {"binary": str(binary), "checkpoint": [str(f) for f, _ in scenarios], "output": str(args.output),
                    "counterfactual_checkpoint_binary": str(checkpoint_binary) if args.counterfactual_checkpoint_binary else None}}
    if args.counterfactual_checkpoint_binary:
        manifest['checkpoint_binary'] = str(checkpoint_binary)
        manifest['checkpoint_binary_sha256'] = digest(checkpoint_binary)
    manifest_path = args.output / "manifest.json"
    with (args.output / "manifest.lock").open("a") as manifest_lock:
        fcntl.flock(manifest_lock, fcntl.LOCK_EX)
        if manifest_path.exists():
            previous = json.loads(manifest_path.read_text())
            if previous["binary_sha256"] != manifest["binary_sha256"]:
                raise RuntimeError("binary changed: use a new evidence directory")
            if previous.get('checkpoint_binary_sha256') and previous['checkpoint_binary_sha256'] != manifest.get('checkpoint_binary_sha256'):
                raise RuntimeError('checkpoint binary changed: use a new evidence directory')
            old_fixtures = {f["path"]: f["sha256"] for f in previous["fixtures"]}
            if any(f["path"] in old_fixtures and old_fixtures[f["path"]] != f["sha256"] for f in manifest["fixtures"]):
                raise RuntimeError("checkpoint changed: use a new evidence directory")
            # A completed workload can start its paired continuations while other
            # workload audits run. Keep the complete source inventory in the manifest.
            fixtures = {f["path"]: f for f in previous["fixtures"]}
            fixtures.update({f["path"]: f for f in manifest["fixtures"]})
            manifest["fixtures"] = list(fixtures.values())
        temporary_manifest = manifest_path.with_suffix(".json.tmp")
        temporary_manifest.write_text(json.dumps(manifest, indent=2) + "\n")
        temporary_manifest.replace(manifest_path)
    def process_workload(fixture, start):
        base = args.output / fixture.name
        base.mkdir(exist_ok=True)
        if args.phase == "verify":
            checks = []
            for delay in (0, 2, 4, 8):
                reference = None
                for workers in (0, 1, 2, 4, 8):
                    for audit in (False, True) if workers == 4 else (False,):
                        output = base / f"verify-d{delay}-w{workers}-audit{int(audit)}"
                        row = run(binary, fixture, start + args.ticks, output, workers, delay, audit, True)
                        if reference is None:
                            reference = row["hashes"]
                        if row["hashes"] != reference:
                            raise RuntimeError(f"worker-count/audit simulation mismatch: {output}")
                        checks.append(row)
                        print("PASS", fixture.name, delay, workers, audit, flush=True)
            (base / "verification.json").write_text(json.dumps(checks, indent=2) + "\n")
        elif args.phase == "timing":
            variants = [(delay, workers) for delay in (0, 2, 4, 8) for workers in (1, 2, 4)]
            for delay, workers in variants:
                run(binary, fixture, start + args.ticks, base / f"warmup-d{delay}-w{workers}", workers, delay)
            accepted, attempts = [], []
            for repeat in range(args.repeats):
                order = variants[repeat % len(variants):] + variants[:repeat % len(variants)]
                if repeat % 2:
                    order.reverse()
                for attempt in range(3):
                    rows = []
                    for delay, workers in order:
                        output = base / f"timing-{repeat}-{attempt}-d{delay}-w{workers}"
                        row = run(binary, fixture, start + args.ticks, output, workers, delay)
                        row.update(repeat=repeat, attempt=attempt)
                        rows.append(row)
                        print("TIME", fixture.name, repeat, delay, workers, round(row["result"]["run_ns"] / 1e9, 3), flush=True)
                    quiet = max(r.get("background_cores", 0) for r in rows) <= args.quiet_cores
                    attempts.append({"repeat": repeat, "attempt": attempt, "quiet": quiet, "rows": rows})
                    (base / "timing-attempts.json").write_text(json.dumps(attempts, indent=2) + "\n")
                    if quiet:
                        accepted.extend(rows)
                        (base / "timings.json").write_text(json.dumps(accepted, indent=2) + "\n")
                        break
                else:
                    raise RuntimeError("host stayed busy for three complete timing rounds")
        elif args.phase == "audit":
            for delay in (0, 2, 4, 8):
                output = base / f"audit-d{delay}"
                run(binary, fixture, start + args.ticks, output, 4, delay, True)
                print("AUDIT", fixture.name, delay, flush=True)
        else:
            previous_path = base / "counterfactuals.json"
            previous_cases = json.loads(previous_path.read_text()) if previous_path.exists() else []
            validated = {(r['delay'], r['kind'], json.dumps(r['case'], sort_keys=True)): r
                         for r in previous_cases}
            for delay in (args.counterfactual_delay,) if args.counterfactual_delay else (2, 4, 8):
                cases = []
                audit_record = json.loads((base / f"audit-d{delay}" / "measurement.json").read_text())
                audit = audit_record["impact"]
                reference_end = counterfactual_reference_end(audit)
                extended_observations = reference_end is not None and reference_end > int(audit_record['result']['ticks'])
                for kind, inventory in audit["case_inventory"].items():
                    for number, case in enumerate(inventory):
                        if selected_events and (int(case['tick']), int(case['event'])) not in selected_events:
                            continue
                        key = (delay, kind, json.dumps(case, sort_keys=True))
                        if key in validated:
                            cases.append(validated[key])
                            continue
                        tick, event = int(case["tick"]), int(case["event"])
                        # Multiple candidate and resource-site rows can describe one decision.
                        # Share its executions while retaining each affected lifetime separately.
                        normal_dir = base / f"normal-d{delay}-reference"
                        fresh_dir = base / f"fresh-d{delay}-t{tick}-e{event}"
                        # The uninterrupted audit already contains the normal
                        # outcomes. Replay its state/trace cheaply, and bound
                        # those retained observations to the same 512-tick
                        # horizon instead of rebuilding every oracle twice.
                        normal = run(binary, fixture, reference_end, normal_dir, 4, delay, extended_observations, True)
                        event_offset = 0
                        checkpoint_proof = None
                        checkpoint_rejection = None
                        normal_observations = normal_dir if extended_observations else base / f'audit-d{delay}'
                        use_checkpoint = (args.counterfactual_checkpoints and tick - start > 512
                                          and not (fresh_dir / 'measurement.json').exists())
                        if use_checkpoint:
                            boundary = tick - 1
                            checkpoint_dir = base / f'checkpoint{checkpoint_tag}-d{delay}-t{boundary}'
                            checkpoint = run(checkpoint_binary, fixture, boundary, checkpoint_dir, 4, delay, False, True)
                            prefix_ticks = verify_trace_window(normal_dir, checkpoint_dir)
                            checkpoint_file = checkpoint_dir / 'final.game.gz'
                            resumed_normal = base / f'normal{checkpoint_tag}-d{delay}-t{tick}-resumed'
                            normal_snapshot = run(checkpoint_binary, checkpoint_file, tick + 512, resumed_normal,
                                                  4, delay, True, True, resume=True)
                            matched_ticks, rejection = verify_optional_checkpoint(normal_dir, resumed_normal)
                            if rejection:
                                # A pre-existing controller cache can make a late
                                # save unsuitable for causal replay. Fall back to
                                # the common workload checkpoint and verify every
                                # pre-intervention tick; never accept a diverged pair.
                                checkpoint_rejection = dict(boundary_tick=boundary,
                                    reason=rejection, directory=str(resumed_normal),
                                    checkpoint_binary_sha256=digest(checkpoint_binary))
                                local_event = None
                            else:
                                local_event = resumed_case_event(resumed_normal, case)
                            if local_event is not None:
                                fresh_dir = base / f'fresh{checkpoint_tag}-d{delay}-t{tick}-e{event}-resumed'
                                event_offset = event - local_event
                                fresh = run(checkpoint_binary, checkpoint_file, tick + 512, fresh_dir, 4, delay,
                                            True, True, f'{tick}:{local_event}', resume=True)
                                if normal_snapshot['hashes']['initial'] != fresh['hashes']['initial']:
                                    raise RuntimeError('counterfactual initial states differ')
                                decision_rows = verify_decision_prefix(resumed_normal, fresh_dir, local_event)
                                normal_observations = resumed_normal
                                checkpoint_proof = dict(boundary_tick=boundary,
                                    reference_binary_sha256=manifest['binary_sha256'],
                                    checkpoint_binary_sha256=digest(checkpoint_binary),
                                    checkpoint_sha256=checkpoint['hashes']['save'],
                                    checkpoint_file_sha256=digest(checkpoint_file),
                                    matched_reference_ticks=matched_ticks, matched_decision_rows=decision_rows,
                                    matching_initial_state_sha256=fresh['hashes']['initial'],
                                    local_event=local_event, event_offset=event_offset,
                                    trip_metric_scope='Observed after the common checkpoint; distance and elapsed differences retain the shared start. Initial heading and earlier trip duration may be left censored.')
                        if checkpoint_proof is None:
                            fresh = run(binary, fixture, tick + 512, fresh_dir, 4, delay, True, True, f"{tick}:{event}")
                            prefix_ticks = verify_counterfactual_prefix(normal_dir, fresh_dir, tick)
                        observation_end = tick + 512
                        a_observed = case_observations(normal_observations, kind, case,
                                                       observation_end, event_offset=event_offset)
                        b_observed = case_observations(fresh_dir, kind, case,
                                                       observation_end, event_offset=event_offset)
                        outcomes = {}
                        for name in ("hired", "harvested", "market_acquired", "acquired", "delivered"):
                            a, b = a_observed[name], b_observed[name]
                            applicable = name != 'hired' or kind in ('hiring', 'hiring_candidate')
                            if name in ('harvested', 'market_acquired'):
                                applicable = bool(a or b or not a_observed['acquired'] or not b_observed['acquired'])
                            outcomes[name] = {"normal": a, "fresh": b, "censored": a is None or b is None,
                                              "applicable": applicable,
                                              "additional_ticks": int(a["tick"]) - int(b["tick"]) if a and b else None}
                        cases.append({"delay": delay, "kind": kind, "case": case, "outcomes": outcomes,
                                      "trip_endpoints": {"normal": a_observed['trip_endpoint'], "fresh": b_observed['trip_endpoint']},
                                      "hiring_episode_endpoints": {"normal": a_observed['hiring_episode_endpoint'],
                                                                   "fresh": b_observed['hiring_episode_endpoint']},
                                      "normal_directory": str(normal_dir), "fresh_directory": str(fresh_dir),
                                      "normal_observations_directory": str(normal_observations),
                                      "checkpoint_proof": checkpoint_proof, "checkpoint_rejection": checkpoint_rejection,
                                      "observation_end_tick": observation_end,
                                      "event_offset": event_offset,
                                      "decision_binary_sha256": digest(checkpoint_binary) if checkpoint_proof else manifest['binary_sha256'],
                                      "matching_prefix_ticks": prefix_ticks, "normal_trace": normal["hashes"]["trace"], "fresh_trace": fresh["hashes"]["trace"]})
                        save_case_progress(previous_path, cases, delay)
                        print("CASE", fixture.name, delay, kind, number, flush=True)
                save_case_progress(previous_path, cases, delay)

    with ThreadPoolExecutor(max_workers=args.workload_jobs) as executor:
        futures = [executor.submit(process_workload, fixture, start) for fixture, start in scenarios]
        for future in futures:
            future.result()


if __name__ == "__main__":
    main()
