#!/usr/bin/env python3
"""Run a resumable LAN-only paired Maxima team-defense tournament."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any

import optimize_maxima_portfolio as portfolio
import run_nicowar_2v2_tournament as tournament


DEFAULT_WORKERS = (
    ("local", 8),
    ("pharaoh-dev-1.local", 4),
    ("pharaoh-dev-2.local", 4),
    ("pharaoh-dev-3.local", 4),
    ("devlaptop.local", 16),
    ("therig.local", 32),
)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--batch-rounds", type=int, default=5)
    parser.add_argument("--seed", type=int, default=0x5445414D)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer")
    parser.add_argument(
        "--remote-only",
        action="store_true",
        help="dispatch unfinished checkpoint cases only to .local workers",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=root / "tournament-results/maxima-team-defense-lan-100seed-20260903",
    )
    args = parser.parse_args()
    if args.rounds < 1 or args.batch_rounds < 1 or args.max_steps < 1:
        parser.error("rounds, batch-rounds, and max-steps must be positive")
    return args


def checkpoint_path(output_dir: Path, round_number: int, arm: str) -> Path:
    return output_dir / "checkpoints" / f"batch-{round_number:03d}-{arm}.json"


def save_checkpoint(path: Path, metadata: dict[str, Any], matches: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps({"metadata": metadata, "matches": matches}) + "\n")
    temporary.replace(path)


def load_checkpoint(path: Path) -> list[dict[str, Any]]:
    return json.loads(path.read_text())["matches"]


def match_key(match: dict[str, Any]) -> tuple[Any, ...]:
    return (
        match["round"], match["map"], match["seed"],
        match["partition"], match["swap"],
    )


def paired_summary(on: list[dict[str, Any]], off: list[dict[str, Any]]) -> dict[str, Any]:
    def key(match: dict[str, Any]) -> tuple[Any, ...]:
        return (
            match["round"], match["map"], match["seed"],
            match["partition"], match["swap"],
        )

    on_by_key = {key(match): match for match in on if match["status"] == "completed"}
    off_by_key = {key(match): match for match in off if match["status"] == "completed"}
    shared = sorted(set(on_by_key) & set(off_by_key))
    rows = []
    blocks: dict[tuple[Any, ...], list[float]] = {}
    for case in shared:
        enabled = on_by_key[case]
        disabled = off_by_key[case]
        difference = float(enabled["victory_score_a"]) - float(disabled["victory_score_a"])
        block = case[:3]
        blocks.setdefault(block, []).append(difference)
        rows.append(
            {
                "round": case[0], "map": case[1], "seed": case[2],
                "partition": case[3], "swap": case[4],
                "on_victory_score": enabled["victory_score_a"],
                "off_victory_score": disabled["victory_score_a"],
                "difference": round(difference, 6),
                "on_winner": enabled["winner_side"],
                "off_winner": disabled["winner_side"],
                "worker_on": enabled.get("worker_host", ""),
                "worker_off": disabled.get("worker_host", ""),
            }
        )
    block_differences = [mean(values) for values in blocks.values()]
    interval = portfolio.ffa_runner.mean_interval(block_differences, (-200.0, 200.0))
    maps: dict[str, list[float]] = {}
    for row in rows:
        maps.setdefault(str(row["map"]), []).append(float(row["difference"]))
    return {
        "paired_games": len(rows),
        "paired_map_seed_blocks": len(block_differences),
        "mean_victory_score_difference": round(mean(block_differences), 6),
        "ci_low": interval["low"],
        "ci_high": interval["high"],
        "blocks_favoring_on": sum(value > 0 for value in block_differences),
        "blocks_favoring_off": sum(value < 0 for value in block_differences),
        "blocks_tied": sum(value == 0 for value in block_differences),
        "sign_test_p": portfolio.ffa_runner.exact_sign_test(
            sum(value > 0 for value in block_differences),
            sum(value < 0 for value in block_differences),
        ),
        "maps": {
            name: {"games": len(values), "mean_difference": round(mean(values), 6)}
            for name, values in sorted(maps.items())
        },
        "rows": rows,
    }


def write_paired_report(output_dir: Path, summary: dict[str, Any]) -> None:
    rows = summary.pop("rows")
    sign_test = summary["sign_test_p"]
    sign_test_text = "n/a" if sign_test is None else f"{sign_test:.6g}"
    (output_dir / "paired-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (output_dir / "paired-games.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]) if rows else [])
        if rows:
            writer.writeheader()
            writer.writerows(rows)
    lines = [
        "# Maxima team-defense paired comparison",
        "",
        f"- Paired games: {summary['paired_games']}",
        f"- Independent map/seed blocks: {summary['paired_map_seed_blocks']}",
        "- Difference: defense-on Maxima victory score minus defense-off Maxima victory score",
        f"- Mean difference: {summary['mean_victory_score_difference']:+.4f}",
        f"- 95% block interval: {summary['ci_low']:+.4f} to {summary['ci_high']:+.4f}",
        f"- Blocks favoring on/off/tied: {summary['blocks_favoring_on']}/"
        f"{summary['blocks_favoring_off']}/{summary['blocks_tied']}",
        f"- Exact sign-test p: {sign_test_text}",
        "",
        "| Map | Games | Mean on-minus-off victory score |",
        "|---|---:|---:|",
    ]
    for name, values in summary["maps"].items():
        lines.append(f"| {name} | {values['games']} | {values['mean_difference']:+.4f} |")
    (output_dir / "paired-summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    output_dir = args.output_dir if args.output_dir.is_absolute() else root / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    maps = tournament.ffa.filter_maps(
        tournament.ffa.discover_maps(args.binary, root), None, True
    )
    schedule = tournament.make_schedule(
        args.rounds, maps, [("maxima", "original")], args.seed, args.max_steps
    )
    for match in schedule:
        match["kind"] = "2v2"
    workers = [
        worker for worker in DEFAULT_WORKERS
        if not args.remote_only or worker[0] != "local"
    ]
    if any(host != "local" and not host.endswith(".local") for host, _ in workers):
        raise RuntimeError("LAN tournament worker list contains a non-.local host")
    cluster = portfolio.PortfolioCluster(
        args.binary, root, workers, args.remote_root, args.timeout,
        retries=1, require_all_workers=True,
    )
    started = time.monotonic()
    all_results = {"on": [], "off": []}
    batches = (args.rounds + args.batch_rounds - 1) // args.batch_rounds
    print(
        f"LAN-only paired tournament: {len(schedule) * 2} games, "
        f"{args.rounds} seeds/map, {len(maps)} maps, {sum(count for _, count in workers)} slots",
        flush=True,
    )
    for batch in range(1, batches + 1):
        first_round = (batch - 1) * args.batch_rounds + 1
        last_round = min(args.rounds, batch * args.batch_rounds)
        expected_by_round = {
            round_number: [
                match for match in schedule if match["round"] == round_number
            ]
            for round_number in range(first_round, last_round + 1)
        }
        stored: dict[str, dict[int, list[dict[str, Any]]]] = {
            "on": {}, "off": {},
        }
        pending: dict[str, list[dict[str, Any]]] = {"on": [], "off": []}
        for round_number in range(first_round, last_round + 1):
            for arm in ("on", "off"):
                path = checkpoint_path(output_dir, round_number, arm)
                prior = load_checkpoint(path) if path.exists() else []
                stored[arm][round_number] = prior
                completed_keys = {
                    match_key(match) for match in prior
                    if match.get("status") == "completed"
                }
                pending[arm].extend(
                    match for match in expected_by_round[round_number]
                    if match_key(match) not in completed_keys
                )
        pending_count = sum(len(matches) for matches in pending.values())
        if pending_count == 0:
            for arm in ("on", "off"):
                for round_number in range(first_round, last_round + 1):
                    all_results[arm].extend(stored[arm][round_number])
            print(f"[{batch}/{batches}] resumed rounds {first_round}-{last_round}", flush=True)
            continue
        print(
            f"[{batch}/{batches}] repairing/running rounds {first_round}-{last_round} "
            f"({pending_count} unfinished games)", flush=True,
        )
        workloads = []
        for key, arm, override in (
            (0, "on", "teamplay.defense_enabled=true"),
            (1, "off", "teamplay.defense_enabled=false"),
        ):
            if pending[arm]:
                workloads.append((key, pending[arm], override))
        result = cluster.run_many(workloads)
        rerun = {
            "on": result.get(0, []),
            "off": result.get(1, []),
        }
        for round_number in range(first_round, last_round + 1):
            stamp = {
                "batch": batch, "rounds": [round_number, round_number],
                "created_at": datetime.now(timezone.utc).isoformat(),
            }
            for arm in ("on", "off"):
                replacements = {
                    match_key(match): match for match in rerun[arm]
                    if match["round"] == round_number
                }
                merged = {
                    match_key(match): match
                    for match in stored[arm][round_number]
                }
                merged.update(replacements)
                ordered = [
                    merged[match_key(match)]
                    for match in expected_by_round[round_number]
                    if match_key(match) in merged
                ]
                if replacements or not checkpoint_path(
                    output_dir, round_number, arm
                ).exists():
                    save_checkpoint(
                        checkpoint_path(output_dir, round_number, arm), stamp,
                        ordered,
                    )
                all_results[arm].extend(ordered)
        completed = sum(
            match["status"] == "completed"
            for arm_results in rerun.values() for match in arm_results
        )
        print(
            f"[{batch}/{batches}] checkpointed {completed}/{pending_count} repaired; "
            f"elapsed {time.monotonic() - started:.1f}s",
            flush=True,
        )

    wall = round(time.monotonic() - started, 3)
    metadata_base = {
        "created_at": datetime.now(timezone.utc).isoformat(),
        "binary": str(args.binary), "base_seed": args.seed,
        "jobs": sum(count for _, count in DEFAULT_WORKERS),
        "workers": list(DEFAULT_WORKERS),
        "repair_workers": workers if args.remote_only else [],
        "max_steps": args.max_steps, "telemetry": False,
        "wall_seconds": wall, "maps": maps,
        "matchups": [["maxima", "original"]], "rounds": args.rounds,
        "schedule_description": (
            f"{args.rounds} round(s) x {len(maps)} map(s) x 1 matchup x "
            "3 seat partitions x 2 side swaps"
        ),
        "score_signal": {
            "version": tournament.scoring.SIGNAL_VERSION,
            "range": [tournament.scoring.SCORE_MIN, tournament.scoring.SCORE_MAX],
            "outcome_weight": tournament.scoring.OUTCOME_WEIGHT,
            "speed_weight": tournament.scoring.SPEED_WEIGHT,
            "dominance_weight": tournament.scoring.DOMINANCE_WEIGHT,
        },
    }
    for arm, override in (
        ("on", "teamplay.defense_enabled=true"),
        ("off", "teamplay.defense_enabled=false"),
    ):
        results = sorted(all_results[arm], key=lambda match: match["id"])
        metadata = {**metadata_base, "maxima_overrides": override}
        summary = tournament.build_summary(results)
        tournament.write_reports(output_dir / arm, results, summary, metadata)
    write_paired_report(
        output_dir, paired_summary(all_results["on"], all_results["off"])
    )
    print(f"Reports written to {output_dir}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
