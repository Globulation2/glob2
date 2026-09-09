#!/usr/bin/env python3
"""Run Maxima versus Nicowar across every supported format and compatible map."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any

import analyze_nicowar_tournament as analysis
import nicowar_tournament_signal as signal
import optimize_maxima_portfolio as portfolio


DEFAULT_WORKERS = (
    ("local", 8),
    ("pharaoh-dev-1.local", 4),
    ("pharaoh-dev-2.local", 4),
    ("pharaoh-dev-3.local", 4),
    ("devlaptop.local", 16),
    ("therig.local", 32),
)


def worker(value: str) -> tuple[str, int]:
    return portfolio.parse_worker(value)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--rounds", type=int, default=3, help="Independent seeds per map/format")
    parser.add_argument("--seed", type=int, default=0x414C4C46)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--worker", action="append", type=worker, dest="workers")
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-telemetry", action="store_true")
    args = parser.parse_args()
    if min(args.rounds, args.max_steps, args.timeout) < 1 or args.retries < 0:
        parser.error("rounds, steps, and timeout must be positive; retries must be nonnegative")
    if not args.workers:
        args.workers = list(DEFAULT_WORKERS)
    return args


def compatible_maps(available: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {}
    for name, players in portfolio.PLAYER_COUNTS.items():
        result[name] = [item for item in available if item["teams"] >= players]
    result["2v2"] = [item for item in available if item["teams"] == 4]
    return result


def outcome(match: dict[str, Any]) -> str:
    if match.get("status") != "completed":
        return "failed"
    if match["kind"] == "2v2":
        return {"A": "win", "B": "loss"}.get(match.get("winner_side"), "draw")
    candidate = next(player for player in match["players"] if player["role"] == "candidate")
    if candidate["won"]:
        return "win"
    if candidate["lost"]:
        return "loss"
    return "draw"


def detailed_rows(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for match in results:
        formal, shaped, win_share = (0.0, 0.0, 0.0)
        if match.get("status") == "completed":
            score_fn = portfolio.team_match_score if match["kind"] == "2v2" else portfolio.scenario_match_score
            formal, shaped, win_share = score_fn(match)
        rows.append({
            "id": match["id"], "block": match["block"], "round": match["round"],
            "format": match["format"], "kind": match["kind"], "map": match["map"],
            "map_file": match["map_file"], "seed": match["seed"],
            "candidate_seat": match.get("candidate_seat", ""),
            "position_offset": match.get("position_offset", ""),
            "partition": match.get("partition", ""), "swap": match.get("swap", ""),
            "steps": match.get("steps", 0), "engine_status": match.get("engine_status", ""),
            "status": match.get("status", ""), "outcome": outcome(match),
            "formal_score": round(formal, 8), "normalized_score": round(shaped, 8),
            "win_share": round(win_share, 8), "worker_host": match.get("worker_host", ""),
            "wall_seconds": match.get("wall_seconds", 0), "attempt": match.get("attempt", 1),
            "error": match.get("error", ""),
        })
    return rows


def grouped_summary(rows: list[dict[str, Any]], dimensions: tuple[str, ...]) -> list[dict[str, Any]]:
    groups: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row[key] for key in dimensions)].append(row)
    summary = []
    for key, values in sorted(groups.items()):
        complete = [value for value in values if value["status"] == "completed"]
        counts = Counter(value["outcome"] for value in values)
        summary.append({
            **dict(zip(dimensions, key)), "games": len(values), "completed": len(complete),
            "wins": counts["win"], "losses": counts["loss"], "draws": counts["draw"],
            "failed": counts["failed"],
            "formal_score": round(mean(value["formal_score"] for value in complete), 8) if complete else "",
            "normalized_score": round(mean(value["normalized_score"] for value in complete), 8) if complete else "",
            "average_steps": round(mean(value["steps"] for value in complete), 2) if complete else "",
        })
    return summary


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fields = list(rows[0]) if rows else []
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        if fields:
            writer.writeheader()
            writer.writerows(rows)


def make_spool_sink(spool: Path):
    spool.mkdir(parents=True, exist_ok=True)

    def sink(result: dict[str, Any]) -> dict[str, Any]:
        full = dict(result)
        full.pop("output", None)
        if full.get("status") == "completed":
            score_fn = portfolio.team_match_score if full["kind"] == "2v2" else portfolio.scenario_match_score
            full["_portfolio_score"] = list(score_fn(full))
        with gzip.open(spool / f"match-{int(full['id']):05d}.json.gz", "wt", encoding="utf-8") as handle:
            json.dump(full, handle, separators=(",", ":"))
        compact = dict(full)
        compact.pop("telemetry", None)
        compact.pop("observer", None)
        compact.pop("score_telemetry", None)
        return compact

    return sink


def spooled_results(spool: Path):
    for path in sorted(spool.glob("match-*.json.gz")):
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            yield json.load(handle)


TABLES = {
    "telemetry.csv": ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "ai_version", "event"],
    "observer.csv": ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "version"],
    "trajectories.csv": ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "version"],
    "game_features.csv": ["match_id", "block", "round", "game_type", "map", "seed", "team", "version", "role", "victory_score"],
    "strategy_events.csv": ["match_id", "block", "round", "game_type", "map", "seed", "team", "version", "event", "count", "first_tick", "last_tick"],
}


def table_rows(match: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    context = {
        "match_id": match.get("id"), "block": match.get("block"),
        "round": match.get("round"), "game_type": match.get("format", "portfolio"),
        "map": match.get("map"), "seed": match.get("seed"),
    }
    if match.get("status") != "completed":
        return {name: [] for name in TABLES}
    trajectories = [{**context, **row} for row in signal.trajectory(match)[0]]
    return {
        "telemetry.csv": [{**context, **row} for row in match.get("telemetry", [])],
        "observer.csv": [{**context, **row} for row in match.get("observer", [])],
        "trajectories.csv": trajectories,
        "game_features.csv": signal.participant_feature_rows(match, str(match["format"])),
        "strategy_events.csv": signal.strategy_event_rows(match, str(match["format"])),
    }


def write_spooled_tables(output: Path, spool: Path) -> dict[str, int]:
    fields = {name: set(preferred) for name, preferred in TABLES.items()}
    counts = Counter()
    for match in spooled_results(spool):
        for name, rows in table_rows(match).items():
            counts[name] += len(rows)
            for row in rows:
                fields[name].update(row)
    handles = {}
    writers = {}
    try:
        for name, preferred in TABLES.items():
            ordered = preferred + sorted(fields[name] - set(preferred))
            handles[name] = (output / name).open("w", newline="", encoding="utf-8")
            writers[name] = csv.DictWriter(handles[name], fieldnames=ordered, extrasaction="ignore")
            writers[name].writeheader()
        for match in spooled_results(spool):
            for name, rows in table_rows(match).items():
                writers[name].writerows(rows)
    finally:
        for handle in handles.values():
            handle.close()
    return {
        "trajectories": counts["trajectories.csv"],
        "game_features": counts["game_features.csv"],
        "strategy_events": counts["strategy_events.csv"],
        "telemetry": counts["telemetry.csv"],
        "observer": counts["observer.csv"],
    }


def write_summary(output: Path, metadata: dict[str, Any], score: dict[str, Any],
                  formats: list[dict[str, Any]], maps: list[dict[str, Any]]) -> None:
    lines = [
        "# Maxima vs Nicowar: all-format, all-map tournament", "",
        f"- Games: {score['games'] - score['failed']} completed / {score['games']} scheduled",
        f"- Independent seeds per compatible map/format: {metadata['rounds']}",
        f"- Maps in the union: {metadata['map_count']}",
        f"- Formats: {', '.join(portfolio.FORMATS)}",
        f"- Hosts: {', '.join(host for host, _ in metadata['workers'])}",
        f"- Wall time: {metadata['wall_seconds']:.1f} seconds", "",
        f"- Equal-format portfolio score: {score['portfolio_score']:.1%}",
        f"- Worst-format score: {score['worst_format_score']:.1%}", "",
        "## Format results", "",
        "Formal score, confidence interval, and shaped score are block-weighted. Raw win share is shown separately because the FFA formal score gives partial credit for outlasting other losers.", "",
        "| Format | Games | W-L-D | Formal score (95% CI) | Raw win share | Shaped score | Avg steps |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in formats:
        official = score["formats"][row["format"]]
        lines.append(
            f"| {row['format']} | {row['completed']} | {row['wins']}-{row['losses']}-{row['draws']} | "
            f"{official['score']:.1%} ({official['ci95_low']:.1%}–{official['ci95_high']:.1%}) | "
            f"{official['win_share']:.1%} | {official['shaped']:+.3f} | {row['average_steps']:.0f} |"
        )
    lines.extend(["", "## Map and format results", "",
                  "| Format | Map | Games | W-L-D | Formal score | Normalized score |",
                  "|---|---|---:|---:|---:|---:|"])
    for row in maps:
        lines.append(
            f"| {row['format']} | {row['map']} | {row['completed']} | "
            f"{row['wins']}-{row['losses']}-{row['draws']} | {row['formal_score']:.1%} | "
            f"{row['normalized_score']:+.3f} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else root / args.binary
    output = args.output_dir or root / "tournament-results" / datetime.now(timezone.utc).strftime(
        "maxima-all-formats-%Y%m%dT%H%M%SZ"
    )
    if not output.is_absolute():
        output = root / output
    output.mkdir(parents=True, exist_ok=True)

    available = portfolio.discover_maps(binary, root)
    maps = compatible_maps(available)
    schedule = portfolio.make_portfolio_schedule(maps, args.rounds, args.seed, args.max_steps)
    for match in schedule:
        match["telemetry"] = not args.no_telemetry
    print("Schedule: " + " ".join(f"{name}={len(maps[name])} maps" for name in portfolio.FORMATS), flush=True)
    print(f"Running {len(schedule)} games over {args.rounds} seeds on {sum(n for _, n in args.workers)} slots...", flush=True)

    started = time.monotonic()
    spool = output / "match-telemetry"
    cluster = portfolio.PortfolioCluster(
        binary, root, args.workers, args.remote_root, args.timeout, args.retries, True,
        make_spool_sink(spool),
    )
    results = cluster.run(schedule, "")
    elapsed = time.monotonic() - started
    score = portfolio.score_results(results)
    rows = detailed_rows(results)
    format_rows = grouped_summary(rows, ("format",))
    map_rows = grouped_summary(rows, ("format", "map"))
    seed_rows = grouped_summary(rows, ("format", "map", "round", "seed"))
    host_rows = grouped_summary(rows, ("worker_host",))

    metadata = {
        "created_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
        "rounds": args.rounds, "base_seed": args.seed, "max_steps": args.max_steps,
        "timeout": args.timeout, "workers": args.workers, "wall_seconds": round(elapsed, 3),
        "telemetry": not args.no_telemetry, "map_count": len(available),
        "maps_by_format": {name: [item["name"] for item in items] for name, items in maps.items()},
    }
    write_csv(output / "matches.csv", rows)
    write_csv(output / "formats.csv", format_rows)
    write_csv(output / "maps.csv", map_rows)
    write_csv(output / "seeds.csv", seed_rows)
    write_csv(output / "hosts.csv", host_rows)
    counts = write_spooled_tables(output, spool)
    if counts["game_features"]:
        analysis.analyze([output], output)
    compact = portfolio.compact_results(results)
    (output / "results.json").write_text(json.dumps({
        "metadata": metadata, "score": score, "matches": compact,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps({
        "metadata": metadata, "schedule": schedule,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_summary(output, metadata, score, format_rows, map_rows)
    portfolio.print_score("Tournament", score)
    print(f"Telemetry events: {counts['telemetry']}", flush=True)
    print(f"Report: {output / 'summary.md'}", flush=True)
    return 0 if score["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
