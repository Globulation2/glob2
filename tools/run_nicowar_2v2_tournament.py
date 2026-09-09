#!/usr/bin/env python3
"""Run balanced shared-vision, independent-agent Nicowar 2v2 tournaments."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any

import run_nicowar_tournament as ffa
import nicowar_tournament_signal as scoring
import analyze_nicowar_tournament as tournament_analysis
import crash_capture


MATCH_PREFIX = "NICOWAR_2V2_MATCH_RESULT\t"
PLAYER_PREFIX = "NICOWAR_2V2_PLAYER_RESULT\t"
AI_IDS = {
    "original": (5, "Nicowar"),
    "maxima": (7, "Maxima"),
}
DEFAULT_MATCHUPS = ("maxima-v-original",)


def matchup(value: str) -> tuple[str, str]:
    parts = value.casefold().split("-v-")
    if len(parts) != 2 or any(part not in AI_IDS for part in parts):
        raise argparse.ArgumentTypeError(
            "matchup must look like maxima-v-original; names are original and maxima"
        )
    return parts[0], parts[1]


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description=(
            "Run shared-vision allied 2v2 matches with separate AI instances. "
            "Each map/seed block tests all "
            "three two-seat partitions with A/B swapped, for six games per block."
        )
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=repo_root / "build-tournament" / "src" / "glob2",
    )
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--seed", type=int, help="Base unsigned 32-bit seed")
    parser.add_argument("--max-steps", type=int, default=180000)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument(
        "--matchup",
        action="append",
        type=matchup,
        help="Direct comparison such as maxima-v-original; repeat for multiple matchups",
    )
    parser.add_argument("--map", action="append", dest="map_filters")
    parser.add_argument("--all-maps", action="store_true")
    parser.add_argument("--telemetry", action="store_true")
    parser.add_argument(
        "--no-crash-capture",
        action="store_true",
        help="Disable core dumps and crash bundles (enabled by default)",
    )
    parser.add_argument(
        "--maxima-overrides",
        help=(
            "Comma-separated Maxima dotted key=value overrides. Each match receives them "
            "through GLOB2_MAXIMA_OVERRIDES."
        ),
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--resume-from", type=Path)
    parser.add_argument("--retry-failed-only", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.jobs < 1 or args.max_steps < 1 or args.timeout < 1:
        parser.error("rounds, jobs, max-steps, and timeout must be positive")
    if args.seed is not None and not 0 <= args.seed <= 0xFFFFFFFF:
        parser.error("--seed must fit in an unsigned 32-bit integer")
    if args.all_maps and args.map_filters:
        parser.error("--all-maps cannot be combined with --map")
    if args.retry_failed_only and args.resume_from is None:
        parser.error("--retry-failed-only requires --resume-from")
    if args.resume_from is not None and args.matchup:
        parser.error("--resume-from cannot be combined with --matchup")
    if not args.matchup:
        args.matchup = [matchup(item) for item in DEFAULT_MATCHUPS]
    return args


def matchup_slug(pair: tuple[str, str]) -> str:
    return f"{pair[0]}-v-{pair[1]}"


def make_schedule(
    rounds: int,
    maps: list[dict[str, str]],
    pairs: list[tuple[str, str]],
    base_seed: int,
    max_steps: int,
) -> list[dict[str, Any]]:
    schedule: list[dict[str, Any]] = []
    block = 0
    for round_number in range(1, rounds + 1):
        for map_index, map_info in enumerate(maps):
            seed_index = (round_number - 1) * len(maps) + map_index
            block_seed = ffa.derive_seed(base_seed, seed_index)
            for pair in pairs:
                block += 1
                ai_a, name_a = AI_IDS[pair[0]]
                ai_b, name_b = AI_IDS[pair[1]]
                for partition in range(3):
                    for swap in range(2):
                        schedule.append(
                            {
                                "id": len(schedule) + 1,
                                "block": block,
                                "round": round_number,
                                "map": map_info["name"],
                                "map_file": map_info["file"],
                                "seed": block_seed,
                                "matchup": matchup_slug(pair),
                                "ai_a": ai_a,
                                "ai_b": ai_b,
                                "version_a": name_a,
                                "version_b": name_b,
                                "partition": partition,
                                "swap": swap,
                                "max_steps": max_steps,
                            }
                        )
    return schedule


def parse_worker_output(match: dict[str, Any], output: str, returncode: int) -> dict[str, Any]:
    result = dict(match)
    result.update(
        {
            "status": "failed",
            "engine_status": "unknown",
            "steps": 0,
            "winner_side": "draw",
            "players": [],
            "telemetry": [],
            "observer": [],
            "score_telemetry": [],
        }
    )
    for line in output.splitlines():
        if line.startswith(MATCH_PREFIX):
            fields = line.split("\t")
            if len(fields) == 10:
                result.update(
                    {
                        "seed": int(fields[1]),
                        "map": fields[2],
                        "steps": int(fields[3]),
                        "partition": int(fields[4]),
                        "swap": int(fields[5]),
                        "engine_status": fields[6],
                        "ai_a": int(fields[7]),
                        "ai_b": int(fields[8]),
                        "winner_side": fields[9],
                    }
                )
        elif line.startswith(PLAYER_PREFIX):
            fields = line.split("\t")
            if len(fields) == 10:
                result["players"].append(
                    {
                        "side": fields[1],
                        "version": fields[2],
                        "team": int(fields[3]),
                        "won": bool(int(fields[4])),
                        "lost": bool(int(fields[5])),
                        "alive": bool(int(fields[6])),
                        "units": int(fields[7]),
                        "buildings": int(fields[8]),
                        "prestige": int(fields[9]),
                    }
                )
        elif any(line.startswith(prefix) for prefix in ffa.TELEMETRY_PREFIXES):
            fields = line.split("\t")
            if len(fields) >= 4:
                version = next(value for prefix, value in ffa.TELEMETRY_PREFIXES.items() if line.startswith(prefix))
                values = dict(field.split("=", 1) for field in fields[4:] if "=" in field)
                result["telemetry"].append({
                    "tick": int(fields[1]), "team": int(fields[2]),
                    "ai_version": version, "event": fields[3], **values,
                })
        elif line.startswith(ffa.OBSERVER_PREFIX):
            fields = line.split("\t")
            if len(fields) >= 5:
                values = dict(field.split("=", 1) for field in fields[4:] if "=" in field)
                result["observer"].append({
                    "tick": int(fields[1]), "team": int(fields[2]),
                    "version": fields[3], **values,
                })
        elif line.startswith(scoring.SCORE_PREFIX):
            snapshot = scoring.parse_score_line(line)
            if snapshot is not None:
                result["score_telemetry"].append(snapshot)
    if returncode == 0 and result["engine_status"] in ("completed", "timeout") and len(result["players"]) == 4:
        result["status"] = "completed"
    else:
        result["error"] = f"worker exited {returncode}; structured result was incomplete"
    result["returncode"] = returncode
    result["output"] = output
    if result["status"] == "completed":
        scoring.enrich_2v2_match(result)
    return result


def run_match(
    binary: Path,
    repo_root: Path,
    match_info: dict[str, Any],
    timeout: int,
    telemetry: bool,
    maxima_overrides: str | None = None,
    crash_root: Path | None = None,
) -> dict[str, Any]:
    command = [
        str(binary),
        "-nicowar-2v2-match-nox",
        match_info["map_file"],
        str(match_info["seed"]),
        str(match_info["ai_a"]),
        str(match_info["ai_b"]),
        str(match_info["partition"]),
        str(match_info["swap"]),
        str(match_info["max_steps"]),
    ]
    if telemetry:
        command.append("-nicowar-telemetry")
    environment = os.environ.copy()
    if maxima_overrides:
        environment["GLOB2_MAXIMA_OVERRIDES"] = maxima_overrides
    captured = crash_capture.run_process(
        command,
        cwd=repo_root,
        timeout=timeout,
        environment=environment,
        crash_root=crash_root,
        label=f"match-{match_info['id']:04d}",
    )
    if captured.timed_out:
        result = dict(match_info)
        result.update(
            {
                "status": "failed",
                "engine_status": "process_timeout",
                "steps": 0,
                "winner_side": "draw",
                "players": [],
                "telemetry": [],
                "observer": [],
                "score_telemetry": [],
                "returncode": None,
                "error": f"worker exceeded {timeout} seconds",
                "output": captured.output,
            }
        )
    elif captured.returncode is None:
        result = dict(match_info)
        result.update(
            {
                "status": "failed",
                "engine_status": "worker_error",
                "steps": 0,
                "winner_side": "draw",
                "players": [],
                "telemetry": [],
                "observer": [],
                "score_telemetry": [],
                "returncode": None,
                "error": captured.output,
                "output": captured.output,
            }
        )
    else:
        result = parse_worker_output(match_info, captured.output, captured.returncode)
    result.update(captured.result_fields())
    result["wall_seconds"] = captured.wall_seconds
    return result


def side_totals(result: dict[str, Any], side: str) -> dict[str, float]:
    players = [player for player in result["players"] if player["side"] == side]
    return {
        "units": sum(player["units"] for player in players),
        "buildings": sum(player["buildings"] for player in players),
        "prestige": sum(player["prestige"] for player in players),
        "alive": sum(player["alive"] for player in players),
    }


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = [result for result in results if result["status"] == "completed"]
    grouped: dict[str, list[dict[str, Any]]] = {}
    for result in completed:
        grouped.setdefault(result["matchup"], []).append(result)
    comparisons = []
    for slug, matches in sorted(grouped.items()):
        for match in matches:
            scoring.enrich_2v2_match(match)
        version_a = matches[0]["version_a"]
        version_b = matches[0]["version_b"]
        a_wins = sum(match["winner_side"] == "A" for match in matches)
        b_wins = sum(match["winner_side"] == "B" for match in matches)
        draws = len(matches) - a_wins - b_wins
        blocks: dict[int, list[dict[str, Any]]] = {}
        for match in matches:
            blocks.setdefault(match["block"], []).append(match)
        block_scores = []
        block_victory_scores = []
        positive = negative = tied = 0
        for block_matches in blocks.values():
            block_a = sum(match["winner_side"] == "A" for match in block_matches)
            block_b = sum(match["winner_side"] == "B" for match in block_matches)
            block_draws = len(block_matches) - block_a - block_b
            block_scores.append((block_a + 0.5 * block_draws) / len(block_matches))
            block_victory_scores.append(mean(float(match["victory_score_a"]) for match in block_matches))
            if block_a > block_b:
                positive += 1
            elif block_b > block_a:
                negative += 1
            else:
                tied += 1
        interval = ffa.mean_interval(block_scores, (0.0, 1.0))
        victory_interval = ffa.mean_interval(block_victory_scores, (-100.0, 100.0))
        totals_a = [side_totals(match, "A") for match in matches]
        totals_b = [side_totals(match, "B") for match in matches]
        maps: dict[str, dict[str, int]] = {}
        for match in matches:
            row = maps.setdefault(match["map"], {"games": 0, "a_wins": 0, "b_wins": 0, "draws": 0})
            row["games"] += 1
            if match["winner_side"] == "A":
                row["a_wins"] += 1
            elif match["winner_side"] == "B":
                row["b_wins"] += 1
            else:
                row["draws"] += 1
        comparisons.append(
            {
                "matchup": slug,
                "version_a": version_a,
                "version_b": version_b,
                "games": len(matches),
                "blocks": len(blocks),
                "a_wins": a_wins,
                "b_wins": b_wins,
                "draws": draws,
                "a_score": round((a_wins + 0.5 * draws) / len(matches), 4),
                "a_score_ci_low": interval["low"],
                "a_score_ci_high": interval["high"],
                "tournament_score_a": round(sum(float(match["victory_score_a"]) for match in matches), 3),
                "average_victory_score_a": round(mean(float(match["victory_score_a"]) for match in matches), 4),
                "victory_score_ci_low": victory_interval["low"],
                "victory_score_ci_high": victory_interval["high"],
                "blocks_favoring_a": positive,
                "blocks_favoring_b": negative,
                "blocks_tied": tied,
                "sign_test_p": ffa.exact_sign_test(positive, negative),
                "average_team_units_a": round(mean(row["units"] for row in totals_a), 2),
                "average_team_units_b": round(mean(row["units"] for row in totals_b), 2),
                "average_team_buildings_a": round(mean(row["buildings"] for row in totals_a), 2),
                "average_team_buildings_b": round(mean(row["buildings"] for row in totals_b), 2),
                "average_team_prestige_a": round(mean(row["prestige"] for row in totals_a), 2),
                "average_team_prestige_b": round(mean(row["prestige"] for row in totals_b), 2),
                "maps": maps,
            }
        )
    return {
        "matches_requested": len(results),
        "matches_completed": len(completed),
        "matches_failed": len(results) - len(completed),
        "decisive_matches": sum(result["winner_side"] in ("A", "B") for result in completed),
        "draws": sum(result["winner_side"] == "draw" for result in completed),
        "comparisons": comparisons,
    }


def percent(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1%}"


def write_reports(
    output_dir: Path,
    results: list[dict[str, Any]],
    summary: dict[str, Any],
    metadata: dict[str, Any],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    logs = output_dir / "logs"
    logs.mkdir(exist_ok=True)
    payload = {"metadata": metadata, "summary": summary, "matches": results}
    (output_dir / "results.json").write_text(json.dumps(payload, indent=2) + "\n")
    with (output_dir / "matches.csv").open("w", newline="") as handle:
        fields = [
            "id", "block", "round", "matchup", "map", "map_file", "seed",
            "partition", "swap", "steps", "engine_status", "status", "winner_side",
            "victory_score_a", "victory_score_b", "wall_seconds", "returncode", "process_pid",
            "termination_signal", "failure_kind", "crash_artifact", "error",
        ]
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for result in results:
            writer.writerow(result)
            (logs / f"match-{result['id']:04d}.log").write_text(result.get("output", ""))
    with (output_dir / "players.csv").open("w", newline="") as handle:
        fields = ["match", "matchup", "map", "seed", "partition", "swap", "side", "version", "team", "won", "lost", "alive", "units", "buildings", "prestige", "victory_score", "normalized_score", "economy_advantage", "military_advantage", "lead_changes", "dominance_volatility", "comeback", "signal_quality", "signal_version"]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for result in results:
            for player in result.get("players", []):
                writer.writerow(
                    {
                        "match": result["id"],
                        "matchup": result["matchup"],
                        "map": result["map"],
                        "seed": result["seed"],
                        "partition": result["partition"],
                        "swap": result["swap"],
                        **player,
                    }
                )

    scoring.write_raw_telemetry_tables(output_dir, results, "2v2")
    analysis_counts = scoring.write_analysis_tables(output_dir, results, "2v2")
    if analysis_counts["game_features"]:
        tournament_analysis.analyze([output_dir], output_dir)

    lines = [
        "# Shared-vision, independent-agent Nicowar 2v2 results",
        "",
        f"- Matches: {summary['matches_completed']} completed / {summary['matches_requested']} requested",
        f"- Failed workers: {summary['matches_failed']}",
        f"- Captured crash bundles: {sum(bool(match.get('crash')) for match in results)}",
        f"- Decisive matches: {summary['decisive_matches']}",
        f"- Draws or step timeouts: {summary['draws']}",
        f"- Protocol: {metadata['schedule_description']}",
        f"- Base seed: {metadata['base_seed']}",
        f"- Wall time: {metadata['wall_seconds']:.1f} seconds",
        "- Coordination: fixed alliances, normal allied vision, and joint victory; no shared resources, messages, AI state, targeting decisions, or assigned roles",
        "",
        f"- Compact score-trajectory snapshots: {analysis_counts['trajectories']}",
        "",
        "The primary standing is the sum of continuous victory scores. Decisive outcome anchors the sign; speed and time-integrated economy, military, resilience, and prestige dominance determine magnitude. The 95% interval uses independent map/seed blocks; the six seat-partition games within a block are not treated as independent.",
        "",
        "| Matchup | Tournament score A | Avg victory score (95% block CI) | Games | A wins | B wins | Draws | Match points A | Blocks A/B/tied | Team units A/B |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in summary["comparisons"]:
        lines.append(
            f"| {row['version_a']} vs {row['version_b']} | {row['tournament_score_a']:+.1f} | "
            f"{row['average_victory_score_a']:+.2f} ({row['victory_score_ci_low']:+.2f}–{row['victory_score_ci_high']:+.2f}) | {row['games']} | "
            f"{row['a_wins']} | {row['b_wins']} | {row['draws']} | "
            f"{percent(row['a_score'])} ({percent(row['a_score_ci_low'])}–{percent(row['a_score_ci_high'])}) | "
            f"{row['blocks_favoring_a']}/{row['blocks_favoring_b']}/{row['blocks_tied']} | "
            f"{row['average_team_units_a']:.1f}/{row['average_team_units_b']:.1f} |"
        )
    for row in summary["comparisons"]:
        lines.extend(
            [
                "",
                f"## {row['version_a']} vs {row['version_b']}",
                "",
                "| Map | Games | A wins | B wins | Draws |",
                "|---|---:|---:|---:|---:|",
            ]
        )
        for map_name, values in sorted(row["maps"].items()):
            lines.append(
                f"| {map_name} | {values['games']} | {values['a_wins']} | "
                f"{values['b_wins']} | {values['draws']} |"
            )
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else repo_root / args.binary
    timestamp = datetime.now(timezone.utc).strftime("2v2-%Y%m%dT%H%M%SZ")
    source_metadata = None
    carried: list[dict[str, Any]] = []
    if args.resume_from:
        source = args.resume_from / "results.json" if args.resume_from.is_dir() else args.resume_from
        payload = json.loads(source.read_text())
        source_metadata = payload["metadata"]
        prior = payload["matches"]
        if args.retry_failed_only:
            schedule = [match for match in prior if match["status"] != "completed"]
            carried = [match for match in prior if match["status"] == "completed"]
        else:
            schedule = prior
        output_dir = source.parent
        base_seed = int(source_metadata["base_seed"])
        maps = source_metadata["maps"]
        pairs = source_metadata["matchups"]
    else:
        maps = ffa.filter_maps(ffa.discover_maps(binary, repo_root), args.map_filters, args.all_maps)
        base_seed = args.seed if args.seed is not None else int(time.time()) & 0xFFFFFFFF
        pairs = args.matchup
        schedule = make_schedule(args.rounds, maps, pairs, base_seed, args.max_steps)
        output_dir = args.output_dir or repo_root / "tournament-results" / timestamp
    if not output_dir.is_absolute():
        output_dir = repo_root / output_dir
    crash_root = None if args.no_crash_capture else output_dir / "crashes"
    telemetry = args.telemetry or bool(source_metadata and source_metadata.get("telemetry"))
    maxima_overrides = args.maxima_overrides or (
        source_metadata.get("maxima_overrides") if source_metadata else None
    )
    print(
        f"Running {len(schedule)} shared-vision, independent-agent 2v2 matches with {args.jobs} workers; "
        f"{len(pairs)} matchup(s), {len(maps)} map(s), base seed {base_seed}.",
        flush=True,
    )
    started = time.monotonic()
    results = list(carried)
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(
                run_match,
                binary,
                repo_root,
                item,
                args.timeout,
                telemetry,
                maxima_overrides,
                crash_root,
            ): item
            for item in schedule
        }
        for completed, future in enumerate(as_completed(futures), 1):
            result = future.result()
            results.append(result)
            print(
                f"[{completed}/{len(schedule)}] {result['matchup']} {result['map']} "
                f"p{result['partition']} swap={result['swap']}: {result['status']} "
                f"({result['winner_side']})",
                flush=True,
            )
    wall = round(time.monotonic() - started, 3)
    results.sort(key=lambda item: item["id"])
    summary = build_summary(results)
    prior_wall = float(source_metadata.get("wall_seconds", 0.0)) if source_metadata else 0.0
    metadata = {
        "created_at": datetime.now(timezone.utc).isoformat(),
        "binary": str(binary),
        "base_seed": base_seed,
        "jobs": args.jobs,
        "max_steps": args.max_steps,
        "telemetry": telemetry,
        "maxima_overrides": maxima_overrides,
        "crash_capture": not args.no_crash_capture,
        "wall_seconds": round(prior_wall + wall, 3),
        "maps": maps,
        "matchups": pairs,
        "rounds": args.rounds if not source_metadata else source_metadata["rounds"],
        "schedule_description": (
            source_metadata["schedule_description"] if source_metadata else
            f"{args.rounds} round(s) × {len(maps)} map(s) × {len(pairs)} matchup(s) × 3 seat partitions × 2 side swaps"
        ),
        "score_signal": {
            "version": scoring.SIGNAL_VERSION, "range": [scoring.SCORE_MIN, scoring.SCORE_MAX],
            "outcome_weight": scoring.OUTCOME_WEIGHT, "speed_weight": scoring.SPEED_WEIGHT,
            "dominance_weight": scoring.DOMINANCE_WEIGHT,
        },
    }
    write_reports(output_dir, results, summary, metadata)
    print("\nDirect comparisons:")
    for row in summary["comparisons"]:
        print(
            f"  {row['version_a']} vs {row['version_b']}: "
            f"{row['a_wins']}-{row['b_wins']}-{row['draws']} "
            f"(victory score {row['tournament_score_a']:+.1f}; match points {row['a_score']:.1%})"
        )
    print(f"\nReports written to {output_dir}")
    return 1 if summary["matches_failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
