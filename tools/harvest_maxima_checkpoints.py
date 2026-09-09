#!/usr/bin/env python3
"""Generate Maxima source games, periodic saves, and a stratified checkpoint bank."""

from __future__ import annotations

import argparse
import hashlib
import json
import shlex
import subprocess
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from queue import Empty, Queue
from typing import Any

import build_maxima_checkpoint_bank as bank
import maxima_harvest_transfer as transfer
import optimize_maxima_portfolio as portfolio


ROOT = Path(__file__).resolve().parent.parent
REMOTE_WORKERS = tuple(
    worker for worker in portfolio.STANDARD_WORKERS if worker[0] != "local"
)
DEFAULT_MAPS = (
    "maps/FourSquares1.map", "maps/G2.map", "maps/Garden_3.map",
    "maps/Holiday_Island_2.map", "maps/Isles.map", "maps/Migration.map",
    "maps/balanced.map",
)
FORMAT_PLAYERS = {"duel": 2, "ffa3": 3, "ffa4": 4, "ffa5": 5, "2v2": 4}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build-tournament/src/glob2")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--map", action="append", dest="maps")
    parser.add_argument("--rounds", type=int, default=1,
                        help="Independent seeds per map")
    parser.add_argument("--seed", type=int, default=0x41424C41)
    parser.add_argument("--players", type=int)
    parser.add_argument("--opponent-ai", type=int, choices=(1, 2, 5, 7), default=5)
    parser.add_argument("--all-seats", action="store_true",
                        help="Harvest every focal non-team seat for each map/seed block")
    parser.add_argument("--seat-rotations", type=int, default=1,
                        help="Distinct focal seats per source block, rotated across seeds")
    parser.add_argument("--format", choices=tuple(FORMAT_PLAYERS), default="duel",
                        help="Source-game format; 2v2 requires four-team maps")
    parser.add_argument("--max-steps", type=int, default=30000)
    parser.add_argument("--interval", type=int, default=1000)
    parser.add_argument("--worker", action="append", type=portfolio.parse_worker,
                        dest="cluster_workers",
                        help="Cluster allocation HOST:JOBS; repeat for each host")
    parser.add_argument("--workers", type=int, dest="legacy_workers",
                        help=argparse.SUPPRESS)
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer",
                        help="Immutable checkout path used by SSH workers")
    parser.add_argument("--allow-missing-workers", action="store_true",
                        help="Continue if configured cluster slots cannot be reached")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--transfer-timeout", type=int, default=900)
    parser.add_argument("--transfer-queue-per-slot", type=int, default=2,
                        help="Bound each host's pending collections relative to its compute slots")
    parser.add_argument("--quota", type=int, default=40)
    parser.add_argument("--max-lead", type=int, default=1000)
    args = parser.parse_args()
    expected_players = FORMAT_PLAYERS[args.format]
    if args.players is not None and args.players != expected_players:
        parser.error(f"{args.format} requires {expected_players} players")
    args.players = expected_players
    if not 1 <= args.seat_rotations <= args.players:
        parser.error("seat-rotations must be between one and the format's player count")
    if args.cluster_workers:
        args.workers = args.cluster_workers
    elif args.legacy_workers is not None:
        args.workers = [("local", args.legacy_workers)]
    else:
        args.workers = list(REMOTE_WORKERS)
    positive = (args.rounds, args.players, args.max_steps, args.interval,
                args.timeout, args.transfer_timeout, args.transfer_queue_per_slot,
                args.quota, *(count for _host, count in args.workers))
    if min(positive) < 1 or args.max_lead < 0:
        parser.error("counts, steps, intervals, workers, timeout, and quota must be positive")
    return args


def schedule(args: argparse.Namespace) -> list[dict[str, Any]]:
    maps = tuple(args.maps or DEFAULT_MAPS)
    result = []
    run_id = 0
    for round_number in range(args.rounds):
        for map_index, map_name in enumerate(maps):
            map_path = Path(map_name)
            if not map_path.is_absolute():
                map_path = ROOT / map_path
            if not map_path.is_file():
                raise ValueError(f"map does not exist: {map_path}")
            seed = (args.seed + round_number * 104729 + map_index * 8191) & 0x7FFFFFFF
            seat = round_number % args.players
            rotations = args.players if args.all_seats else getattr(args, 'seat_rotations', 1)
            seats = ([(seat + (offset * args.players)//rotations) % args.players
                      for offset in range(rotations)] if args.format != '2v2' else [seat])
            for focal in seats:
                result.append({
                    "id": run_id, "round": round_number, "map": str(map_path),
                    "seed": seed, "candidate_seat": focal, "format": args.format,
                    "opponent_ai": args.opponent_ai,
                    "players": args.players,
                    "partition": round_number % 3, "swap": (round_number // 3) % 2,
                })
                run_id += 1
    return result


def source_command(binary: str, map_path: str, saves: str, args: argparse.Namespace,
                   item: dict[str, Any]) -> list[str]:
    """Construct one source-game command for either a local or remote checkout."""
    if item["format"] == "2v2":
        command = [
            binary, "-nicowar-2v2-match-nox", map_path, str(item["seed"]),
            "7", str(item.get("opponent_ai", 5)), str(item["partition"]), str(item["swap"]),
            str(args.max_steps),
        ]
    else:
        command = [
            binary, "-nicowar-scenario-match-nox", map_path, str(item["seed"]),
            str(args.players), "7", str(item.get("opponent_ai", 5)), str(item["candidate_seat"]), "0",
            str(args.max_steps),
        ]
    command.extend(["--maxima-checkpoint-harvest", saves, str(args.interval)])
    return command


def source_result(output_text: str, returncode: int, error: str, started: float,
                  output: Path, item: dict[str, Any], worker_host: str) -> dict[str, Any]:
    name = f"run-{item['id']:05d}"
    run_dir = output / name
    saves = run_dir / "saves"
    saves.mkdir(parents=True, exist_ok=True)
    log = run_dir / f"{name}.log"
    log.write_text(output_text, encoding="utf-8")
    saved = output_text.count("MAXIMA_CHECKPOINT_SAVED\t")
    opportunities = output_text.count("MAXIMA_ABLATION_OPPORTUNITY\t")
    if not saved:
        error = error or "engine produced no checkpoints"
    return {
        **item, "name": name, "returncode": returncode, "error": error,
        "status": "failed" if error else "completed", "worker_host": worker_host,
        "wall_seconds": round(time.monotonic() - started, 3), "log": str(log),
        "checkpoints": saved, "opportunities": opportunities,
    }


def run_source(binary: Path, output: Path, args: argparse.Namespace,
               item: dict[str, Any]) -> dict[str, Any]:
    name = f"run-{item['id']:05d}"
    saves = output / name / "saves"
    saves.mkdir(parents=True, exist_ok=True)
    command = source_command(str(binary), item["map"], str(saves), args, item)
    started = time.monotonic()
    try:
        process = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=args.timeout)
        output_text = process.stdout
        error = "" if process.returncode == 0 else f"engine exited {process.returncode}"
        returncode = process.returncode
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        output_text = stdout + stderr
        error = f"engine exceeded {args.timeout} seconds"
        returncode = 124
    return source_result(output_text, returncode, error, started, output, item, "local")


class HarvestCluster(portfolio.PortfolioCluster):
    """Harvest remotely and pull every generated checkpoint into the controller bank."""

    def __init__(self, binary: Path, repo_root: Path, workers: list[tuple[str, int]],
                 remote_root: str, timeout: int, require_all_workers: bool,
                 output: Path, args: argparse.Namespace, campaign: str) -> None:
        super().__init__(binary, repo_root, workers, remote_root, timeout, retries=1,
                         require_all_workers=require_all_workers)
        self.output = output
        self.args = args
        self.remote_stage = f"{self.remote_root}/.maxima-harvest/{campaign}"
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
        if "name" in result:
            return result
        return source_result(
            str(result.get("output", "")), int(result.get("returncode", 1)),
            str(result.get("error", "worker failed")), time.monotonic(),
            self.output, result, str(result.get("worker_host", "unknown")),
        )

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
            raise RuntimeError("no harvest workers are currently reachable")
        if self.require_all_workers and len(active) != len(self.slots):
            missing = sorted(set(self.slots) - set(active))
            raise RuntimeError(f"required worker connections unavailable: {missing}")
        return active

    def request_hash(self, item: dict[str, Any]) -> str:
        fields = {key: item.get(key) for key in
                  ('id', 'map', 'seed', 'candidate_seat', 'format', 'partition', 'swap', 'opponent_ai')}
        fields.update(max_steps=self.args.max_steps, interval=self.args.interval,
                      remote_root=self.remote_root)
        return hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()

    def pending_path(self, item: dict[str, Any]) -> Path:
        return self.output / f"run-{item['id']:05d}" / 'PENDING_TRANSFER.json'

    def run(self, schedule: list[dict[str, Any]], settings: str) -> list[dict[str, Any]]:
        """Compute slots feed independent, bounded, per-host collection queues."""
        del settings
        started = time.monotonic()
        pending: Queue[dict[str, Any]] = Queue()
        results: list[dict[str, Any]] = []
        lock = threading.Lock()
        active_slots = self._active_slots()
        host_slots = {host: count for host, count in self.workers if host != 'local'}
        transfers = {host: Queue(maxsize=max(1, count * self.args.transfer_queue_per_slot))
                     for host, count in host_slots.items()}
        # Collection has its own SSH connection, never a compute slot's channel.
        for host, count in host_slots.items():
            if not self._prepare_connection(host, count):
                raise RuntimeError(f'could not prepare collection connection on {host}')
        resumed = []
        for item in schedule:
            path = self.pending_path(item)
            if path.exists():
                record = json.loads(path.read_text())
                if record['request_hash'] != self.request_hash(item):
                    raise RuntimeError(f'pending transfer belongs to different source settings: {path}')
                if record['worker_host'] not in transfers:
                    raise RuntimeError(f'pending transfer host is unavailable: {path}')
                resumed.append(record)
            else:
                pending.put(item)
        computed = len(resumed)
        progress_path = self.output / 'pipeline-progress.json'

        def progress() -> None:
            payload = {'scheduled': len(schedule), 'computed': computed, 'collected': len(results),
                       'pending_compute': pending.qsize(),
                       'pending_collection': {h: q.qsize() for h, q in transfers.items()},
                       'errors': sum(bool(row['error']) for row in results),
                       'elapsed_seconds': round(time.monotonic()-started, 3)}
            temporary = progress_path.with_suffix('.tmp')
            temporary.write_text(json.dumps(payload, indent=2)+'\n'); temporary.replace(progress_path)

        def record_result(result: dict[str, Any]) -> None:
            with lock:
                results.append(result)
                progress()
                with (self.output / 'source-progress.jsonl').open('a') as stream:
                    stream.write(json.dumps(result)+'\n')
                print(f"Harvest progress {len(results)}/{len(schedule)}: {result['name']} "
                      f"on {result['worker_host']} error={result['error'] or 'none'} "
                      f"engine={result.get('engine_seconds', 0):.2f}s "
                      f"collection={result.get('transfer_seconds', 0):.2f}s", flush=True)

        def collect_host(host: str) -> None:
            queue = transfers[host]
            while True:
                item = queue.get()
                try:
                    if item is None: return
                    try:
                        result = self._collect(host, host_slots[host], item)
                    except Exception as exc:
                        result = source_result('', 1, f'checkpoint collection failed: {exc}',
                                               time.monotonic(), self.output, item, host)
                        result['engine_seconds'] = item.get('engine_seconds', 0)
                        # PENDING_TRANSFER and the remote logs/saves remain intact.
                    record_result(result)
                finally:
                    queue.task_done()

        def compute(slot: tuple[str, int]) -> None:
            nonlocal computed
            host, group = slot
            while True:
                try:
                    item = pending.get_nowait()
                except Empty:
                    return
                try:
                    try:
                        result = self._run_one(host, group, item, '')
                    except Exception as exc:
                        result = dict(item, error=f'worker exception: {exc}', output='',
                                      returncode=1, worker_host=host, status='failed')
                    with lock:
                        computed += 1
                        progress()
                        print(f'Compute progress {computed}/{len(schedule)} on {host}; '
                              f'collected={len(results)}', flush=True)
                    if 'remote_run' in result:
                        transfers[host].put(result)
                    else:
                        result = result if 'name' in result else self._normalize_failure(result)
                        result['worker_slot'] = group
                        record_result(result)
                finally:
                    pending.task_done()

        with ThreadPoolExecutor(max_workers=max(1, len(transfers))) as collection_pool:
            collectors = [collection_pool.submit(collect_host, host) for host in transfers]
            try:
                for item in resumed:
                    transfers[item['worker_host']].put(item)
                with ThreadPoolExecutor(max_workers=len(active_slots)) as compute_pool:
                    futures = [compute_pool.submit(compute, slot) for slot in active_slots]
                    for future in futures: future.result()
            finally:
                for queue in transfers.values(): queue.put(None)
                for future in collectors: future.result()
        return sorted(results, key=lambda row: int(row['id']))

    def _execute(self, host: str, group: int, item: dict[str, Any],
                 settings: str) -> dict[str, Any]:
        del settings
        if host == 'local':
            return run_source(self.binary, self.output, self.args, item)
        started = time.monotonic()
        started_epoch = time.time()
        name = f"run-{item['id']:05d}"
        remote_run = f'{self.remote_stage}/{name}'
        remote_saves = f'{remote_run}/saves'
        relative_map = Path(item['map']).resolve().relative_to(ROOT.resolve())
        remote_binary = f'{self.remote_root}/build-tournament/src/glob2'
        command = source_command(remote_binary, f'{self.remote_root}/{relative_map.as_posix()}',
                                 remote_saves, self.args, item)
        previous_saves = f'{remote_run}/previous-saves-{time.time_ns()}'
        # Durable engine output is independent of the connection used to collect it.
        remote = (f'cd {shlex.quote(self.remote_root)} && '
                  f'{{ if test -d {shlex.quote(remote_saves)}; then '
                  f'mv {shlex.quote(remote_saves)} {shlex.quote(previous_saves)}; fi; }} && '
                  f'mkdir -p {shlex.quote(remote_saves)} && '
                  f'{{ LD_LIBRARY_PATH={shlex.quote(self.remote_root + "/lib")} '
                  f'{shlex.join(command)} > {shlex.quote(remote_run + "/engine.log")} 2>&1; '
                  f'result=$?; printf "%s\\n" "$result" > '
                  f'{shlex.quote(remote_run + "/engine.exit")}; exit "$result"; }}')
        process = subprocess.run(self._ssh_arguments(host, group)+[host, remote],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=self.timeout)
        result = {**item, 'remote_run': remote_run, 'remote_saves': remote_saves,
                  'worker_host': host, 'worker_slot': group, 'returncode': process.returncode,
                  'status': 'completed' if process.returncode == 0 else 'failed',
                  'error': '' if process.returncode == 0 else f'engine exited {process.returncode}',
                  'engine_seconds': round(time.monotonic()-started, 3),
                  'engine_started_epoch': started_epoch, 'queued_epoch': time.time(),
                  'request_hash': self.request_hash(item)}
        path = self.pending_path(item); path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix('.tmp')
        temporary.write_text(json.dumps(result, indent=2)+'\n'); temporary.replace(path)
        return result

    def _collect(self, host: str, group: int, item: dict[str, Any]) -> dict[str, Any]:
        name = f"run-{item['id']:05d}"
        destination = self.output / name
        queued_seconds = time.time()-item['queued_epoch']
        statistics = transfer.collect(self._ssh_arguments(host, group), host, item['remote_run'],
                                      destination, self.args.transfer_timeout)
        output = (destination / 'engine.log').read_text(errors='replace')
        returncode = int((destination / 'engine.exit').read_text().strip())
        local_saves = str((destination / 'saves').resolve())
        output = output.replace(item['remote_saves']+'/', local_saves+'/')
        error = '' if returncode == 0 else f'engine exited {returncode}'
        if not error and 'MATCH_RESULT\t' not in output:
            error = 'engine log contains no terminal match result'
        # All referenced checkpoints must be covered by the verified archive.
        if not error:
            expected = {Path(bank.fields(line)['path']).name for line in output.splitlines()
                        if line.startswith('MAXIMA_CHECKPOINT_SAVED\t')}
            received = {Path(path).name for path in statistics['file_hashes'] if path.startswith('saves/')}
            if not expected or expected != received:
                error = 'checkpoint archive does not match the engine save log'
        elapsed = time.time()-item['engine_started_epoch']
        result = source_result(output, returncode, error, time.monotonic()-elapsed,
                               self.output, item, host)
        result.update(statistics, queue_seconds=round(queued_seconds, 3),
                      engine_seconds=item['engine_seconds'], worker_slot=item['worker_slot'])
        result_path = destination / 'result.json'
        temporary = result_path.with_suffix('.tmp')
        temporary.write_text(json.dumps(result, indent=2)+'\n'); temporary.replace(result_path)
        if not error:
            # Never delete the only complete copy until verification and metadata commit.
            cleanup = self._ssh_arguments(host, group)+[
                host, f"rm -rf -- {shlex.quote(item['remote_run'])}"]
            try:
                subprocess.run(cleanup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
            except (OSError, subprocess.TimeoutExpired):
                print(f'Checkpoint copies committed; remote cleanup deferred for {name}', flush=True)
            self.pending_path(item).unlink(missing_ok=True)
        return result


def cached_source(output: Path, item: dict[str, Any]) -> dict[str, Any] | None:
    name = f"run-{item['id']:05d}"
    result_file = output / name / "result.json"
    if result_file.exists():
        result = json.loads(result_file.read_text())
        if not result.get("error") and result.get("file_hashes"):
            for key in ('id', 'map', 'seed', 'candidate_seat', 'format', 'partition', 'swap', 'opponent_ai'):
                if result.get(key) != item.get(key):
                    raise ValueError(f"cached source settings changed: {result_file}")
            for relative, digest in result['file_hashes'].items():
                if transfer.file_hash(output / name / relative) != digest:
                    raise ValueError(f"cached checkpoint checksum changed: {relative}")
            return {**result, 'resumed': True}
    log = output / name / f"{name}.log"
    if not log.is_file():
        return None
    output_text = log.read_text(encoding="utf-8", errors="replace")
    if "MAXIMA_CHECKPOINT_SAVED\t" not in output_text or "MATCH_RESULT\t" not in output_text:
        return None
    saved_paths = [
        Path(bank.fields(line).get("path", ""))
        for line in output_text.splitlines()
        if line.startswith("MAXIMA_CHECKPOINT_SAVED\t")
    ]
    if not saved_paths or any(not path.is_file() for path in saved_paths):
        return None
    previous_results = output / "source-runs.json"
    if previous_results.exists():
        for previous in json.loads(previous_results.read_text()):
            if previous['id'] == item['id'] and not previous.get('error'):
                if all(previous.get(key) == item.get(key) for key in
                       ('map', 'seed', 'candidate_seat', 'format', 'partition', 'swap', 'opponent_ai')):
                    return {**previous, 'resumed': True}
    return source_result(output_text, 0, "", time.monotonic(), output,
                         item, "resume-cache")


def main() -> int:
    args = parse_args()
    binary = args.binary if args.binary.is_absolute() else ROOT / args.binary
    binary = binary.resolve()
    if not binary.is_file():
        print(f"binary not found: {binary}")
        return 2
    output = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    try:
        items = schedule(args)
    except ValueError as exc:
        print(str(exc))
        return 2
    campaign = hashlib.sha256(str(output.resolve()).encode()).hexdigest()[:16]
    results = []
    pending_items = []
    for item in items:
        cached = cached_source(output, item)
        if cached is None:
            pending_items.append(item)
        else:
            results.append(cached)
    cluster = HarvestCluster(
        binary, ROOT, args.workers, args.remote_root, args.timeout,
        not args.allow_missing_workers, output, args, campaign,
    )
    print(f"Harvesting {len(pending_items)} source games ({len(results)} reused) on "
          f"{sum(count for _host, count in args.workers)} cluster slots: "
          f"{', '.join(f'{host}:{count}' for host, count in args.workers)}", flush=True)
    try:
        results.extend(cluster.run(pending_items, "") if pending_items else [])
    except RuntimeError as exc:
        print(str(exc))
        return 2
    finally:
        cluster.close()
    results.sort(key=lambda row: int(row["id"]))
    for result in results:
        print(f"{result['name']}: host={result['worker_host']} "
              f"checkpoints={result['checkpoints']} opportunities={result['opportunities']} "
              f"error={result['error'] or 'none'}", flush=True)
    logs = [Path(row["log"]) for row in results if not row["error"]]
    candidates = []
    for log in logs:
        candidates.extend(bank.parse_log(log, args.max_lead))
    chosen = bank.select(candidates, args.quota)
    coverage = {}
    for switch in bank.ablation.ALL_SWITCHES:
        available = [row for row in candidates if row["switch"] == switch]
        retained = [row for row in chosen if row["switch"] == switch]
        coverage[switch] = {
            "source_games_available": len(available),
            "source_games_retained": len(retained), "quota": args.quota,
        }
    manifest = {
        "schema_version": 1,
        "selection": "one pre-trigger checkpoint per switch/source; round-robin severity tertiles",
        "checkpoint_count": len(chosen), "coverage": coverage, "checkpoints": chosen,
    }
    (output / "source-runs.json").write_text(json.dumps(results, indent=2) + "\n",
                                             encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                          encoding="utf-8")
    failed = sum(bool(row["error"]) for row in results)
    print(f"Wrote {len(chosen)} selected checkpoints from {len(results) - failed} source games "
          f"to {output / 'manifest.json'}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
