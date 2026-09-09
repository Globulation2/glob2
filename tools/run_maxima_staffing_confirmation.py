#!/usr/bin/env python3
"""Confirm the construction+birth Maxima candidate over every compatible map."""

from __future__ import annotations

import argparse
import gzip
import json
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import analyze_nicowar_tournament as analysis
import optimize_maxima_portfolio as portfolio
import run_maxima_all_formats_tournament as tournament
import run_maxima_staffing_ablation as staffing


DEFAULT_WORKERS = tournament.DEFAULT_WORKERS
CANDIDATE = {**staffing.CONSTRUCTION, **staffing.BIRTH}
CONFIGURATIONS = (
    {"id": 0, "name": "baseline", "parameters": {}, "overrides": ""},
    {
        "id": 1,
        "name": "construction+birth",
        "parameters": CANDIDATE,
        "overrides": staffing.override_string(CANDIDATE),
    },
)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--rounds", type=int, default=5, help="Independent seeds per map/format")
    parser.add_argument("--seed", type=int, default=0x434F4E46)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--worker", action="append", type=portfolio.parse_worker, dest="workers")
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-telemetry", action="store_true")
    args = parser.parse_args()
    if min(args.rounds, args.max_steps, args.timeout) < 1 or args.retries < 0:
        parser.error("rounds, max-steps, and timeout must be positive; retries must be nonnegative")
    if not args.workers:
        args.workers = list(DEFAULT_WORKERS)
    return args


def load_spool(spool: Path, games_per_variant: int) -> tuple[dict[int, list[dict[str, Any]]], set[int]]:
    results = {configuration["id"]: [] for configuration in CONFIGURATIONS}
    completed: set[int] = set()
    for path in sorted(spool.glob("match-*.json.gz")):
        try:
            with gzip.open(path, "rt", encoding="utf-8") as handle:
                match = json.load(handle)
        except (EOFError, json.JSONDecodeError):
            continue
        if match.get("status") != "completed":
            continue
        match_id = int(match["id"])
        variant_id = (match_id - 1) // games_per_variant
        if variant_id not in results:
            continue
        results[variant_id].append(portfolio.compact_results([match])[0])
        completed.add(match_id)
    return results, completed


def outcome_counts(matches: list[dict[str, Any]]) -> Counter[str]:
    return Counter(tournament.outcome(match) for match in matches)


def write_summary(path: Path, scores: dict[str, dict[str, Any]],
                  results: dict[int, list[dict[str, Any]]], metadata: dict[str, Any]) -> None:
    lines = [
        "# Maxima construction+birth all-map confirmation", "",
        f"- Games per variant: {metadata['games_per_variant']}",
        f"- Independent seeds per compatible map/format: {metadata['rounds']}",
        f"- Total completed: {sum(len(matches) for matches in results.values())}",
        f"- Cluster slots: {sum(count for _host, count in metadata['workers'])}",
        f"- Wall time: {metadata['wall_seconds']:.1f} seconds", "",
        "| Variant | W-L-D | Portfolio | Worst format | Objective |",
        "|---|---:|---:|---:|---:|",
    ]
    for configuration in CONFIGURATIONS:
        score = scores[configuration["name"]]
        counts = outcome_counts(results[configuration["id"]])
        lines.append(
            f"| {configuration['name']} | {counts['win']}-{counts['loss']}-{counts['draw']} | "
            f"{score['portfolio_score']:.1%} | {score['worst_format_score']:.1%} | "
            f"{score['objective']:.4f} |"
        )
    lines.extend(["", "## Per-format formal scores", "",
                  "| Variant | Duel | FFA3 | FFA4 | FFA5 | 2v2 |",
                  "|---|---:|---:|---:|---:|---:|"])
    for configuration in CONFIGURATIONS:
        formats = scores[configuration["name"]]["formats"]
        lines.append(
            f"| {configuration['name']} | {formats['duel']['score']:.1%} | "
            f"{formats['ffa3']['score']:.1%} | {formats['ffa4']['score']:.1%} | "
            f"{formats['ffa5']['score']:.1%} | {formats['2v2']['score']:.1%} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else root / args.binary
    output = args.output_dir or root / "tournament-results" / datetime.now(timezone.utc).strftime(
        "maxima-staffing-confirmation-%Y%m%dT%H%M%SZ"
    )
    if not output.is_absolute():
        output = root / output
    output.mkdir(parents=True, exist_ok=True)

    staffing.validate(binary, list(CONFIGURATIONS))
    available = portfolio.discover_maps(binary, root)
    maps = tournament.compatible_maps(available)
    base_schedule = portfolio.make_portfolio_schedule(maps, args.rounds, args.seed, args.max_steps)
    spool = output / "match-telemetry"
    prior, completed = load_spool(spool, len(base_schedule))
    workloads = []
    for configuration in CONFIGURATIONS:
        schedule = []
        for match in base_schedule:
            item = dict(match)
            item["id"] = configuration["id"] * len(base_schedule) + int(match["id"])
            item["variant"] = configuration["name"]
            item["telemetry"] = not args.no_telemetry
            if item["id"] not in completed:
                schedule.append(item)
        workloads.append((configuration["id"], schedule, configuration["overrides"]))

    scheduled = sum(len(schedule) for _key, schedule, _settings in workloads)
    slots = sum(count for _host, count in args.workers)
    print(
        f"Running 2 variants x {len(base_schedule)} paired all-map games; "
        f"resuming {len(completed)}, scheduling {scheduled} on {slots} slots...",
        flush=True,
    )
    started = time.monotonic()
    if scheduled:
        cluster = staffing.DirectSSHPortfolioCluster(
            binary, root, args.workers, args.remote_root, args.timeout, args.retries,
            True, tournament.make_spool_sink(spool),
        )
        fresh = cluster.run_many(workloads)
    else:
        fresh = {configuration["id"]: [] for configuration in CONFIGURATIONS}
    results = {
        configuration["id"]: sorted(
            prior[configuration["id"]] + fresh[configuration["id"]],
            key=lambda match: int(match["id"]),
        )
        for configuration in CONFIGURATIONS
    }
    elapsed = time.monotonic() - started
    scores = {
        configuration["name"]: portfolio.score_results(results[configuration["id"]])
        for configuration in CONFIGURATIONS
    }
    for configuration in CONFIGURATIONS:
        portfolio.print_score(configuration["name"], scores[configuration["name"]])

    metadata = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "binary": str(binary), "rounds": args.rounds, "seed": args.seed,
        "max_steps": args.max_steps, "timeout": args.timeout,
        "workers": args.workers, "wall_seconds": round(elapsed, 3),
        "telemetry": not args.no_telemetry, "games_per_variant": len(base_schedule),
        "maps_by_format": {name: [item["name"] for item in items] for name, items in maps.items()},
    }
    (output / "results.json").write_text(json.dumps({
        "metadata": metadata, "configurations": CONFIGURATIONS,
        "scores": scores,
        "matches": {
            configuration["name"]: portfolio.compact_results(results[configuration["id"]])
            for configuration in CONFIGURATIONS
        },
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps({
        "metadata": metadata, "configurations": CONFIGURATIONS,
        "base_schedule": base_schedule,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    counts = tournament.write_spooled_tables(output, spool)
    if counts["game_features"]:
        analysis.analyze([output], output)
    staffing.write_csv(output / "checkpoint_metrics.csv", staffing.checkpoint_rows(spool))
    write_summary(output / "summary.md", scores, results, metadata)
    print(f"Report: {output / 'summary.md'}", flush=True)
    return 0 if all(score["failed"] == 0 for score in scores.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
