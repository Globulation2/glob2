#!/usr/bin/env python3
"""Optimize Maxima across a balanced portfolio of game formats.

The objective gives equal weight to 1v1, three-, four-, and five-player FFA,
and shared-vision, independent-agent 2v2. Search results are replicated on fresh seeds at the
same simulation horizon; map rotation is optional.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shlex
import subprocess
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path
from queue import Empty, Queue
from statistics import mean, pstdev
from typing import Any, Callable

import optimize_maxima as tuning
import run_nicowar_2v2_tournament as team_runner
import run_nicowar_tournament as ffa_runner
import nicowar_tournament_signal as scoring


SCENARIO_MATCH_PREFIX = "NICOWAR_SCENARIO_MATCH_RESULT\t"
SCENARIO_PLAYER_PREFIX = "NICOWAR_SCENARIO_PLAYER_RESULT\t"
SCENARIO_MAP_PREFIX = "NICOWAR_SCENARIO_MAP\t"
FORMATS = ("duel", "ffa3", "ffa4", "ffa5", "2v2")
PLAYER_COUNTS = {"duel": 2, "ffa3": 3, "ffa4": 4, "ffa5": 5}

# Training and audit maps never overlap. Larger maps deliberately appear in
# multiple player formats because changing density is part of the portfolio.
TRAIN_MAPS = {
    "duel": ("Dejans", "Mazury", "Muka", "balanced for 2", "A big pond", "FourSquares1"),
    "ffa3": ("A big pond", "Easy Three", "Triangle", "G2", "Island of the Renfur"),
    "ffa4": ("FourSquares1", "G2", "Holiday Island 2", "Migration", "Archipelago"),
    "ffa5": ("Archipelago", "Island of the Renfur", "Sand River"),
    "2v2": ("FourSquares1", "G2", "Holiday Island 2", "Migration"),
}
AUDIT_MAPS = {
    "duel": ("SmallForTwo", "strange2", "Garden 3", "Wild River"),
    "ffa3": ("Garden 3", "Centerfolds 2", "Wild River", "Playground"),
    "ffa4": ("Garden 3", "Isles", "balanced", "Centerfolds 2"),
    "ffa5": ("Centerfolds 2", "Wild River", "Playground", "Oazis"),
    "2v2": ("Garden 3", "Isles", "balanced"),
}

# Shared capacity profile for full tournament campaigns. Offline SSH hosts are
# skipped by PortfolioCluster's connection probe, so local-only development
# still works without a separate configuration.
STANDARD_WORKERS = (
    ("local", 8),
    ("pharaoh-dev-1.local", 4),
    ("pharaoh-dev-2.local", 4),
    ("pharaoh-dev-3.local", 4),
    ("devlaptop.local", 16),
    ("therig.local", 32),
)


def parse_worker(value: str) -> tuple[str, int]:
    host, separator, count = value.rpartition(":")
    if not separator:
        raise argparse.ArgumentTypeError("worker must be HOST:JOBS")
    try:
        jobs = int(count)
    except ValueError as error:
        raise argparse.ArgumentTypeError("worker job count must be an integer") from error
    if not host or jobs < 1:
        raise argparse.ArgumentTypeError("worker must have a host and positive job count")
    return host, jobs


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--trials", type=int, default=32)
    parser.add_argument("--startup-trials", type=int, default=10)
    parser.add_argument("--replication-finalists", type=int, default=8)
    parser.add_argument("--audit-finalists", type=int, default=4)
    # Four independently derived seeds per map/format keep a lucky or hostile
    # single seed from steering either the search or its confirmation stages.
    parser.add_argument("--search-rounds", type=int, default=4)
    parser.add_argument("--replication-rounds", type=int, default=4)
    parser.add_argument("--audit-rounds", type=int, default=4)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--seed", type=int, default=0x504F5254)
    parser.add_argument("--sampler-seed", type=int, default=0x4D554C54)
    parser.add_argument("--min-trial-distance", type=float, default=0.055)
    parser.add_argument(
        "--bohb", action="store_true",
        help="Cycle wide BOHB brackets at 49/147/440-game budgets",
    )
    parser.add_argument(
        "--bohb-eta", type=int, default=3,
        help="Successive-halving reduction factor (must be 3)",
    )
    parser.add_argument(
        "--bohb-random-fraction", type=float, default=0.25,
        help="Fraction of new BOHB configurations sampled uniformly for exploration",
    )
    parser.add_argument(
        "--parameter-stage", choices=tuple(tuning.PARAMETER_SETS),
        default="director", help="Coherent Maxima parameter block to optimize",
    )
    parser.add_argument(
        "--same-maps", action="store_true",
        help="Use the search map portfolio for every stage; vary only seeds",
    )
    parser.add_argument(
        "--warm-start-from", type=Path,
        help="Completed BOHB result directory or optimization.json used as prior data",
    )
    parser.add_argument(
        "--resume-from", type=Path,
        help=(
            "Resume an interrupted BOHB campaign from its completed brackets; "
            "unlike warm start, --trials remains the total configuration target"
        ),
    )
    parser.add_argument("--worker", action="append", type=parse_worker, dest="workers")
    parser.add_argument(
        "--require-all-workers", action="store_true",
        help="Fail a batch instead of silently reducing configured worker capacity",
    )
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if not args.workers:
        args.workers = list(STANDARD_WORKERS)
    positive = (
        args.trials, args.startup_trials, args.replication_finalists,
        args.audit_finalists, args.search_rounds, args.replication_rounds,
        args.audit_rounds, args.max_steps, args.timeout, args.bohb_eta,
    )
    if any(value < 1 for value in positive):
        parser.error("trial, round, step, timeout, and finalist counts must be positive")
    if args.replication_finalists > args.trials:
        parser.error("replication finalists cannot exceed trials")
    if args.audit_finalists > args.replication_finalists:
        parser.error("audit finalists cannot exceed replication finalists")
    if args.retries < 0:
        parser.error("retries cannot be negative")
    if args.bohb and args.search_rounds != 4:
        parser.error("--bohb currently requires --search-rounds 4")
    if args.bohb and args.bohb_eta != 3:
        parser.error("--bohb requires --bohb-eta 3")
    if not 0.0 <= args.bohb_random_fraction <= 1.0:
        parser.error("--bohb-random-fraction must be between 0 and 1")
    if args.warm_start_from and not args.bohb:
        parser.error("--warm-start-from requires --bohb")
    if args.resume_from and not args.bohb:
        parser.error("--resume-from requires --bohb")
    if args.resume_from and args.warm_start_from:
        parser.error("--resume-from and --warm-start-from are mutually exclusive")
    return args


def discover_maps(binary: Path, repo_root: Path) -> list[dict[str, Any]]:
    process = subprocess.run(
        [str(binary), "-list-nicowar-scenario-maps"], cwd=repo_root,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True,
    )
    maps = []
    for line in process.stdout.splitlines():
        if not line.startswith(SCENARIO_MAP_PREFIX):
            continue
        fields = line.split("\t")
        if len(fields) == 4:
            maps.append({"file": fields[1], "name": fields[2], "teams": int(fields[3])})
    if not maps:
        raise RuntimeError("scenario map discovery returned no maps")
    return maps


def resolve_map_sets(available: list[dict[str, Any]]) -> dict[str, dict[str, list[dict[str, Any]]]]:
    by_name = {item["name"].casefold(): item for item in available}
    result: dict[str, dict[str, list[dict[str, Any]]]] = {"train": {}, "audit": {}}
    for split, definitions in (("train", TRAIN_MAPS), ("audit", AUDIT_MAPS)):
        for format_name, names in definitions.items():
            selected = []
            for name in names:
                item = by_name.get(name.casefold())
                if item is None:
                    raise RuntimeError(f"required {split} map {name!r} is unavailable")
                needed = 4 if format_name == "2v2" else PLAYER_COUNTS[format_name]
                if format_name == "2v2" and item["teams"] != 4:
                    raise RuntimeError(f"2v2 map {name!r} must have exactly four teams")
                if item["teams"] < needed:
                    raise RuntimeError(f"map {name!r} cannot host {format_name}")
                selected.append(item)
            result[split][format_name] = selected
    train_files = {item["file"] for maps in result["train"].values() for item in maps}
    audit_files = {item["file"] for maps in result["audit"].values() for item in maps}
    overlap = train_files & audit_files
    if overlap:
        raise RuntimeError(f"train/audit map overlap: {sorted(overlap)}")
    return result


def scenario_offsets(map_teams: int, players: int) -> range:
    return range(1 if map_teams == players else 2)


def make_portfolio_schedule(
    maps_by_format: dict[str, list[dict[str, Any]]], rounds: int,
    base_seed: int, max_steps: int,
) -> list[dict[str, Any]]:
    schedule: list[dict[str, Any]] = []
    block = 0
    for round_number in range(1, rounds + 1):
        for format_index, format_name in enumerate(FORMATS):
            maps = maps_by_format[format_name]
            for map_index, map_info in enumerate(maps):
                block += 1
                seed_index = (round_number - 1) * 1000 + format_index * 100 + map_index
                seed = ffa_runner.derive_seed(base_seed, seed_index)
                if format_name == "2v2":
                    for partition in range(3):
                        for swap in range(2):
                            schedule.append({
                                "id": len(schedule) + 1, "block": block,
                                "round": round_number, "format": format_name,
                                "map": map_info["name"], "map_file": map_info["file"],
                                "seed": seed, "kind": "2v2", "ai_a": 6, "ai_b": 5,
                                "partition": partition, "swap": swap,
                                "max_steps": max_steps,
                            })
                    continue
                players = PLAYER_COUNTS[format_name]
                for offset in scenario_offsets(map_info["teams"], players):
                    for candidate_seat in range(players):
                        schedule.append({
                            "id": len(schedule) + 1, "block": block,
                            "round": round_number, "format": format_name,
                            "map": map_info["name"], "map_file": map_info["file"],
                            "map_teams": map_info["teams"], "seed": seed,
                            "kind": "scenario", "players_requested": players,
                            "candidate_ai": 7, "opponent_ai": 5,
                            "candidate_seat": candidate_seat,
                            "position_offset": offset, "max_steps": max_steps,
                        })
    return schedule


def offset_schedule(
    schedule: list[dict[str, Any]], id_offset: int, block_offset: int,
) -> list[dict[str, Any]]:
    """Copy a schedule while keeping cumulative BOHB result keys unique."""
    result = []
    for match in schedule:
        shifted = dict(match)
        shifted["id"] += id_offset
        shifted["block"] += block_offset
        result.append(shifted)
    return result


def bohb_increment_schedules(
    maps_by_format: dict[str, list[dict[str, Any]]], base_seed: int,
    max_steps: int,
) -> list[list[dict[str, Any]]]:
    """Return incremental schedules reaching 49, 147, then 440 games.

    Each partial round covers every map and format before adding further seat
    rotations. The budgets are approximately geometric with eta 3 and the full
    budget remains four complete, independently seeded rounds.
    """
    full_schedule = make_portfolio_schedule(
        maps_by_format, 4, base_seed, max_steps
    )

    def balanced_subset(round_number: int, count: int) -> list[dict[str, Any]]:
        round_matches = [
            match for match in full_schedule if match["round"] == round_number
        ]
        groups: dict[tuple[str, int], list[dict[str, Any]]] = {}
        for match in round_matches:
            groups.setdefault((match["format"], match["block"]), []).append(match)
        ordered_groups = list(groups.values())
        selected: list[dict[str, Any]] = []
        depth = 0
        while len(selected) < count:
            progressed = False
            for group_index, group in enumerate(ordered_groups):
                index = (group_index + depth) % len(group)
                candidate = group[index]
                if candidate not in selected:
                    selected.append(candidate)
                    progressed = True
                    if len(selected) == count:
                        break
            if not progressed:
                raise RuntimeError("cannot construct balanced partial-round budget")
            depth += 1
        return sorted(selected, key=lambda match: match["id"])

    low = balanced_subset(1, 49)
    middle_ids = {match["id"] for match in full_schedule if match["round"] == 1}
    middle_ids.update(match["id"] for match in balanced_subset(2, 37))
    low_ids = {match["id"] for match in low}
    middle_increment = [
        match for match in full_schedule
        if match["id"] in middle_ids and match["id"] not in low_ids
    ]
    high_increment = [
        match for match in full_schedule if match["id"] not in middle_ids
    ]
    assert len(full_schedule) == 440
    assert [len(low), len(middle_increment), len(high_increment)] == [49, 98, 293]
    return [low, middle_increment, high_increment]


def parse_scenario_output(match: dict[str, Any], output: str, returncode: int) -> dict[str, Any]:
    result = dict(match)
    result.update({
        "status": "failed", "engine_status": "unknown", "steps": 0,
        "players": [], "telemetry": [], "observer": [], "score_telemetry": [],
    })
    for line in output.splitlines():
        if line.startswith(SCENARIO_MATCH_PREFIX):
            fields = line.split("\t")
            if len(fields) == 10:
                result.update({
                    "seed": int(fields[1]), "map": fields[2], "steps": int(fields[3]),
                    "players_requested": int(fields[4]), "candidate_ai": int(fields[5]),
                    "opponent_ai": int(fields[6]), "candidate_seat": int(fields[7]),
                    "position_offset": int(fields[8]), "engine_status": fields[9],
                })
        elif line.startswith(SCENARIO_PLAYER_PREFIX):
            fields = line.split("\t")
            if len(fields) == 11:
                result["players"].append({
                    "role": fields[1], "version": fields[2], "player": int(fields[3]),
                    "team": int(fields[4]), "won": bool(int(fields[5])),
                    "lost": bool(int(fields[6])), "alive": bool(int(fields[7])),
                    "units": int(fields[8]), "buildings": int(fields[9]),
                    "prestige": int(fields[10]),
                })
        elif line.startswith(scoring.SCORE_PREFIX):
            snapshot = scoring.parse_score_line(line)
            if snapshot is not None:
                result["score_telemetry"].append(snapshot)
        elif any(line.startswith(prefix) for prefix in ffa_runner.TELEMETRY_PREFIXES):
            fields = line.split("\t")
            if len(fields) >= 4:
                version = next(
                    value for prefix, value in ffa_runner.TELEMETRY_PREFIXES.items()
                    if line.startswith(prefix)
                )
                values = dict(field.split("=", 1) for field in fields[4:] if "=" in field)
                result["telemetry"].append({
                    "tick": int(fields[1]), "team": int(fields[2]),
                    "ai_version": version, "event": fields[3], **values,
                })
        elif line.startswith(ffa_runner.OBSERVER_PREFIX):
            fields = line.split("\t")
            if len(fields) >= 5:
                values = dict(field.split("=", 1) for field in fields[4:] if "=" in field)
                result["observer"].append({
                    "tick": int(fields[1]), "team": int(fields[2]),
                    "version": fields[3], **values,
                })
    expected = match["players_requested"]
    if returncode == 0 and result["engine_status"] in ("completed", "timeout") and len(result["players"]) == expected:
        result["status"] = "completed"
    else:
        result["error"] = f"worker exited {returncode}; structured result was incomplete"
    result["returncode"] = returncode
    result["output"] = output
    if result["status"] == "completed":
        scoring.enrich_ffa_match(result)
    return result


def command_for(match: dict[str, Any], binary: str) -> list[str]:
    if match["kind"] == "2v2":
        command = [binary, "-nicowar-2v2-match-nox", match["map_file"], str(match["seed"]),
                   str(match["ai_a"]), str(match["ai_b"]), str(match["partition"]),
                   str(match["swap"]), str(match["max_steps"])]
    else:
        command = [binary, "-nicowar-scenario-match-nox", match["map_file"], str(match["seed"]),
                   str(match["players_requested"]), str(match["candidate_ai"]),
                   str(match["opponent_ai"]), str(match["candidate_seat"]),
                   str(match["position_offset"]), str(match["max_steps"])]
    if match.get("telemetry"):
        command.append("-nicowar-telemetry")
    return command


class PortfolioCluster:
    def __init__(self, binary: Path, repo_root: Path, workers: list[tuple[str, int]],
                 remote_root: str, timeout: int, retries: int,
                 require_all_workers: bool = False,
                 result_sink: Callable[[dict[str, Any]], dict[str, Any]] | None = None) -> None:
        self.binary = binary
        self.repo_root = repo_root
        self.workers = workers
        self.remote_root = remote_root.rstrip("/")
        self.timeout = timeout
        self.retries = retries
        self.require_all_workers = require_all_workers
        self.result_sink = result_sink
        # Give every remote slot its own SSH control connection. Reusing one
        # master concurrently can briefly overlap closing and replacement
        # channels; the campaign hosts reject that overlap at their session cap.
        self.slots = [
            (host, index)
            for host, count in workers
            for index in range(count)
        ]
        self.connection_epoch = 0
        self.prepared_connections: set[tuple[str, int]] = set()

    def _control_path(self, host: str, group: int) -> str:
        safe_host = "".join(character if character.isalnum() else "-" for character in host)
        return f"/tmp/g2opt-{os.getpid()}-{self.connection_epoch}-{safe_host}-{group}"

    def _ssh_arguments(self, host: str, group: int) -> list[str]:
        path = self._control_path(host, group)
        return ["ssh", "-o", "BatchMode=yes", "-o", f"ControlPath={path}"]

    def _prepare_connection(self, host: str, group: int) -> bool:
        key = (host, group)
        if host == "local" or key in self.prepared_connections:
            return True
        command = self._ssh_arguments(host, group)
        command[1:1] = ["-o", "ConnectTimeout=8", "-o", "ControlMaster=yes",
                        "-o", "ControlPersist=600"]
        command.extend(["-MNf", host])
        try:
            process = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, timeout=15)
        except subprocess.TimeoutExpired:
            return False
        if process.returncode != 0:
            return False

        # With ControlMaster backgrounding, ssh can return success just before
        # sshd rejects the still-authenticating connection under MaxStartups.
        # Confirm that the master socket is genuinely usable before assigning a
        # tournament slot to it. Otherwise a nominal 32-slot host can silently
        # start with fewer live workers.
        check = self._ssh_arguments(host, group)
        check.extend(["-O", "check", host])
        try:
            verified = subprocess.run(
                check, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=5,
            )
        except subprocess.TimeoutExpired:
            return False
        if verified.returncode != 0:
            try:
                Path(self._control_path(host, group)).unlink()
            except FileNotFoundError:
                pass
            return False
        self.prepared_connections.add(key)
        return True

    def _reset_connection(self, host: str, group: int) -> None:
        """Replace a control master after ssh reports an unusable channel."""
        if host == "local":
            return
        key = (host, group)
        command = self._ssh_arguments(host, group)
        command.extend(["-O", "exit", host])
        try:
            subprocess.run(
                command, text=True, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, timeout=5,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass
        self.prepared_connections.discard(key)
        try:
            Path(self._control_path(host, group)).unlink()
        except FileNotFoundError:
            pass
        self._prepare_connection(host, group)

    def _execute(self, host: str, group: int, match: dict[str, Any], settings: str) -> dict[str, Any]:
        started = time.monotonic()
        if host == "local":
            command = command_for(match, str(self.binary))
            environment = os.environ.copy()
            environment["GLOB2_MAXIMA_OVERRIDES"] = settings
            process = subprocess.run(command, cwd=self.repo_root, text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=self.timeout, env=environment)
        else:
            binary = f"{self.remote_root}/build-tournament/src/glob2"
            command = command_for(match, binary)
            remote = (f"cd {shlex.quote(self.remote_root)} && "
                      f"GLOB2_MAXIMA_OVERRIDES={shlex.quote(settings)} "
                      + shlex.join(command))
            process = subprocess.run(self._ssh_arguments(host, group) + [host, remote],
                                     text=True, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, timeout=self.timeout)
        if match["kind"] == "2v2":
            result = team_runner.parse_worker_output(match, process.stdout, process.returncode)
        else:
            result = parse_scenario_output(match, process.stdout, process.returncode)
        # Telemetry campaigns already retain structured event, observer, and score
        # rows. Keeping the complete raw stdout as well duplicates the dominant
        # memory cost and can kill the controller in thousand-game portfolios.
        if result.get("status") == "completed":
            result["output"] = ""
        result["worker_host"] = host
        result["wall_seconds"] = round(time.monotonic() - started, 3)
        return result

    def _run_one(self, host: str, group: int, match: dict[str, Any], settings: str) -> dict[str, Any]:
        last: dict[str, Any] | None = None
        attempt = 0
        ordinary_retries = 0
        infrastructure_retries = 0
        while True:
            try:
                last = self._execute(host, group, match, settings)
            except subprocess.TimeoutExpired as error:
                output = error.stdout or ""
                if isinstance(output, bytes):
                    output = output.decode(errors="replace")
                last = dict(match)
                last.update({"status": "failed", "engine_status": "process_timeout",
                             "steps": 0, "players": [], "worker_host": host,
                             "error": f"worker exceeded {self.timeout} seconds", "output": output})
            except OSError as error:
                last = dict(match)
                last.update({"status": "failed", "engine_status": "process_start_failed",
                             "steps": 0, "players": [], "worker_host": host,
                             "error": f"worker process could not start: {error}", "output": ""})
            last["attempt"] = attempt + 1
            if last["status"] == "completed":
                return last
            if (
                host != "local" and last.get("returncode") == 255
                and infrastructure_retries < 4
            ):
                infrastructure_retries += 1
                attempt += 1
                self._reset_connection(host, group)
                time.sleep(0.25 * infrastructure_retries)
                continue
            if ordinary_retries < self.retries:
                ordinary_retries += 1
                attempt += 1
                time.sleep(0.25 * ordinary_retries)
                continue
            break
        assert last is not None
        return last

    def run_many(
        self,
        workloads: list[tuple[int, list[dict[str, Any]], str]],
    ) -> dict[int, list[dict[str, Any]]]:
        """Run multiple strategy configurations through one shared match queue."""
        # A shared queue lets heterogeneous hosts continuously pull work. Static
        # round-robin assignment makes an entire block wait for its slowest slot.
        pending: Queue[tuple[int, dict[str, Any], str, int]] = Queue()
        results: dict[int, list[dict[str, Any]]] = {
            key: [] for key, _, _ in workloads
        }
        for key, schedule, settings in workloads:
            for match in schedule:
                pending.put((key, match, settings, 0))
        results_lock = threading.Lock()
        unhealthy_connections: set[tuple[str, int]] = set()
        connection_errors: dict[tuple[str, int], str] = {}
        stop = threading.Event()

        # Prepare independent per-slot control connections in small parallel
        # waves. Connections persist across batches, avoiding both per-rung
        # setup cost and multiplexed-session overlap failures.
        usable_connections: set[tuple[str, int]] = set()
        preparation_lock = threading.Lock()

        def prepare_group(host: str, group: int) -> None:
            for attempt in range(3):
                if self._prepare_connection(host, group):
                    with preparation_lock:
                        usable_connections.add((host, group))
                    return
                time.sleep(0.5 * (attempt + 1))

        def prepare_host(host: str) -> None:
            groups = sorted({group for slot_host, group in self.slots if slot_host == host})
            if host == "local":
                with preparation_lock:
                    usable_connections.update((host, group) for group in groups)
                return
            # Keep unauthenticated connection bursts below conservative sshd
            # MaxStartups limits, especially on high-capacity 32-slot hosts.
            with ThreadPoolExecutor(max_workers=min(2, len(groups))) as executor:
                futures = [
                    executor.submit(prepare_group, host, group)
                    for group in groups
                ]
                for future in futures:
                    future.result()

        with ThreadPoolExecutor(max_workers=len(self.workers)) as executor:
            futures = [
                executor.submit(prepare_host, host) for host, _ in self.workers
            ]
            for future in futures:
                future.result()
        active_slots = [
            slot for slot in self.slots
            if slot[0] == "local" or slot in usable_connections
        ]
        if not active_slots:
            raise RuntimeError("no portfolio workers are currently reachable")
        if self.require_all_workers and len(active_slots) != len(self.slots):
            missing = sorted(set(self.slots) - set(active_slots))
            raise RuntimeError(
                f"required worker connections unavailable: {missing}"
            )

        def consume(slot: tuple[str, int]) -> None:
            host, group = slot
            while not stop.is_set():
                with results_lock:
                    unavailable = slot in unhealthy_connections
                if unavailable:
                    stop.wait(0.25)
                    continue
                try:
                    key, match, settings, dispatch_attempt = pending.get(timeout=0.25)
                except Empty:
                    continue
                try:
                    result = self._run_one(host, group, match, settings)
                    if result["status"] == "completed":
                        if self.result_sink is not None:
                            result = self.result_sink(result)
                        with results_lock:
                            results[key].append(result)
                    else:
                        infrastructure_failure = result.get("returncode") == 255
                        if infrastructure_failure:
                            with results_lock:
                                # One exhausted or closed SSH control connection
                                # must not quarantine every connection on a
                                # high-capacity host.
                                unhealthy_connections.add(slot)
                                connection_errors[slot] = str(
                                    result.get("output")
                                    or result.get("error") or "ssh exited 255"
                                )[-1000:]
                        if dispatch_attempt < max(2, self.retries + 1):
                            pending.put((
                                key, match, settings, dispatch_attempt + 1,
                            ))
                        else:
                            if self.result_sink is not None:
                                result = self.result_sink(result)
                            with results_lock:
                                results[key].append(result)
                finally:
                    pending.task_done()

        with ThreadPoolExecutor(max_workers=len(active_slots)) as executor:
            futures = [executor.submit(consume, slot) for slot in active_slots]
            pending.join()
            stop.set()
            for future in futures:
                future.result()
        if self.require_all_workers and unhealthy_connections:
            details = {
                str(key): connection_errors.get(key, "")
                for key in sorted(unhealthy_connections)
            }
            raise RuntimeError(
                "required worker connections failed during batch: "
                f"{json.dumps(details, sort_keys=True)}"
            )
        return {
            key: sorted(items, key=lambda result: result["id"])
            for key, items in results.items()
        }

    def run(
        self, schedule: list[dict[str, Any]], settings: str,
    ) -> list[dict[str, Any]]:
        return self.run_many([(0, schedule, settings)])[0]


def scenario_match_score(match: dict[str, Any]) -> tuple[float, float, float]:
    if "_portfolio_score" in match:
        return tuple(match["_portfolio_score"])
    scoring.enrich_ffa_match(match)
    candidate = next(player for player in match["players"] if player["role"] == "candidate")
    opponents = [player for player in match["players"] if player["role"] == "opponent"]
    formal = []
    for opponent in opponents:
        if candidate["won"] != opponent["won"]:
            outcome = 1.0 if candidate["won"] else 0.0
        elif candidate["lost"] != opponent["lost"]:
            outcome = 0.0 if candidate["lost"] else 1.0
        else:
            outcome = 0.5
        formal.append(outcome)
    return mean(formal), float(candidate["normalized_score"]), 1.0 if candidate["won"] else 0.0


def team_match_score(match: dict[str, Any]) -> tuple[float, float, float]:
    if "_portfolio_score" in match:
        return tuple(match["_portfolio_score"])
    scoring.enrich_2v2_match(match)
    if match["winner_side"] == "A":
        outcome = 1.0
    elif match["winner_side"] == "B":
        outcome = 0.0
    else:
        outcome = 0.5
    return outcome, float(match["score"]["normalized_score"]), outcome


def score_results(results: list[dict[str, Any]]) -> dict[str, Any]:
    failed = [result for result in results if result["status"] != "completed"]
    if failed:
        return {"objective": -1.0, "portfolio_score": 0.0,
                "worst_format_score": 0.0, "format_stddev": 0.0,
                "failed": len(failed), "games": len(results), "formats": {},
                "score_version": scoring.SIGNAL_VERSION}
    blocks: dict[tuple[str, int], list[tuple[float, float, float]]] = {}
    map_scores: dict[tuple[str, str], list[float]] = {}
    for match in results:
        value = team_match_score(match) if match["kind"] == "2v2" else scenario_match_score(match)
        blocks.setdefault((match["format"], match["block"]), []).append(value)
        map_scores.setdefault((match["format"], match["map"]), []).append(value[0])
    format_metrics: dict[str, dict[str, float | int]] = {}
    for format_name in FORMATS:
        block_values = [values for (fmt, _), values in blocks.items() if fmt == format_name]
        formal_blocks = [mean(value[0] for value in values) for values in block_values]
        shaped_blocks = [mean(value[1] for value in values) for values in block_values]
        wins = [mean(value[2] for value in values) for values in block_values]
        se = pstdev(formal_blocks) / math.sqrt(len(formal_blocks)) if len(formal_blocks) > 1 else 0.0
        score = mean(formal_blocks)
        format_metrics[format_name] = {
            "blocks": len(block_values), "score": round(score, 8),
            "shaped": round(mean(shaped_blocks), 8), "win_share": round(mean(wins), 8),
            "ci95_low": round(max(0.0, score - 1.96 * se), 8),
            "ci95_high": round(min(1.0, score + 1.96 * se), 8),
        }
    formal_scores = [float(format_metrics[name]["score"]) for name in FORMATS]
    shaped_scores = [float(format_metrics[name]["shaped"]) for name in FORMATS]
    map_metrics = {
        f"{format_name}:{map_name}": {
            "format": format_name,
            "map": map_name,
            "score": round(mean(values), 8),
            "games": len(values),
        }
        for (format_name, map_name), values in sorted(map_scores.items())
    }
    # Equal format weight plus a worst-format term discourages brittle specialists.
    objective = 0.70 * mean(shaped_scores) + 0.30 * min(shaped_scores)
    return {
        "objective": round(objective, 8),
        "portfolio_score": round(mean(formal_scores), 8),
        "worst_format_score": round(min(formal_scores), 8),
        "format_stddev": round(pstdev(formal_scores), 8),
        "failed": 0, "games": len(results), "formats": format_metrics,
        "maps": map_metrics,
        "score_version": scoring.SIGNAL_VERSION,
    }


def compact_results(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    compact = []
    for result in results:
        row = dict(result)
        output = row.pop("output", "")
        row.pop("score_telemetry", None)
        row.pop("observer", None)
        row.pop("telemetry", None)
        if row.get("status") != "completed":
            row["output_tail"] = output[-4000:]
        compact.append(row)
    return compact


def evaluate(cluster: PortfolioCluster, parameters: dict[str, int], schedule: list[dict[str, Any]]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    tuning.resolve_candidate(cluster.binary, parameters)
    results = cluster.run(schedule, tuning.tuning_string(parameters))
    return score_results(results), results


def print_score(prefix: str, score: dict[str, Any]) -> None:
    formats = " ".join(f"{name}={score['formats'].get(name, {}).get('score', 0):.1%}" for name in FORMATS)
    print(f"{prefix}: portfolio={score['portfolio_score']:.1%} worst={score.get('worst_format_score', 0):.1%} objective={score['objective']:.4f} {formats}", flush=True)


def select_diverse(records: list[dict[str, Any]], count: int, distance: float) -> list[dict[str, Any]]:
    selected = tuning.select_diverse_finalists(records, count, distance)
    baseline = next(record for record in records if record["trial"] == 0)
    if all(record["trial"] != 0 for record in selected):
        selected[-1] = baseline
    return selected


def select_bohb_promotions(
    records: list[dict[str, Any]], count: int,
) -> list[dict[str, Any]]:
    """Promote the best records, retaining the baseline when it is in this bracket."""
    count = max(1, min(count, len(records)))
    ranked = sorted(records, key=lambda item: item["objective"], reverse=True)
    selected = ranked[:count]
    baseline = next((record for record in records if record["trial"] == 0), None)
    if baseline is not None and all(record["trial"] != 0 for record in selected):
        selected[-1] = baseline
    return selected


def bohb_model_records(
    observations: dict[int, list[dict[str, Any]]], minimum: int,
) -> tuple[int | None, list[dict[str, Any]]]:
    """Use the largest budget with enough observations, as BOHB prescribes."""
    for budget in (440, 147, 49):
        records = observations[budget]
        if len(records) >= minimum:
            return budget, records
    return None, []


def propose_bohb_parameters(
    rng: Any, model_records: list[dict[str, Any]],
    all_configurations: list[dict[str, Any]], required_distance: float,
    startup_trials: int,
) -> dict[str, int]:
    """Draw a novel joint proposal from Optuna's multivariate TPE model."""
    return tuning.novel_tpe_parameters(
        rng, model_records, required_distance,
        novelty_records=all_configurations,
        startup_trials=startup_trials,
    )


def write_report(output_dir: Path, payload: dict[str, Any]) -> None:
    leader_records = payload.get("full_budget_candidates") or payload["trials"]
    lines = [
        "# Maxima multi-format portfolio optimization", "",
        f"- New configurations this phase: {payload['metadata'].get('new_configurations', len(payload['trials']))}",
        f"- Total configurations available: {len(payload['trials'])}",
        f"- Formats: {', '.join(FORMATS)} (equal weight)",
        f"- Simulation horizon: {payload['metadata']['max_steps']} ticks in every stage",
        f"- Workers: {', '.join(f'{h}:{n}' for h, n in payload['metadata']['workers'])}",
        f"- Wall time: {payload['metadata']['wall_seconds']:.1f} seconds", "",
        "Objective = 70% equal-format continuous victory-score mean + 30% worst-format continuous score. Decisive outcomes anchor the sign; speed and time-integrated economy, military, resilience, and prestige determine magnitude. Formal pairwise outcomes are reported separately.", "",
        "## Full-budget search leaders", "",
        "| Trial | Portfolio | Worst format | Objective | Format scores |",
        "|---:|---:|---:|---:|---|",
    ]
    for record in sorted(leader_records, key=lambda item: item["objective"], reverse=True)[:10]:
        details = ", ".join(f"{name} {record['formats'][name]['score']:.1%}" for name in FORMATS) if record["formats"] else "failed"
        lines.append(f"| {record['trial']} | {record['portfolio_score']:.1%} | {record.get('worst_format_score', 0):.1%} | {record['objective']:.4f} | {details} |")
    if payload.get("bohb_rungs"):
        for rung_index, records in enumerate(payload["bohb_rungs"]):
            games = payload["metadata"]["bohb_budgets"][rung_index]
            lines.extend(["", f"## BOHB budget: {games} games", "",
                          "| Trial | Portfolio | Worst format | Objective | Format scores |",
                          "|---:|---:|---:|---:|---|"])
            for record in sorted(records, key=lambda item: item["objective"], reverse=True)[:10]:
                details = ", ".join(f"{name} {record['formats'][name]['score']:.1%}" for name in FORMATS) if record["formats"] else "failed"
                lines.append(f"| {record['trial']} | {record['portfolio_score']:.1%} | {record.get('worst_format_score', 0):.1%} | {record['objective']:.4f} | {details} |")
    confirmation_title = ("Second fresh-seed replication"
                          if payload["metadata"]["same_maps"]
                          else "Untouched-map audit")
    for title, key in (("Fresh-seed replication", "replications"), (confirmation_title, "audits")):
        lines.extend(["", f"## {title}", "", "| Trial | Portfolio | Worst format | Objective | Format scores |", "|---:|---:|---:|---:|---|"])
        for record in sorted(payload[key], key=lambda item: item["objective"], reverse=True):
            details = ", ".join(f"{name} {record['formats'][name]['score']:.1%}" for name in FORMATS) if record["formats"] else "failed"
            lines.append(f"| {record['trial']} | {record['portfolio_score']:.1%} | {record.get('worst_format_score', 0):.1%} | {record['objective']:.4f} | {details} |")
    if payload.get("audits"):
        lines.extend(["", "## Held-out map breakdown", ""])
        for record in sorted(payload["audits"], key=lambda item: item["trial"]):
            enabled = record["parameters"].get("preemptive_defense_enabled")
            treatment = f" (preemptive defense {'on' if enabled else 'off'})" if enabled is not None else ""
            lines.extend([
                f"### Trial {record['trial']}{treatment}", "",
                "| Player format | Map | Score | Games |",
                "|---|---|---:|---:|",
            ])
            for metric in record.get("maps", {}).values():
                lines.append(
                    f"| {metric['format']} | {metric['map']} | "
                    f"{metric['score']:.1%} | {metric['games']} |"
                )
            lines.append("")
    lines.extend([
        "", "## Selected strategy overrides", "",
        f"Selection basis: {payload['metadata'].get('selection_basis', 'replication objective')}.",
        "", "```text", tuning.tuning_string(payload["selected"]["parameters"]), "```",
    ])
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n")


def load_warm_start(path: Path) -> tuple[dict[str, Any], Path]:
    """Load either a completed result or a recoverable JSONL checkpoint."""
    source = path
    if source.is_dir() and (source / "optimization.json").exists():
        source = source / "optimization.json"
    if source.is_file():
        return json.loads(source.read_text()), source
    if not source.is_dir():
        raise RuntimeError(f"warm-start result does not exist: {source}")
    trials_path = source / "trials.jsonl"
    evaluations_path = source / "bohb-evaluations.jsonl"
    if not trials_path.exists() or not evaluations_path.exists():
        raise RuntimeError(f"warm-start checkpoint is incomplete: {source}")
    trials = [
        json.loads(line) for line in trials_path.read_text().splitlines()
        if line.strip()
    ]
    evaluations = [
        json.loads(line) for line in evaluations_path.read_text().splitlines()
        if line.strip()
    ]
    brackets_path = source / "bohb-brackets.json"
    brackets = (
        json.loads(brackets_path.read_text()) if brackets_path.exists() else []
    )
    # A checkpoint may have complete evaluations from the bracket that was
    # interrupted. Only a bracket recorded in bohb-brackets.json crossed its
    # promotion boundary and is safe to reuse.
    completed_brackets = {record["bracket"] for record in brackets}
    if brackets:
        trials = [
            record for record in trials
            if record.get("bracket") in completed_brackets
        ]
        evaluations = [
            record for record in evaluations
            if record.get("bracket") in completed_brackets
        ]
    manifest_path = source / "run-manifest.json"
    manifest = (
        json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    )
    manifest_schema = manifest.get("strategy_schema", {})
    manifest_seeds = manifest.get("seeds", {})
    manifest_bohb = manifest.get("bohb", {})
    manifest_command = manifest.get("command", [])

    def manifest_option(name: str, default: Any = None) -> Any:
        try:
            return manifest_command[manifest_command.index(name) + 1]
        except (ValueError, IndexError):
            return default

    budgets = [49, 147, 440]
    rungs = [
        [record for record in evaluations if record.get("budget_games") == budget]
        for budget in budgets
    ]
    payload = {
        "metadata": {
            "bohb": True, "bohb_budgets": budgets,
            "checkpoint_recovered": True,
            "strategy_schema_version": manifest_schema.get("schemaVersion", 2),
            "proposal_engine": manifest_bohb.get("proposal_engine"),
            "optuna_version": manifest_bohb.get("optuna_version"),
            "seed": manifest_seeds.get("search"),
            "sampler_seed": manifest_seeds.get("sampler"),
            "max_steps": int(manifest_option("--max-steps", 0)) or None,
            "search_rounds": int(manifest_option("--search-rounds", 0)) or None,
            "replication_rounds": int(
                manifest_option("--replication-rounds", 0)
            ) or None,
            "audit_rounds": int(manifest_option("--audit-rounds", 0)) or None,
            "same_maps": "--same-maps" in manifest_command,
        },
        "trials": trials,
        "bohb_rungs": rungs,
        "full_budget_candidates": rungs[-1],
        "bohb_brackets": brackets,
        "replications": [], "audits": [],
    }
    return payload, source


def restore_bohb_rng(
    rng: Any, payload: dict[str, Any], startup_trials: int,
    random_fraction: float, required_distance: float,
) -> None:
    """Replay completed brackets to restore the exact sampler RNG state."""
    budgets = (49, 147, 440)
    observations: dict[int, list[dict[str, Any]]] = {
        budget: [] for budget in budgets
    }
    trials = {record["trial"]: record for record in payload.get("trials", [])}
    evaluations = {
        (record["bracket"], record["trial"], record["budget_games"]): record
        for rung in payload.get("bohb_rungs", []) for record in rung
    }
    configurations: list[dict[str, Any]] = []
    for bracket in payload.get("bohb_brackets", []):
        start_budget = bracket["start_budget"]
        # The production runner asks for every new configuration in a bracket
        # before evaluating the batch. Replay proposals against the same frozen
        # observation set, then expose the completed start-rung records.
        pending_start_records = []
        for trial in bracket["new_trials"]:
            stored = trials[trial]
            if trial == 0:
                proposed = tuning.default_parameters()
            else:
                model_budget, model_records = bohb_model_records(
                    observations, startup_trials
                )
                if rng.random() < random_fraction:
                    proposed = tuning.random_parameters(rng)
                elif model_records:
                    proposed = propose_bohb_parameters(
                        rng, model_records, configurations,
                        required_distance, startup_trials,
                    )
                else:
                    proposed = tuning.random_parameters(rng)
            if proposed != stored["parameters"]:
                raise RuntimeError(
                    f"cannot restore BOHB sampler state at trial {trial}"
                )
            configurations.append(stored)
            pending_start_records.append(
                evaluations[(bracket["bracket"], trial, start_budget)]
            )
        observations[start_budget].extend(pending_start_records)
        for promotion in bracket.get("promotions", []):
            budget = promotion["to_budget"]
            for trial in promotion["trial_ids"]:
                observations[budget].append(
                    evaluations[(bracket["bracket"], trial, budget)]
                )


def validate_warm_start(payload: dict[str, Any]) -> None:
    metadata = payload.get("metadata", {})
    if metadata.get("strategy_schema_version") != 2:
        raise RuntimeError(
            "warm-start strategy schema version must be 2; historical vectors "
            "are not translated"
        )
    if not metadata.get("bohb"):
        raise RuntimeError("warm-start result is not a BOHB campaign")
    recorded_version = metadata.get("score_signal", {}).get("version")
    if recorded_version is None:
        records = payload.get("trials", []) + [
            row for rung in payload.get("bohb_rungs", []) for row in rung
        ]
        versions = {row.get("score_version") for row in records}
        recorded_version = scoring.SIGNAL_VERSION if versions == {scoring.SIGNAL_VERSION} else None
    if recorded_version != scoring.SIGNAL_VERSION:
        raise RuntimeError(
            f"warm-start score signal version must be {scoring.SIGNAL_VERSION}; "
            "objectives from the previous win/loss signal cannot be mixed"
        )
    if metadata.get("bohb_budgets") != [49, 147, 440]:
        raise RuntimeError("warm-start BOHB budgets must be 49/147/440 games")
    if metadata.get("proposal_engine") != tuning.PROPOSAL_ENGINE:
        raise RuntimeError(
            "warm-start proposal engine is incompatible; custom-TPE and "
            "Optuna observations cannot be mixed in one promotion campaign"
        )
    if metadata.get("optuna_version") != tuning.optuna_version():
        raise RuntimeError(
            "warm-start Optuna version does not match the running optimizer"
        )


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else root / args.binary
    tuning.configure_parameters(binary, args.parameter_stage, "2v2")
    dependency_optuna_version = tuning.optuna_version()
    output_dir = args.output_dir or root / "tournament-results" / datetime.now(timezone.utc).strftime("maxima-portfolio-%Y%m%dT%H%M%SZ")
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    prior_payload: dict[str, Any] | None = None
    prior_source: Path | None = None
    resume_mode = args.resume_from is not None
    prior_argument = args.resume_from or args.warm_start_from
    if prior_argument:
        prior_source = prior_argument
        if not prior_source.is_absolute():
            prior_source = root / prior_source
        prior_payload, prior_source = load_warm_start(prior_source)
        validate_warm_start(prior_payload)
        if resume_mode and prior_source.resolve() == output_dir.resolve():
            raise RuntimeError(
                "--resume-from requires a different output directory so the "
                "source checkpoint remains recoverable"
            )
        if resume_mode:
            metadata = prior_payload["metadata"]
            expected = {
                "seed": args.seed,
                "sampler_seed": args.sampler_seed,
                "max_steps": args.max_steps,
                "search_rounds": args.search_rounds,
                "replication_rounds": args.replication_rounds,
                "audit_rounds": args.audit_rounds,
                "same_maps": args.same_maps,
            }
            mismatches = {
                key: (metadata.get(key), value)
                for key, value in expected.items()
                if metadata.get(key) is not None and metadata.get(key) != value
            }
            if mismatches:
                raise RuntimeError(
                    f"resume arguments do not match checkpoint: {mismatches}"
                )
            if len(prior_payload.get("trials", [])) > args.trials:
                raise RuntimeError(
                    "resume checkpoint already exceeds the requested trial target"
                )
    maps = resolve_map_sets(discover_maps(binary, root))
    if args.bohb:
        bohb_schedules = bohb_increment_schedules(
            maps["train"], args.seed, args.max_steps
        )
        search_schedule = bohb_schedules[0]
    else:
        bohb_schedules = []
        search_schedule = make_portfolio_schedule(
            maps["train"], args.search_rounds, args.seed, args.max_steps
        )
    replication_schedule = make_portfolio_schedule(maps["train"], args.replication_rounds, args.seed ^ 0x52504C43, args.max_steps)
    audit_map_set = maps["train"] if args.same_maps else maps["audit"]
    audit_schedule = make_portfolio_schedule(audit_map_set, args.audit_rounds, args.seed ^ 0x41554454, args.max_steps)
    cluster = PortfolioCluster(
        binary, root, args.workers, args.remote_root, args.timeout,
        args.retries, args.require_all_workers,
    )
    rng = __import__("random").Random(args.sampler_seed)
    trials: list[dict[str, Any]] = []
    trial_results: dict[int, list[dict[str, Any]]] = {}
    started = time.monotonic()
    bohb_brackets: list[dict[str, Any]] = (
        list(prior_payload.get("bohb_brackets", []))
        if resume_mode and prior_payload else []
    )
    bohb_rungs: list[list[dict[str, Any]]] = [[], [], []]
    search_records: list[dict[str, Any]] = []
    search_candidates: list[dict[str, Any]] = []
    all_configurations: list[dict[str, Any]] = []
    search_games_total = (
        sum(
            record.get("incremental_games", 0)
            for rung in prior_payload.get("bohb_rungs", []) for record in rung
        ) if resume_mode and prior_payload else 0
    )

    if not args.bohb:
        for index in range(args.trials):
            if index == 0:
                parameters, sampler = tuning.default_parameters(), "baseline"
            elif index < args.startup_trials:
                parameters = tuning.novel_random_parameters(
                    rng, trials, args.min_trial_distance
                )
                sampler = "random"
            else:
                parameters = tuning.novel_tpe_parameters(
                    rng, trials, args.min_trial_distance,
                    startup_trials=args.startup_trials,
                )
                sampler = "optuna-tpe"
            trial_started = time.monotonic()
            score, results = evaluate(cluster, parameters, search_schedule)
            record = {
                "trial": index, "sampler": sampler, "parameters": parameters,
                "budget_rounds": args.search_rounds, **score,
                "wall_seconds": round(time.monotonic() - trial_started, 3),
            }
            trials.append(record)
            search_records.append(record)
            with (output_dir / "trials.jsonl").open("a") as handle:
                handle.write(json.dumps(record, sort_keys=True) + "\n")
            (output_dir / f"trial-{index:03d}-matches.json").write_text(
                json.dumps(compact_results(results), indent=2) + "\n"
            )
            print_score(f"trial {index:02d} {sampler}", score)
        search_candidates = trials
        search_games_total = len(trials) * len(search_schedule)
    else:
        # Wide Hyperband brackets for eta=3 and 49/147/440-game budgets.
        # Most new configurations enter through the cheap, topology-complete
        # screen while higher-starting brackets protect against poor fidelity.
        bracket_templates = (
            {"name": "low", "new_count": 9, "start_budget": 49},
            {"name": "middle", "new_count": 5, "start_budget": 147},
            {"name": "high", "new_count": 3, "start_budget": 440},
        )
        budgets = (49, 147, 440)
        budget_index = {budget: index for index, budget in enumerate(budgets)}
        observations: dict[int, list[dict[str, Any]]] = {
            budget: [] for budget in budgets
        }
        if prior_payload:
            all_configurations = list(prior_payload.get("trials", []))
            for budget, records in zip(
                budgets, prior_payload.get("bohb_rungs", [])
            ):
                observations[budget].extend(
                    record for record in records
                    if record.get("failed", 0) == 0
                )
            search_candidates.extend(
                record for record in prior_payload.get(
                    "full_budget_candidates", []
                ) if record.get("failed", 0) == 0
            )
            if resume_mode:
                restore_bohb_rng(
                    rng, prior_payload, args.startup_trials,
                    args.bohb_random_fraction, args.min_trial_distance,
                )
                # Seed the new output with the reusable checkpoint so another
                # interruption can itself be resumed without consulting both
                # directories.
                (output_dir / "trials.jsonl").write_text("".join(
                    json.dumps(record, sort_keys=True) + "\n"
                    for record in prior_payload.get("trials", [])
                ))
                prior_evaluations = [
                    record for rung in prior_payload.get("bohb_rungs", [])
                    for record in rung
                ]
                prior_evaluations.sort(
                    key=lambda record: (
                        record["bracket"], record["budget_games"],
                        record["trial"],
                    )
                )
                (output_dir / "bohb-evaluations.jsonl").write_text("".join(
                    json.dumps(record, sort_keys=True) + "\n"
                    for record in prior_evaluations
                ))
                (output_dir / "bohb-brackets.json").write_text(
                    json.dumps(bohb_brackets, indent=2) + "\n"
                )
        next_trial = 1 + max(
            (record["trial"] for record in all_configurations), default=-1
        )
        generated = len(all_configurations) if resume_mode else 0
        configuration_history = list(all_configurations)

        def evaluate_bohb_batch(
            requests: list[dict[str, Any]],
        ) -> list[dict[str, Any]]:
            nonlocal search_games_total
            schedules = {
                request["trial"]: [
                    match for increment in request["increments"]
                    for match in bohb_schedules[increment]
                ]
                for request in requests
            }
            for request in requests:
                tuning.resolve_candidate(cluster.binary, request["parameters"])
            batch_started = time.monotonic()
            results_by_trial = cluster.run_many([
                (
                    request["trial"], schedules[request["trial"]],
                    tuning.tuning_string(request["parameters"]),
                )
                for request in requests
            ])
            batch_wall_seconds = round(time.monotonic() - batch_started, 3)
            records = []
            for request in requests:
                trial = request["trial"]
                budget = request["budget"]
                incremental_results = results_by_trial[trial]
                combined_results = (
                    trial_results.get(trial, []) + incremental_results
                )
                score = score_results(combined_results)
                trial_results[trial] = compact_results(combined_results)
                record = {
                    "trial": trial, "sampler": request["sampler"],
                    "parameters": request["parameters"],
                    "budget_games": budget,
                    "bracket": request["bracket_id"],
                    "incremental_games": len(incremental_results), **score,
                    "wall_seconds": batch_wall_seconds,
                }
                if request.get("previous_objective") is not None:
                    record["previous_objective"] = request[
                        "previous_objective"
                    ]
                observations[budget].append(record)
                bohb_rungs[budget_index[budget]].append(record)
                search_records.append(record)
                search_games_total += len(incremental_results)
                with (output_dir / "bohb-evaluations.jsonl").open("a") as handle:
                    handle.write(json.dumps(record, sort_keys=True) + "\n")
                (output_dir / (
                    f"trial-{trial:03d}-budget-{budget}-increment-matches.json"
                )).write_text(
                    json.dumps(compact_results(incremental_results), indent=2)
                    + "\n"
                )
                print_score(
                    f"bracket {request['bracket_id']:02d} "
                    f"{request['sampler']} trial {trial:03d} "
                    f"({budget} games)", score,
                )
                records.append(record)
            return records

        while generated < args.trials:
            bracket_id = len(bohb_brackets)
            template = bracket_templates[bracket_id % len(bracket_templates)]
            cycle = bracket_id // len(bracket_templates) + 1
            new_count = min(
                template["new_count"], args.trials - generated
            )
            start_budget = template["start_budget"]
            bracket = {
                "bracket": bracket_id, "cycle": cycle,
                "type": template["name"], "start_budget": start_budget,
                "new_trials": [], "promotions": [],
            }
            requests = []
            for _ in range(new_count):
                index = next_trial
                next_trial += 1
                generated += 1
                model_budget, model_records = bohb_model_records(
                    observations, args.startup_trials
                )
                if not prior_payload and index == 0:
                    parameters, sampler = tuning.default_parameters(), "baseline"
                elif rng.random() < args.bohb_random_fraction:
                    parameters = tuning.random_parameters(rng)
                    sampler = "random-explore"
                elif model_records:
                    parameters = propose_bohb_parameters(
                        rng, model_records, configuration_history,
                        args.min_trial_distance, args.startup_trials,
                    )
                    sampler = f"optuna-tpe@{model_budget}"
                else:
                    parameters, sampler = tuning.random_parameters(rng), "random"
                configuration_history.append({"parameters": parameters})
                bracket["new_trials"].append(index)
                requests.append({
                    "trial": index, "parameters": parameters,
                    "sampler": sampler, "budget": start_budget,
                    "increments": list(range(budget_index[start_budget] + 1)),
                    "bracket_id": bracket_id,
                })
            current_records = evaluate_bohb_batch(requests)
            for record in current_records:
                trials.append(record)
                all_configurations.append(record)
                with (output_dir / "trials.jsonl").open("a") as handle:
                    handle.write(json.dumps(record, sort_keys=True) + "\n")

            current_budget = start_budget
            while current_budget < budgets[-1]:
                next_budget = budgets[budget_index[current_budget] + 1]
                promotion_count = max(
                    1, math.floor(len(current_records) / args.bohb_eta)
                )
                promoted = select_bohb_promotions(
                    current_records, promotion_count
                )
                promoted_records = evaluate_bohb_batch([
                    {
                        "trial": candidate["trial"],
                        "parameters": candidate["parameters"],
                        "sampler": candidate["sampler"],
                        "budget": next_budget,
                        "increments": [budget_index[next_budget]],
                        "bracket_id": bracket_id,
                        "previous_objective": candidate["objective"],
                    }
                    for candidate in promoted
                ])
                bracket["promotions"].append({
                    "from_budget": current_budget,
                    "to_budget": next_budget,
                    "trial_ids": [
                        record["trial"] for record in promoted_records
                    ],
                })
                current_records = promoted_records
                current_budget = next_budget
            search_candidates.extend(current_records)
            bohb_brackets.append(bracket)
            (output_dir / "bohb-brackets.json").write_text(
                json.dumps(bohb_brackets, indent=2) + "\n"
            )

    replication_count = min(args.replication_finalists, len(search_candidates))
    replication_candidates = select_diverse(
        search_candidates, replication_count, args.min_trial_distance
    )
    reuse_prior_validation = bool(
        prior_payload
        and prior_payload.get("metadata", {}).get("seed") == args.seed
        and prior_payload.get("metadata", {}).get("max_steps") == args.max_steps
        and prior_payload.get("metadata", {}).get("replication_rounds")
            == args.replication_rounds
        and prior_payload.get("metadata", {}).get("audit_rounds")
            == args.audit_rounds
        and prior_payload.get("metadata", {}).get("same_maps") == args.same_maps
    )
    prior_replications = {
        record["trial"]: record
        for record in (prior_payload or {}).get("replications", [])
        if record.get("failed", 0) == 0
    } if reuse_prior_validation else {}
    prior_audits = {
        record["trial"]: record
        for record in (prior_payload or {}).get("audits", [])
        if record.get("failed", 0) == 0
    } if reuse_prior_validation else {}
    replications = []
    for candidate in replication_candidates:
        if candidate["trial"] in prior_replications:
            record = dict(prior_replications[candidate["trial"]])
            record.update({"search_objective": candidate["objective"], "reused": True})
            score = record
        else:
            score, results = evaluate(cluster, candidate["parameters"], replication_schedule)
            record = {"trial": candidate["trial"], "parameters": candidate["parameters"],
                      "search_objective": candidate["objective"], **score}
            (output_dir / f"replication-{candidate['trial']:03d}-matches.json").write_text(json.dumps(compact_results(results), indent=2) + "\n")
        replications.append(record)
        print_score(f"replication {candidate['trial']:02d}", score)

    replication_ranked = sorted(replications, key=lambda item: item["objective"], reverse=True)
    audit_count = min(args.audit_finalists, len(replication_ranked))
    audit_candidates = replication_ranked[:audit_count]
    baseline = next(record for record in replications if record["trial"] == 0)
    if all(record["trial"] != 0 for record in audit_candidates):
        audit_candidates[-1] = baseline
    audits = []
    for candidate in audit_candidates:
        if candidate["trial"] in prior_audits:
            record = dict(prior_audits[candidate["trial"]])
            record.update({"replication_objective": candidate["objective"], "reused": True})
            score = record
        else:
            score, results = evaluate(cluster, candidate["parameters"], audit_schedule)
            record = {"trial": candidate["trial"], "parameters": candidate["parameters"],
                      "replication_objective": candidate["objective"], **score}
            (output_dir / f"audit-{candidate['trial']:03d}-matches.json").write_text(json.dumps(compact_results(results), indent=2) + "\n")
        audits.append(record)
        print_score(f"audit {candidate['trial']:02d}", score)

    selection_basis = "replication objective"
    selected = replication_ranked[0]
    if args.parameter_stage == "preemptive-defense-toggle":
        enabled = next((record for record in audits
                        if record["parameters"]["preemptive_defense_enabled"] == 1), None)
        disabled = next((record for record in audits
                         if record["parameters"]["preemptive_defense_enabled"] == 0), None)
        if enabled is not None and disabled is not None:
            # This ablation deliberately ignores the shaped objective. The new
            # strategy earns its default only through a strict held-out gain;
            # ties conservatively select off.
            selected = (enabled if enabled["portfolio_score"]
                        > disabled["portfolio_score"] else disabled)
            selection_basis = (
                "aggregate held-out audit score (strict win required for on; "
                "ties select off)"
            )
    wall_seconds = round(time.monotonic() - started, 3)
    metadata = {
        "created_at": datetime.now(timezone.utc).isoformat(), "seed": args.seed,
        "strategy_schema_version": 2,
        "proposal_engine": tuning.PROPOSAL_ENGINE,
        "optuna_version": dependency_optuna_version,
        "sampler_seed": args.sampler_seed, "workers": args.workers,
        "require_all_workers": args.require_all_workers,
        "max_steps": args.max_steps, "search_rounds": args.search_rounds,
        "replication_rounds": args.replication_rounds, "audit_rounds": args.audit_rounds,
        "parameter_stage": args.parameter_stage, "same_maps": args.same_maps,
        "selection_basis": selection_basis,
        "bohb": args.bohb, "bohb_eta": args.bohb_eta,
        "bohb_random_fraction": args.bohb_random_fraction,
        "bohb_budgets": [49, 147, 440] if args.bohb else [],
        "bohb_rung_sizes": [len(observations[budget]) for budget in (49, 147, 440)]
            if args.bohb else [],
        "bohb_bracket_count": len(bohb_brackets) if args.bohb else 0,
        "bohb_schedule": "cross-configuration-batched-hyperband-brackets"
            if args.bohb else None,
        "warm_start_source": str(prior_source)
            if prior_source and not resume_mode else None,
        "resume_source": str(prior_source) if resume_mode else None,
        "resumed_configurations": len(
            prior_payload.get("trials", [])
        ) if resume_mode and prior_payload else 0,
        "warm_start_configurations": len(
            prior_payload.get("trials", [])
        ) if prior_payload else 0,
        "warm_start_full_budget_candidates": len(
            [record for record in prior_payload.get("full_budget_candidates", [])
             if record.get("failed", 0) == 0]
        ) if prior_payload else 0,
        "new_configurations": len(trials),
        "total_configurations": len(all_configurations) if args.bohb else len(trials),
        "reused_replications": sum(bool(record.get("reused")) for record in replications),
        "reused_audits": sum(bool(record.get("reused")) for record in audits),
        "search_games_total": search_games_total,
        "search_games_per_trial": len(search_schedule),
        "replication_games_per_finalist": len(replication_schedule),
        "audit_games_per_finalist": len(audit_schedule),
        "train_maps": {key: [item["name"] for item in value] for key, value in maps["train"].items()},
        "audit_maps": {key: [item["name"] for item in value] for key, value in audit_map_set.items()},
        "wall_seconds": wall_seconds,
        "score_signal": {
            "version": scoring.SIGNAL_VERSION,
            "range": [scoring.SCORE_MIN, scoring.SCORE_MAX],
            "outcome_weight": scoring.OUTCOME_WEIGHT,
            "speed_weight": scoring.SPEED_WEIGHT,
            "dominance_weight": scoring.DOMINANCE_WEIGHT,
        },
    }
    selected_resolved_strategy = tuning.resolve_candidate(
        binary, selected["parameters"]
    )
    payload = {"metadata": metadata,
               "strategy_schema": tuning.STRATEGY_SCHEMA,
               "baseline_strategy": tuning.RESOLVED_STRATEGY,
               "resolved_strategy": selected_resolved_strategy,
               "trials": all_configurations if args.bohb else trials,
               "new_trials": trials if args.bohb else [],
               "bohb_rungs": [observations[budget] for budget in (49, 147, 440)]
                   if args.bohb else [],
               "new_bohb_rungs": bohb_rungs if args.bohb else [],
               "bohb_brackets": bohb_brackets if args.bohb else [],
               "full_budget_candidates": search_candidates,
               "replications": replications,
               "audits": audits, "selected": selected,
               "parameters": [parameter.__dict__ for parameter in tuning.PARAMETERS]}
    (output_dir / "optimization.json").write_text(json.dumps(payload, indent=2) + "\n")
    selected_overrides = tuning.tuning_string(selected["parameters"]) + "\n"
    (output_dir / "selected-strategy-overrides.txt").write_text(selected_overrides)
    (output_dir / "replication-selected-strategy-overrides.txt").write_text(
        selected_overrides
    )
    (output_dir / "resolved-strategy.json").write_text(
        json.dumps(selected_resolved_strategy, indent=2) + "\n"
    )
    write_report(output_dir, payload)
    print(f"reports: {output_dir}", flush=True)
    return 1 if any(
        record["failed"] for record in search_records + replications + audits
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
