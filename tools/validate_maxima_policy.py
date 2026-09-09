#!/usr/bin/env python3
"""Validate the compiled Maxima defaults against Original without 2v2."""

from __future__ import annotations

import argparse
import json
import os
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any

import optimize_maxima_portfolio as portfolio


NONTEAM_FORMATS = ("duel", "ffa3", "ffa4", "ffa5")


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--seed", type=int, default=0x56334654)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if min(args.rounds, args.max_steps, args.jobs, args.timeout) < 1:
        parser.error("rounds, max-steps, jobs, and timeout must be positive")
    if args.retries < 0:
        parser.error("retries must be nonnegative")
    return args


def map_scores(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    blocks: dict[tuple[str, int], list[tuple[float, float, float]]] = defaultdict(list)
    names: dict[tuple[str, int], str] = {}
    for match in results:
        key = (match["format"], match["block"])
        blocks[key].append(portfolio.scenario_match_score(match))
        names[key] = match["map"]
    grouped: dict[tuple[str, str], list[tuple[float, float, float]]] = defaultdict(list)
    for key, values in blocks.items():
        grouped[(key[0], names[key])].append(tuple(mean(v[i] for v in values) for i in range(3)))
    return [
        {
            "format": key[0], "map": key[1], "blocks": len(values),
            "score": round(mean(value[0] for value in values), 8),
            "shaped": round(mean(value[1] for value in values), 8),
            "win_share": round(mean(value[2] for value in values), 8),
        }
        for key, values in sorted(grouped.items())
    ]


def write_report(output_dir: Path, score: dict[str, Any], maps: list[dict[str, Any]]) -> None:
    lines = [
        "# Maxima terrain-policy validation", "",
        f"- Games: {score['games']}",
        "- Opponent: Original Nicowar",
        "- Formats: duel, FFA3, FFA4, FFA5 (equal format weight)",
        f"- Formal pairwise portfolio score: {score['portfolio_score']:.1%}",
        f"- Worst-format score: {score['worst_format_score']:.1%}",
        f"- Shaped objective: {score['objective']:.4f}", "",
        "## Format results", "",
        "| Format | Blocks | Formal score | 95% interval | Shaped | Win share |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for name in NONTEAM_FORMATS:
        value = score["formats"][name]
        lines.append(
            f"| {name} | {value['blocks']} | {value['score']:.1%} | "
            f"{value['ci95_low']:.1%}–{value['ci95_high']:.1%} | "
            f"{value['shaped']:.1%} | {value['win_share']:.1%} |"
        )
    lines.extend([
        "", "## Map results", "",
        "| Format | Map | Blocks | Formal score | Shaped | Win share |",
        "|---|---|---:|---:|---:|---:|",
    ])
    for value in maps:
        lines.append(
            f"| {value['format']} | {value['map']} | {value['blocks']} | "
            f"{value['score']:.1%} | {value['shaped']:.1%} | "
            f"{value['win_share']:.1%} |"
        )
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else root / args.binary
    output_dir = args.output_dir or root / "tournament-results" / datetime.now(
        timezone.utc
    ).strftime("maxima-terrain-validation-%Y%m%dT%H%M%SZ")
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    # Reuse the established portfolio scheduler and scorer, but intentionally
    # remove 2v2 from both. Empty tuning means the compiled Maxima defaults are the
    # candidate under test rather than an environment override.
    portfolio.FORMATS = NONTEAM_FORMATS
    available = portfolio.discover_maps(binary, root)
    map_sets = portfolio.resolve_map_sets(available)
    schedule = portfolio.make_portfolio_schedule(
        map_sets["train"], args.rounds, args.seed, args.max_steps
    )
    cluster = portfolio.PortfolioCluster(
        binary, root, [("local", args.jobs)], "", args.timeout, args.retries
    )
    print(
        f"Running {len(schedule)} games over {args.rounds} seeds "
        f"with {args.jobs} local workers...",
        flush=True,
    )
    results = cluster.run(schedule, "")
    score = portfolio.score_results(results)
    maps = map_scores(results) if score["failed"] == 0 else []
    payload = {
        "metadata": {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "binary": str(binary), "rounds": args.rounds,
            "max_steps": args.max_steps, "jobs": args.jobs,
            "seed": args.seed, "formats": list(NONTEAM_FORMATS),
            "opponent": "Original Nicowar", "tuning_override": "",
        },
        "score": score, "maps": maps,
    }
    (output_dir / "validation.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )
    (output_dir / "results.json").write_text(
        json.dumps(portfolio.compact_results(results), indent=2, sort_keys=True) + "\n"
    )
    write_report(output_dir, score, maps)
    portfolio.print_score("Validation", score)
    print(f"Report: {output_dir / 'summary.md'}", flush=True)
    return 0 if score["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
