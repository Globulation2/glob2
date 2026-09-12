#!/usr/bin/env python3
"""Run deterministic paired Maxima ON/OFF rollouts from saved checkpoints."""

from __future__ import annotations

import argparse
import csv
import hashlib
import gzip
import json
import math
import re
import shlex
import subprocess
import sys
import tarfile
import threading
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from queue import Empty, Queue
from statistics import NormalDist, mean, stdev
from typing import Any

import nicowar_tournament_signal as scoring
import optimize_maxima_portfolio as portfolio


ROOT = Path(__file__).resolve().parent.parent
REMOTE_WORKERS = tuple(
    worker for worker in portfolio.STANDARD_WORKERS if worker[0] != "local"
)
MARKER_RE = re.compile(r"^(MAXIMA_CHECKPOINT_(?:START|RESULT))\t(.*)$")
WAVES = {
    1: (
        "farming.enabled", "recon.enabled", "tactics.enabled",
        "colonization.enabled", "defense.reactive.enabled",
        "military.preemptive_defense_enabled",
        "military.explorer_defense_enabled",
        "economy.food_service_safeguards_enabled", "upgrades.enabled",
        "emergencies.food_enabled", "emergencies.colony_enabled",
        "food.enabled",
        "postures.recover_enabled", "postures.defend_enabled",
        "postures.expand_enabled", "postures.develop_enabled",
        "postures.mobilize_enabled", "postures.campaign_enabled",
        "postures.finish_enabled",
    ),
    2: (
        "economy.large_economy_adaptation_enabled",
        "economy.amphibious_network_maintenance_enabled",
        "economy.worker_birth_throttle_enabled", "repairs.enabled",
        "military.warrior_training_backlog_throttle_enabled",
        "placement.food_preservation_enabled",
        "placement.defensive_siting_enabled", "placement.artery_routing_enabled",
        "tactics.siege_enabled", "tactics.dig_out_enabled", "raiding.enabled",
        "explorer_campaign.enabled",
        "recon.scouting_missions_enabled", "recon.force_memory_enabled",
        "farming.farm_protection_enabled",
        "farming.resource_preserving_circulation_enabled",
        "farming.maintenance_clearing_enabled",
        "farming.proactive_clearing_enabled",
        "food.target_capping_enabled",
    ),
    3: (
        "economy.swarm_retirement_enabled", "food.retirement_enabled",
        "military.preemptive_amphibious_enabled",
        "placement.spacing_compactness_enabled",
        "tactics.failed_target_quarantine_enabled",
        "fruit.enabled",
        "recon.economic_watch_enabled",
        "farming.wheat_invasion_clearing_enabled",
        "farming.wood_firebreak_enabled",
    ),
}
ALL_SWITCHES = tuple(value for wave in WAVES.values() for value in wave)
SCORE_METRICS = {
    "victory_score": 3.0,
    "economy_advantage": 0.03,
    "military_advantage": 0.03,
    "resilience_advantage": 0.03,
    "prestige_advantage": 0.03,
}

# Two-sided 95% Student-t critical values. Small pilots must not acquire false
# precision from a normal approximation; beyond 30 df the values converge on
# the normal critical value.
T_975 = (
    0.0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262,
    2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093,
    2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045,
    2.042,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="JSON, JSONL, or CSV checkpoint manifest")
    parser.add_argument("--binary", type=Path, default=ROOT / "build-tournament/src/glob2")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--switch", action="append", dest="switches")
    parser.add_argument("--horizon", type=int, help="Override manifest rollout ticks")
    parser.add_argument("--repetitions", type=int, default=2,
                        help="Same-arm qualification runs per checkpoint")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--metric", choices=sorted(SCORE_METRICS),
                        help="Override every manifest row's primary outcome")
    parser.add_argument("--mpid", type=float,
                        help="Override every row's minimum practical difference")
    parser.add_argument("--minimum-blocks", type=int, default=32,
                        help="Hard floor before a result can be considered powered")
    parser.add_argument("--confirmation", action="store_true",
                        help="Mark this as the fresh confirmation bank")
    parser.add_argument("--worker", action="append", type=portfolio.parse_worker,
                        dest="workers", help="Cluster slot allocation HOST:JOBS; repeat per host")
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer",
                        help="Checkout path used by SSH workers")
    parser.add_argument("--allow-missing-workers", action="store_true",
                        help="Continue if configured cluster slots cannot be reached")
    args = parser.parse_args()
    if args.repetitions < 2:
        parser.error("repetitions must be at least 2 for deterministic qualification")
    if args.timeout < 1 or (args.mpid is not None and args.mpid <= 0) or args.minimum_blocks < 2:
        parser.error("timeout and mpid must be positive; minimum-blocks must be at least 2")
    unknown = sorted(set(args.switches or ()) - set(ALL_SWITCHES))
    if unknown:
        parser.error(f"unknown switches: {', '.join(unknown)}")
    if not args.workers:
        args.workers = list(REMOTE_WORKERS)
    return args


def default_metric(switch: str) -> str:
    economic_prefixes = (
        "farming.", "economy.", "staffing.", "placement.", "colonization.",
    )
    if switch.startswith(economic_prefixes) or switch in {"upgrades.enabled", "fruit.enabled"}:
        return "economy_advantage"
    if switch.startswith("defense.") or switch.startswith("emergencies.") or switch == "repairs.enabled":
        return "resilience_advantage"
    return "victory_score"


def read_manifest(path: Path) -> list[dict[str, Any]]:
    if path.suffix == ".csv":
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
    elif path.suffix in {".jsonl", ".ndjson"}:
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
                if line.strip()]
    else:
        payload = json.loads(path.read_text(encoding="utf-8"))
        rows = payload["checkpoints"] if isinstance(payload, dict) else payload
    required = {"checkpoint_id", "path", "source_block", "focal_player", "focal_team", "switch"}
    for index, row in enumerate(rows, 1):
        missing = required - set(row)
        if missing:
            raise ValueError(f"manifest row {index} missing: {', '.join(sorted(missing))}")
        if row["switch"] not in ALL_SWITCHES:
            raise ValueError(f"manifest row {index} has unknown switch {row['switch']!r}")
        row["focal_player"] = int(row["focal_player"])
        row["focal_team"] = int(row["focal_team"])
        row["horizon"] = int(row.get("horizon", 20000))
        row["metric"] = str(row.get("metric", default_metric(str(row["switch"]))))
        if row["metric"] not in SCORE_METRICS:
            raise ValueError(f"manifest row {index} has unknown metric {row['metric']!r}")
        row["mpid"] = float(row.get("mpid", SCORE_METRICS[row["metric"]]))
        if row["mpid"] <= 0:
            raise ValueError(f"manifest row {index} has nonpositive mpid")
        checkpoint = Path(str(row["path"]))
        if not checkpoint.is_absolute():
            checkpoint = (path.parent / checkpoint).resolve()
        row["path"] = str(checkpoint)
        if not checkpoint.is_file():
            raise ValueError(f"manifest row {index} checkpoint does not exist: {checkpoint}")
    return rows


def marker_fields(output: str) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for line in output.splitlines():
        match = MARKER_RE.match(line)
        if not match:
            continue
        values: dict[str, str] = {}
        for field in match.group(2).split("\t"):
            if "=" in field:
                key, value = field.split("=", 1)
                values[key] = value
        result[match.group(1)] = values
    return result


def score_rows(output: str, start_tick: int) -> list[dict[str, Any]]:
    rows = []
    for line in output.splitlines():
        parsed = scoring.parse_score_line(line)
        if parsed is not None and int(parsed["tick"]) >= start_tick:
            parsed["tick"] = int(parsed["tick"]) - start_tick
            rows.append(parsed)
    return rows


def rollout_score(rows: list[dict[str, Any]], focal_team: int, horizon: int,
                  allied_teams: list[int] | None = None) -> dict[str, Any]:
    teams = {int(row["team"]) for row in rows}
    focal_side = set(allied_teams or [focal_team])
    if focal_team not in focal_side or not focal_side <= teams:
        raise ValueError("invalid focal alliance in checkpoint manifest")
    opponents = teams - focal_side
    final = {int(row["team"]): row for row in rows if int(row["tick"]) == max(
        (int(value["tick"]) for value in rows), default=0
    )}
    side_rows = [final.get(team, {}) for team in focal_side]
    outcome = (1 if any(int(row.get("won", 0)) for row in side_rows) else
               -1 if all(int(row.get("lost", 0)) for row in side_rows) else 0)
    steps = max((int(row["tick"]) for row in rows), default=0)
    match = {"score_telemetry": rows, "steps": steps, "max_steps": horizon}
    if not opponents:
        raise ValueError("checkpoint telemetry contains no opponent team")
    return scoring.pair_score(match, focal_side, opponents, outcome)


def command_for(binary: str, checkpoint_path: str, checkpoint: dict[str, Any],
                arm: bool, horizon: int) -> list[str]:
    setting = f"{checkpoint['switch']}={'true' if arm else 'false'}"
    return [
        binary, "--maxima-checkpoint-run", checkpoint_path, str(horizon),
        "--maxima-player-overrides", str(checkpoint["focal_player"]), setting,
    ]


def write_run_log(path: Path, output: str) -> Path:
    """Publish compressed logs atomically to bound campaign disk usage."""
    target = path.with_suffix(path.suffix + ".gz")
    pending = target.with_suffix(target.suffix + ".pending")
    with gzip.open(pending, "wt", encoding="utf-8", compresslevel=1) as handle:
        handle.write(output)
    pending.replace(target)
    return target


def read_run_log(path: Path) -> str | None:
    compressed = path.with_suffix(path.suffix + ".gz")
    if compressed.is_file():
        try:
            with gzip.open(compressed, "rt", encoding="utf-8", errors="replace") as handle:
                return handle.read()
        except (OSError, EOFError):
            return None
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else None


def parse_run_output(checkpoint: dict[str, Any], arm: bool, repetition: int,
                     horizon: int, output: str, returncode: int, error: str,
                     started: float, log_dir: Path, worker_host: str) -> dict[str, Any]:
    arm_name = "on" if arm else "off"
    log_path = log_dir / f"{checkpoint['checkpoint_id']}-{arm_name}-{repetition}.log"
    log_path = write_run_log(log_path, output)
    markers = marker_fields(output)
    start = markers.get("MAXIMA_CHECKPOINT_START", {})
    finish = markers.get("MAXIMA_CHECKPOINT_RESULT", {})
    if not start or not finish:
        error = error or "missing checkpoint start/result markers"
    telemetry = score_rows(output, int(start.get("tick", 0))) if start else []
    score: dict[str, Any] = {}
    if not error:
        try:
            score = rollout_score(telemetry, int(checkpoint["focal_team"]), horizon,
                                  checkpoint.get("allied_teams"))
        except (TypeError, ValueError) as exc:
            error = str(exc)
    return {
        "checkpoint_id": checkpoint["checkpoint_id"],
        "source_block": checkpoint["source_block"], "switch": checkpoint["switch"],
        "arm": arm_name, "repetition": repetition, "horizon": horizon,
        "focal_player": checkpoint["focal_player"], "focal_team": checkpoint["focal_team"],
        "returncode": returncode, "error": error,
        "status": "failed" if error else "completed", "worker_host": worker_host,
        "wall_seconds": round(time.monotonic() - started, 3),
        "start_tick": int(start.get("tick", 0)) if start else None,
        "start_checksum": start.get("checksum"), "start_rng": start.get("rng"),
        "final_tick": int(finish.get("tick", 0)) if finish else None,
        "final_checksum": finish.get("checksum"), "final_rng": finish.get("rng"),
        "telemetry_rows": len(telemetry), "log": str(log_path), **score,
    }


class AblationCluster(portfolio.PortfolioCluster):
    """Run checkpoint branches through the established heterogeneous SSH queue."""

    def __init__(self, binary: Path, repo_root: Path, workers: list[tuple[str, int]],
                 remote_root: str, timeout: int, require_all_workers: bool,
                 log_dir: Path, campaign: str) -> None:
        super().__init__(binary, repo_root, workers, remote_root, timeout, retries=1,
                         require_all_workers=require_all_workers)
        self.log_dir = log_dir
        self.remote_checkpoint_dir = (
            f"{self.remote_root}/.maxima-ablation/{campaign}/checkpoints"
        )
        self._staged: set[tuple[str, str]] = set()
        self.result_sink = self._normalize_failure

    def close(self) -> None:
        for host, group in list(self.prepared_connections):
            command = self._ssh_arguments(host, group) + ["-O", "exit", host]
            closed = subprocess.run(command, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, timeout=5)
            if closed.returncode != 0:
                pattern = f"^ssh .*ControlPath={self._control_path(host, group)} "
                subprocess.run(["pkill", "-TERM", "-f", pattern],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.prepared_connections.clear()

    def _normalize_failure(self, result: dict[str, Any]) -> dict[str, Any]:
        if "checkpoint_id" in result:
            return result
        checkpoint = result["checkpoint"]
        normalized = parse_run_output(
            checkpoint, bool(result["arm"]), int(result["repetition"]),
            int(result["horizon"]), str(result.get("output", "")),
            int(result.get("returncode", 1)), str(result.get("error", "worker failed")),
            time.monotonic(), self.log_dir, str(result.get("worker_host", "unknown")),
        )
        normalized["id"] = result["id"]
        return normalized

    def _active_slots(self) -> list[tuple[str, int]]:
        usable: set[tuple[str, int]] = set()
        lock = threading.Lock()

        def prepare_host(host: str) -> None:
            groups = sorted(group for slot_host, group in self.slots if slot_host == host)

            def prepare(group: int) -> None:
                for attempt in range(3):
                    if self._prepare_connection(host, group):
                        with lock:
                            usable.add((host, group))
                        return
                    time.sleep(0.5 * (attempt + 1))

            with ThreadPoolExecutor(max_workers=min(2, len(groups))) as executor:
                futures = [executor.submit(prepare, group) for group in groups]
                for future in futures:
                    future.result()

        with ThreadPoolExecutor(max_workers=len(self.workers)) as executor:
            futures = [executor.submit(prepare_host, host) for host, _count in self.workers]
            for future in futures:
                future.result()
        active = [slot for slot in self.slots if slot in usable]
        if not active:
            raise RuntimeError("no ablation workers are currently reachable")
        if self.require_all_workers and len(active) != len(self.slots):
            missing = sorted(set(self.slots) - set(active))
            raise RuntimeError(f"required worker connections unavailable: {missing}")
        return active

    def run(self, schedule: list[dict[str, Any]], settings: str) -> list[dict[str, Any]]:
        """Keep every checkpoint's ON/OFF repetitions on one fixed worker slot."""
        del settings
        grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for item in schedule:
            grouped[str(item["checkpoint"]["checkpoint_id"])].append(item)
        pending: Queue[list[dict[str, Any]]] = Queue()
        for checkpoint_id in sorted(grouped):
            pending.put(sorted(grouped[checkpoint_id], key=lambda row: int(row["id"])))
        results: list[dict[str, Any]] = []
        results_lock = threading.Lock()

        def consume(slot: tuple[str, int]) -> None:
            host, group = slot
            while True:
                try:
                    items = pending.get_nowait()
                except Empty:
                    return
                try:
                    block_results = []
                    for item in items:
                        try:
                            result = self._run_one(host, group, item, "")
                        except Exception as exc:
                            failed = dict(item)
                            failed.update({
                                "status": "failed", "returncode": 1,
                                "error": f"worker exception: {exc}", "output": "",
                                "worker_host": host,
                            })
                            result = self._normalize_failure(failed)
                        result["worker_slot"] = group
                        block_results.append(result)
                    with results_lock:
                        results.extend(block_results)
                finally:
                    pending.task_done()

        active_slots = self._active_slots()
        with ThreadPoolExecutor(max_workers=len(active_slots)) as executor:
            futures = [executor.submit(consume, slot) for slot in active_slots]
            for future in futures:
                future.result()
        return sorted(results, key=lambda row: int(row["id"]))

    def _remote_checkpoint_path(self, local_path: Path) -> str:
        digest = hashlib.sha256(str(local_path).encode()).hexdigest()[:16]
        return f"{self.remote_checkpoint_dir}/{digest}-{local_path.name}"

    def prestage(self, checkpoints: list[dict[str, Any]]) -> None:
        """Copy the unique bank once per host so the hot loop is compute-only."""
        unique_paths = sorted({Path(row["path"]).resolve() for row in checkpoints})
        archive = self.log_dir.parent / "checkpoint-stage.tar"
        with tarfile.open(archive, "w") as bundle:
            for local_path in unique_paths:
                remote_name = Path(self._remote_checkpoint_path(local_path)).name
                bundle.add(local_path, arcname=remote_name, recursive=False)

        hosts = sorted({host for host, _count in self.workers if host != "local"})

        def stage_host(host: str) -> tuple[str, str]:
            remote_archive = f"{self.remote_checkpoint_dir}.tar"
            prepare = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host,
                       f"mkdir -p {shlex.quote(str(Path(self.remote_checkpoint_dir).parent))}"]
            made = subprocess.run(prepare, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=30)
            if made.returncode != 0:
                return host, made.stdout.strip() or f"ssh exited {made.returncode}"
            copied = subprocess.run(
                ["scp", "-o", "BatchMode=yes", str(archive), f"{host}:{remote_archive}"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
            )
            if copied.returncode != 0:
                return host, copied.stdout.strip() or f"scp exited {copied.returncode}"
            unpack = (f"rm -rf -- {shlex.quote(self.remote_checkpoint_dir)} && "
                      f"mkdir -p {shlex.quote(self.remote_checkpoint_dir)} && "
                      f"tar -xf {shlex.quote(remote_archive)} "
                      f"-C {shlex.quote(self.remote_checkpoint_dir)} && "
                      f"rm -f -- {shlex.quote(remote_archive)}")
            extracted = subprocess.run(
                ["ssh", "-o", "BatchMode=yes", host, unpack], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
            )
            return host, "" if extracted.returncode == 0 else (
                extracted.stdout.strip() or f"unpack exited {extracted.returncode}"
            )

        errors = {}
        try:
            with ThreadPoolExecutor(max_workers=len(hosts)) as executor:
                futures = [executor.submit(stage_host, host) for host in hosts]
                for future in as_completed(futures):
                    host, error = future.result()
                    if error:
                        errors[host] = error
                    else:
                        for local_path in unique_paths:
                            self._staged.add((host, self._remote_checkpoint_path(local_path)))
        finally:
            archive.unlink(missing_ok=True)
        if errors and self.require_all_workers:
            raise RuntimeError(f"checkpoint pre-staging failed: {json.dumps(errors, sort_keys=True)}")
        if errors:
            self.workers = [worker for worker in self.workers if worker[0] not in errors]
            self.slots = [slot for slot in self.slots if slot[0] not in errors]
            if not self.slots:
                raise RuntimeError("checkpoint pre-staging left no usable workers")

    def _stage_checkpoint(self, host: str, group: int, local_path: Path) -> str:
        del group
        remote_path = self._remote_checkpoint_path(local_path)
        if (host, remote_path) not in self._staged:
            raise OSError(f"checkpoint was not pre-staged on {host}: {local_path.name}")
        return remote_path

    def _execute(self, host: str, group: int, match: dict[str, Any],
                 settings: str) -> dict[str, Any]:
        del settings
        started = time.monotonic()
        checkpoint = match["checkpoint"]
        arm = bool(match["arm"])
        repetition = int(match["repetition"])
        horizon = int(match["horizon"])
        if host == "local":
            command = command_for(str(self.binary), checkpoint["path"], checkpoint, arm, horizon)
            process = subprocess.run(command, cwd=self.repo_root, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=self.timeout)
        else:
            remote_checkpoint = self._stage_checkpoint(
                host, group, Path(checkpoint["path"]).resolve()
            )
            remote_binary = f"{self.remote_root}/build-tournament/src/glob2"
            command = command_for(remote_binary, remote_checkpoint, checkpoint, arm, horizon)
            remote = (f"cd {shlex.quote(self.remote_root)} && "
                      f"LD_LIBRARY_PATH={shlex.quote(self.remote_root + '/lib')} "
                      f"{shlex.join(command)}")
            process = subprocess.run(self._ssh_arguments(host, group) + [host, remote],
                                     text=True, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, timeout=self.timeout)
        error = "" if process.returncode == 0 else f"engine exited {process.returncode}"
        result = parse_run_output(checkpoint, arm, repetition, horizon, process.stdout,
                                  process.returncode, error, started, self.log_dir, host)
        result["id"] = match["id"]
        return result


def cached_run(checkpoint: dict[str, Any], arm: bool, repetition: int,
               horizon: int, log_dir: Path, job_id: int) -> dict[str, Any] | None:
    arm_name = "on" if arm else "off"
    path = log_dir / f"{checkpoint['checkpoint_id']}-{arm_name}-{repetition}.log"
    output = read_run_log(path)
    if output is None:
        return None
    markers = marker_fields(output)
    if "MAXIMA_CHECKPOINT_START" not in markers or "MAXIMA_CHECKPOINT_RESULT" not in markers:
        return None
    result = parse_run_output(checkpoint, arm, repetition, horizon, output, 0, "",
                              time.monotonic(), log_dir, "resume-cache")
    if result["error"]:
        return None
    result["id"] = job_id
    return result


def qualification(rows: list[dict[str, Any]]) -> tuple[bool, str]:
    if any(row["error"] for row in rows):
        return False, "; ".join(sorted({row["error"] for row in rows if row["error"]}))
    starts = {(row["start_tick"], row["start_checksum"], row["start_rng"]) for row in rows}
    if len(starts) != 1:
        return False, "ON/OFF branches did not start from identical state and RNG"
    for arm in ("on", "off"):
        arm_rows = [row for row in rows if row["arm"] == arm]
        if len({row["repetition"] for row in arm_rows}) < 2:
            return False, f"fewer than two {arm.upper()} qualification repetitions"
        endings = {(row["final_tick"], row["final_checksum"], row["final_rng"],
                    row.get("victory_score")) for row in arm_rows}
        if len(endings) != 1:
            return False, f"repeated {arm.upper()} branches were not deterministic"
    return True, ""


def interval(values: list[float]) -> tuple[float, float, float]:
    center = mean(values)
    if len(values) < 2:
        return center, -math.inf, math.inf
    degrees = len(values) - 1
    critical = T_975[degrees] if degrees < len(T_975) else 1.96
    margin = critical * stdev(values) / math.sqrt(len(values))
    return center, center - margin, center + margin


def verdict(center: float, low: float, high: float, mpid: float,
            confirmation: bool, adequately_powered: bool = True,
            multiplicity_resolved: bool = True) -> tuple[str, str]:
    if not adequately_powered:
        return "underpowered", "keep enabled"
    if high < -mpid:
        if not multiplicity_resolved:
            return "harmful before FDR correction", "keep enabled"
        name = "confirmed harmful" if confirmation else "harmful; needs confirmation"
        return name, "disable" if confirmation else "keep enabled"
    if low > mpid:
        return "helpful", "keep enabled"
    if low >= -mpid and high <= mpid:
        return "practically neutral", "keep enabled"
    if high < 0 or low > 0:
        return "statistically directional but practically unresolved", "keep enabled"
    return "inconclusive", "keep enabled"


def required_blocks(values: list[float], mpid: float, minimum: int,
                    power: float = 0.90) -> int:
    """Estimate paired blocks needed for an MPID-sized effect at target power."""
    if len(values) < 2:
        return minimum
    sigma = stdev(values)
    z_alpha = NormalDist().inv_cdf(0.975)
    z_power = NormalDist().inv_cdf(power)
    estimate = math.ceil(((z_alpha + z_power) * sigma / mpid) ** 2)
    return max(minimum, estimate)


def harmful_p_value(values: list[float], mpid: float) -> float:
    """One-sided normal-approximation p-value for mean difference < -MPID.

    This is used only after the hard 32-block floor; the Student-t confidence
    interval remains the per-switch resolution criterion.
    """
    if len(values) < 2:
        return 1.0
    sigma = stdev(values)
    center = mean(values)
    if sigma == 0:
        return 0.0 if center < -mpid else 1.0
    statistic = (center + mpid) / (sigma / math.sqrt(len(values)))
    return NormalDist().cdf(statistic)


def benjamini_hochberg(p_values: dict[str, float], q: float) -> set[str]:
    ordered = sorted(p_values.items(), key=lambda item: (item[1], item[0]))
    cutoff = -1
    total = len(ordered)
    for index, (_name, value) in enumerate(ordered, 1):
        if value <= q * index / max(1, total):
            cutoff = index
    return {name for name, _value in ordered[:cutoff]} if cutoff >= 0 else set()


def write_outputs(output_dir: Path, runs: list[dict[str, Any]], checkpoints: list[dict[str, Any]],
                  confirmation: bool, minimum_blocks: int) -> None:
    with (output_dir / "runs.jsonl").open("w", encoding="utf-8") as handle:
        for row in runs:
            handle.write(json.dumps(row, sort_keys=True) + "\n")
    by_checkpoint: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in runs:
        by_checkpoint[str(row["checkpoint_id"])].append(row)
    paired = []
    rejected = []
    for checkpoint in checkpoints:
        rows = by_checkpoint[str(checkpoint["checkpoint_id"])]
        valid, reason = qualification(rows)
        if not valid:
            rejected.append({"checkpoint_id": checkpoint["checkpoint_id"], "reason": reason})
            continue
        metric = str(checkpoint["metric"])
        on = mean(float(row[metric]) for row in rows if row["arm"] == "on")
        off = mean(float(row[metric]) for row in rows if row["arm"] == "off")
        paired.append({
            "checkpoint_id": checkpoint["checkpoint_id"],
            "source_block": checkpoint["source_block"], "switch": checkpoint["switch"],
            "metric": metric, "mpid": checkpoint["mpid"],
            "on_value": round(on, 8), "off_value": round(off, 8),
            "difference": round(on - off, 8),
        })
    with (output_dir / "paired.csv").open("w", newline="", encoding="utf-8") as handle:
        fields = ["checkpoint_id", "source_block", "switch", "metric", "mpid",
                  "on_value", "off_value", "difference"]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader(); writer.writerows(paired)
    block_values: dict[tuple[str, str], list[float]] = defaultdict(list)
    for row in paired:
        block_values[(row["switch"], str(row["source_block"]))].append(float(row["difference"]))
    summaries = []
    for switch in sorted({row["switch"] for row in paired}):
        switch_rows = [row for row in paired if row["switch"] == switch]
        metrics = {str(row["metric"]) for row in switch_rows}
        mpids = {float(row["mpid"]) for row in switch_rows}
        if len(metrics) != 1 or len(mpids) != 1:
            raise ValueError(f"switch {switch} mixes primary metrics or MPIDs")
        metric = next(iter(metrics))
        mpid = next(iter(mpids))
        values = [mean(group) for (candidate, _), group in block_values.items()
                  if candidate == switch]
        center, low, high = interval(values)
        target_blocks = required_blocks(values, mpid, minimum_blocks)
        adequately_powered = len(values) >= target_blocks
        summaries.append({
            "switch": switch, "metric": metric, "independent_blocks": len(values),
            "mean_difference": center,
            "ci_low": low if math.isfinite(low) else None,
            "ci_high": high if math.isfinite(high) else None,
            "mpid": mpid, "required_blocks_90pct": target_blocks,
            "adequately_powered": adequately_powered,
            "harm_p_value": harmful_p_value(values, mpid),
        })
    fdr_q = 0.05 if confirmation else 0.10
    fdr_resolved = benjamini_hochberg(
        {row["switch"]: row["harm_p_value"] for row in summaries}, fdr_q
    )
    for row in summaries:
        row["fdr_q"] = fdr_q
        row["fdr_resolved"] = row["switch"] in fdr_resolved
        low = row["ci_low"] if row["ci_low"] is not None else -math.inf
        high = row["ci_high"] if row["ci_high"] is not None else math.inf
        name, recommendation = verdict(
            row["mean_difference"], low, high, row["mpid"], confirmation,
            row["adequately_powered"], row["fdr_resolved"],
        )
        row["verdict"] = name
        row["recommendation"] = recommendation
    payload = {
        "confirmation": confirmation, "fdr_q": fdr_q, "runs": len(runs),
        "qualified_checkpoints": len(paired), "rejected_checkpoints": rejected,
        "summaries": summaries,
        "default_rule": "Only a confirmed harmful result disables a switch; every other verdict keeps it enabled.",
    }
    (output_dir / "summary.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    lines = ["# Maxima checkpoint switch ablation", "",
             f"- Qualified checkpoints: {len(paired)} / {len(checkpoints)}",
             f"- Engine runs: {len(runs)}", f"- Confirmation bank: {'yes' if confirmation else 'no'}",
             f"- Harmful-effect FDR threshold: q={fdr_q:.2f}", "",
             "Only a confirmed harmful result recommends disabling a switch. Inconclusive results stay enabled.", "",
             "| Switch | Primary metric | Blocks / required | ON-OFF (95% CI) | FDR | Verdict | Recommendation |",
             "|---|---|---:|---:|---|---|---|"]
    for row in summaries:
        interval_text = ("unbounded" if row["ci_low"] is None else
                         f"{row['ci_low']:+.3f} to {row['ci_high']:+.3f}")
        lines.append(f"| `{row['switch']}` | `{row['metric']}` | "
                     f"{row['independent_blocks']} / "
                     f"{row['required_blocks_90pct']} | "
                     f"{row['mean_difference']:+.3f} ({interval_text}) | "
                     f"{'pass' if row['fdr_resolved'] else 'no'} | "
                     f"{row['verdict']} | {row['recommendation']} |")
    if rejected:
        lines.extend(["", "## Rejected checkpoints", ""])
        lines.extend(f"- `{row['checkpoint_id']}`: {row['reason']}" for row in rejected)
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    binary = args.binary if args.binary.is_absolute() else (ROOT / args.binary)
    binary = binary.resolve()
    if not binary.is_file():
        print(f"binary not found: {binary}", file=sys.stderr); return 2
    manifest = args.manifest.resolve()
    try:
        checkpoints = read_manifest(manifest)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr); return 2
    if args.switches:
        checkpoints = [row for row in checkpoints if row["switch"] in args.switches]
    for checkpoint in checkpoints:
        if args.metric:
            checkpoint["metric"] = args.metric
            if args.mpid is None:
                checkpoint["mpid"] = SCORE_METRICS[args.metric]
        if args.mpid is not None:
            checkpoint["mpid"] = args.mpid
    if not checkpoints:
        print("manifest contains no selected checkpoints", file=sys.stderr); return 2
    output_dir = args.output_dir or manifest.parent / f"ablation-{int(time.time())}"
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    log_dir = output_dir / "logs"; log_dir.mkdir(exist_ok=True)
    jobs = []
    runs = []
    job_id = 0
    for checkpoint in checkpoints:
        horizon = args.horizon or int(checkpoint["horizon"])
        for repetition in range(args.repetitions):
            for arm in (True, False):
                cached = cached_run(checkpoint, arm, repetition, horizon, log_dir, job_id)
                if cached is not None:
                    runs.append(cached)
                else:
                    jobs.append({
                        "id": job_id, "checkpoint": checkpoint, "arm": arm,
                        "repetition": repetition, "horizon": horizon,
                    })
                job_id += 1
    campaign = hashlib.sha256(str(output_dir.resolve()).encode()).hexdigest()[:16]
    cluster = AblationCluster(
        binary, ROOT, args.workers, args.remote_root, args.timeout,
        not args.allow_missing_workers, log_dir, campaign,
    )
    print(f"Running {len(jobs)} checkpoint branches ({len(runs)} reused) on "
          f"{sum(count for _host, count in args.workers)} cluster slots: "
          f"{', '.join(f'{host}:{count}' for host, count in args.workers)}", flush=True)
    try:
        if jobs:
            print("Pre-staging the unique checkpoint bank on every remote host...", flush=True)
            cluster.prestage([job["checkpoint"] for job in jobs])
            runs.extend(cluster.run(jobs, ""))
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        cluster.close()
    runs.sort(key=lambda row: int(row["id"]))
    write_outputs(output_dir, runs, checkpoints, args.confirmation, args.minimum_blocks)
    invalid_runs = sum(bool(row["error"]) for row in runs)
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in runs:
        grouped[str(row["checkpoint_id"])].append(row)
    rejected_checkpoints = sum(not qualification(rows)[0] for rows in grouped.values())
    print(f"Wrote {output_dir}; invalid engine runs: {invalid_runs}; "
          f"rejected checkpoints: {rejected_checkpoints}")
    return 1 if invalid_runs or rejected_checkpoints else 0


if __name__ == "__main__":
    raise SystemExit(main())
