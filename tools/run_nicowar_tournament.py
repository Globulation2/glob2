#!/usr/bin/env python3
"""Run parallel head-to-head tournaments between Maxima and Original Nicowar."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean, stdev
from typing import Any

import nicowar_tournament_signal as scoring
import analyze_nicowar_tournament as tournament_analysis
import crash_capture


MAP_PREFIX = "NICOWAR_TOURNAMENT_MAP\t"
MATCH_PREFIX = "NICOWAR_MATCH_RESULT\t"
PLAYER_PREFIX = "NICOWAR_PLAYER_RESULT\t"
TELEMETRY_PREFIXES = {
    "MAXIMA_TELEMETRY\t": "Maxima",
}
OBSERVER_PREFIX = "NICOWAR_OBSERVER_TELEMETRY\t"
VERSIONS = ("Nicowar", "Maxima")
DEFAULT_EXCLUDED_MAPS = {"balanced"}
PRACTICAL_EQUIVALENCE_MARGIN = 0.10
PRACTICAL_SCORE_EQUIVALENCE = 10.0
T_CRITICAL_95 = (
    0.0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262,
    2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093,
    2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045,
    2.042,
)


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    default_binary = repo_root / "build-tournament" / "src" / "glob2"
    default_jobs = min(4, os.cpu_count() or 1)
    parser = argparse.ArgumentParser(
        description=(
            "Run isolated headless Globulation 2 matches in parallel. A round tests "
            "every selected map with a fresh seed and four starting-position "
            "rotations. The four rotations are one independent map/seed block."
        )
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=default_binary,
        help="Globulation 2 executable (default: optimized build-tournament/src/glob2)",
    )
    schedule_group = parser.add_mutually_exclusive_group()
    schedule_group.add_argument(
        "--rounds",
        type=int,
        help=(
            "Complete rounds across all selected maps; each round is four rotations "
            "per map (default: 4, or 96 matches with the six signal maps)"
        ),
    )
    schedule_group.add_argument(
        "--games",
        type=int,
        help="Legacy fixed match count; must be a multiple of four",
    )
    parser.add_argument("--jobs", type=int, default=default_jobs, help=f"Parallel workers (default: {default_jobs})")
    parser.add_argument("--seed", type=int, help="Base seed (default: current Unix time)")
    parser.add_argument("--max-steps", type=int, default=180000, help="Maximum simulation ticks per match (default: 180000)")
    parser.add_argument("--timeout", type=int, default=600, help="Wall-clock timeout per worker in seconds (default: 600)")
    parser.add_argument(
        "--telemetry",
        action="store_true",
        help=(
            "Capture Maxima decisions in telemetry.csv and omniscient "
            "per-team snapshots in observer.csv"
        ),
    )
    parser.add_argument(
        "--no-crash-capture",
        action="store_true",
        help="Disable core dumps and crash bundles (enabled by default)",
    )
    parser.add_argument(
        "--maxima-overrides",
        help="Comma-separated canonical dotted Maxima overrides",
    )
    parser.add_argument(
        "--map",
        action="append",
        dest="map_filters",
        help="Restrict to a map path or display name; may be repeated",
    )
    parser.add_argument(
        "--all-maps",
        action="store_true",
        help="Include maps excluded from the signal suite for low decisiveness",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Report directory (default: tournament-results/<UTC timestamp>)",
    )
    parser.add_argument(
        "--resume-from",
        type=Path,
        help=(
            "Load a prior results.json (or its directory), rerun only simulation-step "
            "timeouts at --max-steps, and replace those matches in the reports"
        ),
    )
    parser.add_argument(
        "--report-only",
        action="store_true",
        help="With --resume-from, regenerate reports without running matches",
    )
    parser.add_argument(
        "--retry-failed-only",
        action="store_true",
        help=(
            "With --resume-from, rerun failed workers at the same simulation-step "
            "limit without extending legitimate simulation-step timeouts"
        ),
    )
    args = parser.parse_args()
    if args.resume_from is not None and (args.rounds is not None or args.games is not None):
        parser.error("--resume-from cannot be combined with --rounds or --games")
    if args.report_only and args.resume_from is None:
        parser.error("--report-only requires --resume-from")
    if args.retry_failed_only and args.resume_from is None:
        parser.error("--retry-failed-only requires --resume-from")
    if args.retry_failed_only and args.report_only:
        parser.error("--retry-failed-only cannot be combined with --report-only")
    if args.all_maps and args.map_filters:
        parser.error("--all-maps cannot be combined with --map")
    if args.resume_from is None and args.rounds is None and args.games is None:
        args.rounds = 4
    if args.rounds is not None and args.rounds < 1:
        parser.error("--rounds must be at least 1")
    if args.games is not None and args.games < 1:
        parser.error("--games must be at least 1")
    if args.games is not None and args.games % 4:
        parser.error("--games must be a multiple of 4 so every version gets every starting position")
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    if args.max_steps < 1:
        parser.error("--max-steps must be at least 1")
    if args.timeout < 1:
        parser.error("--timeout must be at least 1")
    if args.seed is not None and not 0 <= args.seed <= 0xFFFFFFFF:
        parser.error("--seed must fit in an unsigned 32-bit integer")
    return args


def discover_maps(binary: Path, repo_root: Path) -> list[dict[str, str]]:
    process = subprocess.run(
        [str(binary), "-list-nicowar-tournament-maps"],
        cwd=repo_root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60,
    )
    maps: list[dict[str, str]] = []
    for line in process.stdout.splitlines():
        if line.startswith(MAP_PREFIX):
            fields = line.split("\t", 2)
            if len(fields) == 3:
                maps.append({"file": fields[1], "name": fields[2]})
    if process.returncode != 0 or not maps:
        raise RuntimeError(
            "Could not discover four-player maps. Build the game first and run "
            f"'{binary} -list-nicowar-tournament-maps'.\n{process.stdout[-2000:]}"
        )
    return maps


def filter_maps(
    maps: list[dict[str, str]], filters: list[str] | None, include_all: bool = False
) -> list[dict[str, str]]:
    if not filters:
        if include_all:
            return maps
        return [item for item in maps if item["name"].casefold() not in DEFAULT_EXCLUDED_MAPS]
    requested = {item.casefold() for item in filters}
    selected = [
        item
        for item in maps
        if item["file"].casefold() in requested
        or item["name"].casefold() in requested
        or Path(item["file"]).name.casefold() in requested
    ]
    missing = requested - {
        value.casefold()
        for item in selected
        for value in (item["file"], item["name"], Path(item["file"]).name)
    }
    if missing:
        available = ", ".join(item["name"] for item in maps)
        raise RuntimeError(f"Unknown map filter(s): {', '.join(sorted(missing))}. Available: {available}")
    return selected


def derive_seed(base_seed: int, block_index: int) -> int:
    """Deterministically spread sequential block numbers over the 32-bit seed space."""
    value = (base_seed + (block_index + 1) * 0x9E3779B9) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x85EBCA6B) & 0xFFFFFFFF
    value ^= value >> 13
    value = (value * 0xC2B2AE35) & 0xFFFFFFFF
    value ^= value >> 16
    return value


def make_schedule(
    rounds: int | None,
    games: int | None,
    maps: list[dict[str, str]],
    base_seed: int,
    max_steps: int,
) -> list[dict[str, Any]]:
    if rounds is not None:
        block_count = rounds * len(maps)
    else:
        assert games is not None
        block_count = games // 4
    schedule: list[dict[str, Any]] = []
    for block_index in range(block_count):
        map_info = maps[block_index % len(maps)]
        round_number = block_index // len(maps) + 1
        block_seed = derive_seed(base_seed, block_index)
        for rotation in range(4):
            schedule.append(
                {
                    "id": len(schedule) + 1,
                    "block": block_index + 1,
                    "round": round_number,
                    "map_file": map_info["file"],
                    "map": map_info["name"],
                    "seed": block_seed,
                    "rotation": rotation,
                    "max_steps": max_steps,
                }
            )
    validate_schedule(schedule)
    return schedule


def validate_schedule(schedule: list[dict[str, Any]]) -> None:
    blocks: dict[int, list[dict[str, Any]]] = {}
    for match in schedule:
        blocks.setdefault(match["block"], []).append(match)
    seeds = []
    for block_id, matches in blocks.items():
        rotations = sorted(match["rotation"] for match in matches)
        if rotations != [0, 1, 2, 3]:
            raise ValueError(f"block {block_id} does not contain all four rotations")
        if len({match["map"] for match in matches}) != 1 or len({match["seed"] for match in matches}) != 1:
            raise ValueError(f"block {block_id} mixes maps or seeds")
        seeds.append(matches[0]["seed"])
    if len(seeds) != len(set(seeds)):
        raise ValueError("derived seeds unexpectedly collide between map/seed blocks")


def parse_worker_output(match: dict[str, Any], output: str, returncode: int) -> dict[str, Any]:
    result = dict(match)
    result.update(
        {
            "status": "failed",
            "engine_status": "unknown",
            "steps": 0,
            "players": [],
            "telemetry": [],
            "observer": [],
            "score_telemetry": [],
        }
    )
    for line in output.splitlines():
        if line.startswith(MATCH_PREFIX):
            fields = line.split("\t")
            if len(fields) == 6:
                result["seed"] = int(fields[1])
                result["map"] = fields[2]
                result["steps"] = int(fields[3])
                result["rotation"] = int(fields[4])
                result["engine_status"] = fields[5]
        elif line.startswith(PLAYER_PREFIX):
            fields = line.split("\t")
            if len(fields) == 9:
                result["players"].append(
                    {
                        "version": fields[1],
                        "team": int(fields[2]),
                        "won": bool(int(fields[3])),
                        "lost": bool(int(fields[4])),
                        "alive": bool(int(fields[5])),
                        "units": int(fields[6]),
                        "buildings": int(fields[7]),
                        "prestige": int(fields[8]),
                    }
                )
        elif any(line.startswith(prefix) for prefix in TELEMETRY_PREFIXES):
            fields = line.split("\t")
            if len(fields) >= 4:
                ai_version = next(
                    version
                    for prefix, version in TELEMETRY_PREFIXES.items()
                    if line.startswith(prefix)
                )
                values: dict[str, Any] = {}
                for field in fields[4:]:
                    if "=" in field:
                        key, value = field.split("=", 1)
                        values[key] = value
                result["telemetry"].append(
                    {
                        "tick": int(fields[1]),
                        "team": int(fields[2]),
                        "ai_version": ai_version,
                        "event": fields[3],
                        **values,
                    }
                )
        elif line.startswith(OBSERVER_PREFIX):
            fields = line.split("\t")
            if len(fields) >= 5:
                values: dict[str, Any] = {}
                for field in fields[4:]:
                    if "=" in field:
                        key, value = field.split("=", 1)
                        values[key] = value
                result["observer"].append(
                    {
                        "tick": int(fields[1]),
                        "team": int(fields[2]),
                        "version": fields[3],
                        **values,
                    }
                )
        elif line.startswith(scoring.SCORE_PREFIX):
            snapshot = scoring.parse_score_line(line)
            if snapshot is not None:
                result["score_telemetry"].append(snapshot)
    if returncode == 0 and result["engine_status"] in ("completed", "timeout") and len(result["players"]) == 2:
        result["status"] = "completed"
    else:
        result["error"] = f"worker exited {returncode}; structured result was incomplete"
    result["winners"] = [player["version"] for player in result["players"] if player["won"]]
    result["returncode"] = returncode
    result["output"] = output
    return result


def run_match(
    binary: Path,
    repo_root: Path,
    match: dict[str, Any],
    timeout: int,
    telemetry: bool,
    maxima_overrides: str | None = None,
    crash_root: Path | None = None,
) -> dict[str, Any]:
    command = [
        str(binary),
        "-nicowar-tournament-match-nox",
        match["map_file"],
        str(match["seed"]),
        str(match["rotation"]),
        str(match["max_steps"]),
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
        label=f"match-{match['id']:04d}",
    )
    if captured.timed_out:
        result = dict(match)
        result.update(
            {
                "status": "failed",
                "engine_status": "process_timeout",
                "steps": 0,
                "players": [],
                "telemetry": [],
                "observer": [],
                "score_telemetry": [],
                "winners": [],
                "returncode": None,
                "error": f"worker exceeded {timeout} seconds",
                "output": captured.output,
            }
        )
    elif captured.returncode is None:
        result = dict(match)
        result.update(
            {
                "status": "failed",
                "engine_status": "worker_error",
                "steps": 0,
                "players": [],
                "telemetry": [],
                "observer": [],
                "score_telemetry": [],
                "winners": [],
                "returncode": None,
                "error": captured.output,
                "output": captured.output,
            }
        )
    else:
        result = parse_worker_output(match, captured.output, captured.returncode)
    result.update(captured.result_fields())
    result["wall_seconds"] = captured.wall_seconds
    return result


def mean_interval(values: list[float], clamp: tuple[float, float] | None = None) -> dict[str, Any]:
    """Student-t 95% interval for independent block-level observations."""
    if not values:
        return {"mean": None, "low": None, "high": None, "samples": 0}
    center = mean(values)
    if len(values) > 1:
        degrees_of_freedom = len(values) - 1
        if degrees_of_freedom <= 30:
            critical = T_CRITICAL_95[degrees_of_freedom]
        elif degrees_of_freedom <= 40:
            critical = 2.021
        elif degrees_of_freedom <= 60:
            critical = 2.000
        elif degrees_of_freedom <= 120:
            critical = 1.980
        else:
            critical = 1.960
        margin = critical * stdev(values) / math.sqrt(len(values))
    else:
        margin = 0.0
    low, high = center - margin, center + margin
    if clamp is not None:
        low, high = max(clamp[0], low), min(clamp[1], high)
    return {
        "mean": round(center, 4),
        "low": round(low, 4),
        "high": round(high, 4),
        "samples": len(values),
    }


def exact_sign_test(positive: int, negative: int) -> float | None:
    """Two-sided exact sign test, excluding tied blocks."""
    samples = positive + negative
    if not samples:
        return None
    tail = sum(math.comb(samples, index) for index in range(min(positive, negative) + 1))
    return round(min(1.0, 2.0 * tail / (2**samples)), 6)


def number(snapshot: dict[str, Any], key: str, default: float = 0.0) -> float:
    try:
        return float(snapshot.get(key, default))
    except (TypeError, ValueError):
        return default


def build_summary(results: list[dict[str, Any]]) -> dict[str, Any]:
    for match in results:
        if match.get("status") == "completed":
            match.setdefault(
                "winners",
                [player["version"] for player in match.get("players", []) if player.get("won")],
            )
            scoring.enrich_ffa_match(match)
    versions: dict[str, dict[str, Any]] = {
        version: {
            "games": 0,
            "wins": 0,
            "losses": 0,
            "draws": 0,
            "units": [],
            "buildings": [],
            "prestige": [],
            "final_population_share": [],
            "alive": [],
            "peak_population": [],
            "survival_fraction": [],
            "victory_scores": [],
        }
        for version in VERSIONS
    }
    map_stats: dict[str, dict[str, Any]] = {}
    teams: dict[int, dict[str, int]] = {
        team: {"games": 0, "wins": 0} for team in range(4)
    }
    observer_metrics: dict[tuple[int, str], dict[str, float]] = {}
    for match in results:
        denominator = max(1, match.get("steps", 0))
        for snapshot in match.get("observer", []):
            key = (match["id"], snapshot["version"])
            metrics = observer_metrics.setdefault(key, {"peak_population": 0.0, "last_alive_tick": 0.0})
            metrics["peak_population"] = max(metrics["peak_population"], number(snapshot, "population"))
            if number(snapshot, "alive") > 0:
                metrics["last_alive_tick"] = max(metrics["last_alive_tick"], number(snapshot, "tick"))
            metrics["survival_fraction"] = min(1.0, metrics["last_alive_tick"] / denominator)

    for match in results:
        if match["status"] != "completed":
            continue
        winners = set(match["winners"])
        current_map = map_stats.setdefault(
            match["map"],
            {"games": 0, "decisive": 0, "draws": 0,
             "wins": {version: 0 for version in VERSIONS},
             "scores": {version: 0.0 for version in VERSIONS}},
        )
        current_map["games"] += 1
        current_map["decisive"] += bool(winners)
        current_map["draws"] += not bool(winners)
        total_units = sum(player["units"] for player in match["players"])
        for player in match["players"]:
            team_stats = teams.setdefault(player["team"], {"games": 0, "wins": 0})
            team_stats["games"] += 1
            if player["won"]:
                team_stats["wins"] += 1
            stats = versions.setdefault(
                player["version"],
                {
                    "games": 0, "wins": 0, "losses": 0, "draws": 0,
                    "units": [], "buildings": [], "prestige": [],
                    "final_population_share": [], "alive": [],
                    "peak_population": [], "survival_fraction": [],
                    "victory_scores": [],
                },
            )
            stats["games"] += 1
            if player["version"] in winners:
                stats["wins"] += 1
                current_map["wins"][player["version"]] = current_map["wins"].get(player["version"], 0) + 1
            elif winners:
                stats["losses"] += 1
            else:
                stats["draws"] += 1
            stats["units"].append(player["units"])
            stats["buildings"].append(player["buildings"])
            stats["prestige"].append(player["prestige"])
            stats["victory_scores"].append(float(player.get("victory_score", 0.0)))
            current_map["scores"][player["version"]] = round(
                current_map["scores"].get(player["version"], 0.0)
                + float(player.get("victory_score", 0.0)), 4
            )
            stats["final_population_share"].append(
                player["units"] / total_units if total_units else 1.0 / len(match["players"])
            )
            stats["alive"].append(float(player["alive"]))
            observed = observer_metrics.get((match["id"], player["version"]))
            if observed:
                stats["peak_population"].append(observed["peak_population"])
                stats["survival_fraction"].append(observed["survival_fraction"])

    grouped_matches: dict[int, list[dict[str, Any]]] = {}
    for match in results:
        grouped_matches.setdefault(match["block"], []).append(match)
    block_rows = []
    for block_id, matches in sorted(grouped_matches.items()):
        if len(matches) != 4 or any(match["status"] != "completed" for match in matches):
            continue
        if sorted(match["rotation"] for match in matches) != [0, 1, 2, 3]:
            continue
        per_version: dict[str, dict[str, list[float]]] = {
            version: {"wins": [], "victory_scores": [], "final_population_share": [], "peak_population": [], "survival_fraction": []}
            for version in VERSIONS
        }
        for match in matches:
            total_units = sum(player["units"] for player in match["players"])
            for player in match["players"]:
                metrics = per_version[player["version"]]
                metrics["wins"].append(float(player["won"]))
                metrics["victory_scores"].append(float(player.get("victory_score", 0.0)))
                metrics["final_population_share"].append(
                    player["units"] / total_units if total_units else 1.0 / len(match["players"])
                )
                observed = observer_metrics.get((match["id"], player["version"]))
                if observed:
                    metrics["peak_population"].append(observed["peak_population"])
                    metrics["survival_fraction"].append(observed["survival_fraction"])
        first = matches[0]
        block_rows.append(
            {
                "block": block_id,
                "round": first["round"],
                "map": first["map"],
                "seed": first["seed"],
                "games": 4,
                "decisive": sum(bool(match["winners"]) for match in matches),
                "draws": sum(not match["winners"] for match in matches),
                "versions": {
                    version: {
                        "wins": int(sum(values["wins"])),
                        "win_rate": mean(values["wins"]),
                        "victory_score": mean(values["victory_scores"]),
                        "final_population_share": mean(values["final_population_share"]),
                        "peak_population": mean(values["peak_population"]) if values["peak_population"] else None,
                        "survival_fraction": mean(values["survival_fraction"]) if values["survival_fraction"] else None,
                    }
                    for version, values in per_version.items()
                },
            }
        )

    version_rows = []
    for name, stats in versions.items():
        games = stats["games"]
        version_blocks = [row["versions"][name] for row in block_rows]
        win_interval = mean_interval([row["win_rate"] for row in version_blocks], (0.0, 1.0))
        score_interval = mean_interval([row["victory_score"] for row in version_blocks], (-100.0, 100.0))
        version_rows.append(
            {
                "version": name,
                "games": games,
                "wins": stats["wins"],
                "losses": stats["losses"],
                "draws": stats["draws"],
                "win_rate": round(stats["wins"] / games, 4) if games else 0.0,
                "win_rate_ci_low": win_interval["low"],
                "win_rate_ci_high": win_interval["high"],
                "independent_blocks": win_interval["samples"],
                "tournament_score": round(sum(stats["victory_scores"]), 3),
                "average_victory_score": round(mean(stats["victory_scores"]), 4) if stats["victory_scores"] else 0.0,
                "victory_score_ci_low": score_interval["low"],
                "victory_score_ci_high": score_interval["high"],
                "average_units": round(mean(stats["units"]), 2) if stats["units"] else 0.0,
                "average_buildings": round(mean(stats["buildings"]), 2) if stats["buildings"] else 0.0,
                "average_prestige": round(mean(stats["prestige"]), 2) if stats["prestige"] else 0.0,
                "average_final_population_share": round(mean(stats["final_population_share"]), 4) if stats["final_population_share"] else 0.0,
                "alive_rate": round(mean(stats["alive"]), 4) if stats["alive"] else 0.0,
                "average_peak_population": round(mean(stats["peak_population"]), 2) if stats["peak_population"] else None,
                "average_survival_fraction": round(mean(stats["survival_fraction"]), 4) if stats["survival_fraction"] else None,
            }
        )
    version_rows.sort(key=lambda row: (-row["tournament_score"], -row["wins"], row["version"]))
    team_rows = [
        {
            "team": team,
            "games": stats["games"],
            "wins": stats["wins"],
            "win_rate": round(stats["wins"] / stats["games"], 4) if stats["games"] else 0.0,
        }
        for team, stats in sorted(teams.items())
    ]

    maxima_comparisons = []
    for opponent in ("Nicowar",):
        win_values = [
            row["versions"]["Maxima"]["win_rate"] - row["versions"][opponent]["win_rate"]
            for row in block_rows
        ]
        share_values = [
            row["versions"]["Maxima"]["final_population_share"]
            - row["versions"][opponent]["final_population_share"]
            for row in block_rows
        ]
        score_values = [
            row["versions"]["Maxima"]["victory_score"]
            - row["versions"][opponent]["victory_score"]
            for row in block_rows
        ]
        win_ci = mean_interval(win_values)
        share_ci = mean_interval(share_values)
        score_ci = mean_interval(score_values)
        if len(block_rows) < 20:
            verdict = "INCONCLUSIVE"
        elif score_ci["low"] > 0:
            verdict = "Maxima AHEAD"
        elif score_ci["high"] < 0:
            verdict = "Maxima BEHIND"
        elif share_ci["low"] > 0:
            verdict = "Maxima STRONGER NON-WIN PERFORMANCE"
        elif share_ci["high"] < 0:
            verdict = "Maxima WEAKER NON-WIN PERFORMANCE"
        elif all(
            interval["low"] >= -PRACTICAL_EQUIVALENCE_MARGIN
            and interval["high"] <= PRACTICAL_EQUIVALENCE_MARGIN
            for interval in (win_ci, share_ci)
        ):
            verdict = "PRACTICALLY TIED"
        else:
            verdict = "INCONCLUSIVE"
        maxima_comparisons.append(
            {
                "opponent": opponent,
                "verdict": verdict,
                "independent_blocks": len(block_rows),
                "paired_win_rate_difference": win_ci,
                "paired_final_population_share_difference": share_ci,
                "paired_victory_score_difference": score_ci,
            }
        )

    round_rows = []
    for round_number in sorted({row["round"] for row in block_rows}):
        selected = [row for row in block_rows if row["round"] == round_number]
        round_rows.append(
            {
                "round": round_number,
                "blocks": len(selected),
                "games": sum(row["games"] for row in selected),
                "decisive": sum(row["decisive"] for row in selected),
                "draws": sum(row["draws"] for row in selected),
                "wins": {
                    version: sum(row["versions"][version]["wins"] for row in selected)
                    for version in VERSIONS
                },
                "scores": {
                    version: round(sum(row["versions"][version]["victory_score"] * row["games"] for row in selected), 3)
                    for version in VERSIONS
                },
            }
        )
    for stats in map_stats.values():
        stats["decisive_rate"] = round(stats["decisive"] / stats["games"], 4) if stats["games"] else 0.0
    return {
        "matches_requested": len(results),
        "matches_completed": sum(match["status"] == "completed" for match in results),
        "matches_failed": sum(match["status"] != "completed" for match in results),
        "independent_blocks_requested": len(grouped_matches),
        "independent_blocks_completed": len(block_rows),
        "decisive_matches": sum(bool(match["winners"]) for match in results if match["status"] == "completed"),
        "draws_or_step_timeouts": sum(not match["winners"] for match in results if match["status"] == "completed"),
        "versions": version_rows,
        "maps": map_stats,
        "low_signal_maps": sorted(
            map_name for map_name, stats in map_stats.items() if stats["decisive_rate"] < 0.5
        ),
        "rounds": round_rows,
        "blocks": block_rows,
        "starting_teams": team_rows,
        "maxima_comparisons": maxima_comparisons,
        "score_version": scoring.SIGNAL_VERSION,
    }


def format_percent_interval(interval: dict[str, Any]) -> str:
    if interval["mean"] is None:
        return "n/a"
    return f"{interval['mean']:.1%} ({interval['low']:.1%} to {interval['high']:.1%})"


def format_score_interval(interval: dict[str, Any]) -> str:
    if interval["mean"] is None:
        return "n/a"
    return f"{interval['mean']:+.2f} ({interval['low']:+.2f} to {interval['high']:+.2f})"


def write_reports(
    output_dir: Path,
    results: list[dict[str, Any]],
    summary: dict[str, Any],
    metadata: dict[str, Any],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    logs_dir = output_dir / "logs"
    logs_dir.mkdir(exist_ok=True)
    for match in results:
        output = match.pop("output", None)
        if output is not None:
            (logs_dir / f"match-{match['id']:04d}.log").write_text(output, encoding="utf-8")

    with (output_dir / "matches.csv").open("w", newline="", encoding="utf-8") as handle:
        fields = [
            "id", "block", "round", "map", "map_file", "seed", "rotation", "max_steps", "steps",
            "engine_status", "status", "winners", "wall_seconds", "returncode", "process_pid",
            "termination_signal", "failure_kind", "crash_artifact", "error",
        ]
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for match in results:
            row = dict(match)
            row["winners"] = ", ".join(match["winners"])
            writer.writerow(row)

    with (output_dir / "players.csv").open("w", newline="", encoding="utf-8") as handle:
        fields = [
            "match_id", "block", "round", "map", "seed", "rotation", "engine_status", "version", "team",
            "won", "lost", "alive", "units", "buildings", "prestige",
            "victory_score", "normalized_score", "outcome_component", "speed_component",
            "dominance_component", "economy_advantage", "military_advantage",
            "resilience_advantage", "prestige_advantage", "duration_fraction",
            "lead_changes", "dominance_volatility", "comeback", "signal_quality", "signal_version",
        ]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for match in results:
            for player in match["players"]:
                writer.writerow(
                    {
                        "match_id": match["id"],
                        "block": match["block"],
                        "round": match["round"],
                        "map": match["map"],
                        "seed": match["seed"],
                        "rotation": match["rotation"],
                        "engine_status": match["engine_status"],
                        **player,
                    }
                )

    with (output_dir / "blocks.csv").open("w", newline="", encoding="utf-8") as handle:
        fields = [
            "block", "round", "map", "seed", "games", "decisive", "draws", "version",
            "wins", "win_rate", "victory_score", "final_population_share", "peak_population", "survival_fraction",
        ]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for block in summary["blocks"]:
            for version, values in block["versions"].items():
                writer.writerow({key: block[key] for key in fields if key in block} | {"version": version, **values})

    telemetry_rows = []
    for match in results:
        for event in match.get("telemetry", []):
            telemetry_rows.append(
                {
                    "match_id": match["id"],
                    "block": match["block"],
                    "round": match["round"],
                    "map": match["map"],
                    "seed": match["seed"],
                    "rotation": match["rotation"],
                    **event,
                }
            )
    telemetry_base_fields = [
        "match_id", "block", "round", "map", "seed", "rotation", "tick", "team", "ai_version", "event"
    ]
    telemetry_extra_fields = sorted(
        {key for row in telemetry_rows for key in row if key not in telemetry_base_fields}
    )
    with (output_dir / "telemetry.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=telemetry_base_fields + telemetry_extra_fields)
        writer.writeheader()
        writer.writerows(telemetry_rows)

    observer_rows = []
    for match in results:
        for snapshot in match.get("observer", []):
            observer_rows.append(
                {
                    "match_id": match["id"],
                    "block": match["block"],
                    "round": match["round"],
                    "map": match["map"],
                    "seed": match["seed"],
                    "rotation": match["rotation"],
                    **snapshot,
                }
            )
    observer_base_fields = ["match_id", "block", "round", "map", "seed", "rotation", "tick", "team", "version"]
    observer_extra_fields = sorted(
        {key for row in observer_rows for key in row if key not in observer_base_fields}
    )
    with (output_dir / "observer.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=observer_base_fields + observer_extra_fields)
        writer.writeheader()
        writer.writerows(observer_rows)

    analysis_counts = scoring.write_analysis_tables(output_dir, results, "duel")
    if analysis_counts["game_features"]:
        tournament_analysis.analyze([output_dir], output_dir)

    payload = {"metadata": metadata, "summary": summary, "matches": results}
    (output_dir / "results.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Nicowar tournament results",
        "",
        f"- Matches: {summary['matches_completed']} completed / {summary['matches_requested']} requested",
        f"- Independent map/seed blocks: {summary['independent_blocks_completed']} completed / {summary['independent_blocks_requested']} requested",
        f"- Protocol: {metadata['schedule_description']}",
        f"- Failed workers: {summary['matches_failed']}",
        f"- Captured crash bundles: {sum(bool(match.get('crash')) for match in results)}",
        f"- Decisive matches: {summary['decisive_matches']}",
        f"- Draws or simulation-step timeouts: {summary['draws_or_step_timeouts']}",
        f"- Base seed: {metadata['base_seed']}",
        f"- Parallel jobs: {metadata['jobs']}",
        f"- Tournament wall time: {metadata['wall_seconds']:.1f} seconds",
        f"- Maxima telemetry events: {sum(len(match.get('telemetry', [])) for match in results)}",
        f"- Omniscient observer snapshots: {sum(len(match.get('observer', [])) for match in results)}",
        f"- Analysis trajectory rows: {analysis_counts['trajectories']}",
        "",
        "The primary standing is the sum of continuous victory scores. A decisive result anchors the sign; speed and time-integrated economy, military, resilience, and prestige dominance set its magnitude. The 95% intervals treat each four-rotation map/seed block as one independent sample.",
        "",
        "| Version | Tournament score | Avg score (95% block CI) | Games | W-L-D | Win rate | Final pop share | Alive | Avg peak pop | Avg survival |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in summary["versions"]:
        interval = (
            f"{row['win_rate']:.1%} ({row['win_rate_ci_low']:.1%}–{row['win_rate_ci_high']:.1%})"
            if row["win_rate_ci_low"] is not None else f"{row['win_rate']:.1%}"
        )
        peak = f"{row['average_peak_population']:.1f}" if row["average_peak_population"] is not None else "—"
        survival = f"{row['average_survival_fraction']:.1%}" if row["average_survival_fraction"] is not None else "—"
        score_interval = (
            f"{row['victory_score_ci_low']:+.2f}–{row['victory_score_ci_high']:+.2f}"
            if row["victory_score_ci_low"] is not None else "n/a"
        )
        lines.append(
            f"| {row['version']} | {row['tournament_score']:+.1f} | {row['average_victory_score']:+.2f} "
            f"({score_interval}) | "
            f"{row['games']} | {row['wins']}-{row['losses']}-{row['draws']} | {interval} | {row['average_final_population_share']:.1%} | "
            f"{row['alive_rate']:.1%} | {peak} | {survival} |"
        )
    lines.extend([
        "",
        "## Paired Maxima comparisons",
        "",
        "Each difference is Maxima minus its opponent, paired within the same map/seed block.",
        "",
        "| Opponent | Verdict | Victory-score difference (95% CI) | Win-rate difference (95% CI) | Final-pop-share difference (95% CI) |",
        "|---|---|---:|---:|---:|",
    ])
    for comparison in summary["maxima_comparisons"]:
        lines.append(
            f"| {comparison['opponent']} | {comparison['verdict']} | "
            f"{format_score_interval(comparison['paired_victory_score_difference'])} | "
            f"{format_percent_interval(comparison['paired_win_rate_difference'])} | "
            f"{format_percent_interval(comparison['paired_final_population_share_difference'])} |"
        )
    lines.extend([
        "",
        "## Results by round",
        "",
        "| Round | Blocks | Games | Decisive | Draws | " + " | ".join(VERSIONS) + " |",
        "|---:|---:|---:|---:|---:|" + "---:|" * len(VERSIONS),
    ])
    for row in summary["rounds"]:
        lines.append(
            f"| {row['round']} | {row['blocks']} | {row['games']} | {row['decisive']} | {row['draws']} | "
            + " | ".join(str(row["wins"][version]) for version in VERSIONS) + " |"
        )
    lines.extend(["", "## Results by map", ""])
    lines.append("| Map | Games | Decisive | Rate | Draws | " + " | ".join(VERSIONS) + " |")
    lines.append("|---|---:|---:|---:|---:|" + "---:|" * len(VERSIONS))
    for map_name, stats in sorted(summary["maps"].items()):
        lines.append(
            f"| {map_name} | {stats['games']} | {stats['decisive']} | {stats['decisive_rate']:.1%} | {stats['draws']} | "
            + " | ".join(str(stats["wins"].get(version, 0)) for version in VERSIONS) + " |"
        )
    if summary["low_signal_maps"]:
        lines.extend([
            "",
            "Low-signal maps (<50% decisive): " + ", ".join(summary["low_signal_maps"]) + ".",
        ])
    lines.extend(["", "## Continuous score by map", ""])
    lines.append("| Map | " + " | ".join(VERSIONS) + " |")
    lines.append("|---|" + "---:|" * len(VERSIONS))
    for map_name, stats in sorted(summary["maps"].items()):
        lines.append(
            f"| {map_name} | "
            + " | ".join(f"{stats['scores'].get(version, 0.0):+.1f}" for version in VERSIONS)
            + " |"
        )
    lines.extend([
        "",
        "## Wins by starting team",
        "",
        "This table exposes map or engine starting-position bias that version rotation can hide.",
        "",
        "| Starting team | Games | Wins | Win rate |",
        "|---:|---:|---:|---:|",
    ])
    for row in summary["starting_teams"]:
        lines.append(
            f"| {row['team']} | {row['games']} | {row['wins']} | {row['win_rate']:.1%} |"
        )
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else (repo_root / args.binary)
    binary = binary.resolve()
    if not binary.is_file():
        print(f"Tournament binary not found: {binary}", file=sys.stderr)
        print(
            "Build it with: scons --build=build-tournament release=1 -j4 "
            "build-tournament/src/glob2",
            file=sys.stderr,
        )
        return 2

    carried_results: list[dict[str, Any]] = []
    source_metadata: dict[str, Any] | None = None
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    if args.resume_from is not None:
        source_path = args.resume_from if args.resume_from.is_absolute() else repo_root / args.resume_from
        if source_path.is_dir():
            source_path = source_path / "results.json"
        try:
            source_payload = json.loads(source_path.read_text(encoding="utf-8"))
            source_results = source_payload["matches"]
            source_metadata = source_payload["metadata"]
        except (OSError, ValueError, KeyError, TypeError) as error:
            print(f"Could not load resume results from {source_path}: {error}", file=sys.stderr)
            return 2
        prior_limit = max((match.get("max_steps", 0) for match in source_results), default=0)
        if not args.report_only and not args.retry_failed_only and args.max_steps <= prior_limit:
            print(
                f"--max-steps must exceed the prior limit of {prior_limit} when resuming",
                file=sys.stderr,
            )
            return 2
        if args.report_only:
            retry_ids = set()
        elif args.retry_failed_only:
            retry_ids = {
                match["id"] for match in source_results
                if match.get("status") != "completed"
            }
        else:
            retry_ids = {
                match["id"]
                for match in source_results
                if match.get("status") == "completed"
                and match.get("engine_status") == "timeout"
            }
        match_fields = ("id", "block", "round", "map_file", "map", "seed", "rotation")
        schedule = [
            {**{key: match[key] for key in match_fields}, "max_steps": args.max_steps}
            for match in source_results
            if match["id"] in retry_ids
        ]
        carried_results = [match for match in source_results if match["id"] not in retry_ids]
        base_seed = source_metadata["base_seed"]
        maps = source_metadata["maps"]
        output_dir = args.output_dir or source_path.parent
        if args.report_only:
            print(f"Regenerating reports for {len(carried_results)} stored match(es).", flush=True)
        elif args.retry_failed_only:
            print(
                f"Retrying {len(schedule)} failed worker(s) at {args.max_steps} ticks "
                f"with {args.jobs} workers; preserving {len(carried_results)} "
                f"completed match(es).",
                flush=True,
            )
        else:
            print(
                f"Extending {len(schedule)} step-timeout match(es) from {prior_limit} to "
                f"{args.max_steps} ticks with {args.jobs} workers; preserving "
                f"{len(carried_results)} completed match(es).",
                flush=True,
            )
    else:
        try:
            maps = filter_maps(discover_maps(binary, repo_root), args.map_filters, args.all_maps)
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(error, file=sys.stderr)
            return 2
        base_seed = args.seed if args.seed is not None else int(time.time()) & 0xFFFFFFFF
        try:
            schedule = make_schedule(args.rounds, args.games, maps, base_seed, args.max_steps)
        except ValueError as error:
            print(f"Invalid tournament schedule: {error}", file=sys.stderr)
            return 2
        output_dir = args.output_dir or (repo_root / "tournament-results" / timestamp)
        print(
            f"Running {len(schedule)} matches with {args.jobs} workers across "
            f"{len(maps)} four-player map(s) and {len(schedule) // 4} independent "
            f"map/seed block(s); base seed {base_seed}.",
            flush=True,
        )
    if not output_dir.is_absolute():
        output_dir = repo_root / output_dir

    crash_root = None if args.no_crash_capture else output_dir / "crashes"

    capture_telemetry = args.telemetry or bool(source_metadata and source_metadata.get("telemetry"))
    maxima_overrides = args.maxima_overrides or (
        source_metadata.get("maxima_overrides") if source_metadata else None
    )
    tournament_started = time.monotonic()
    results: list[dict[str, Any]] = list(carried_results)
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(
                run_match, binary, repo_root, match, args.timeout,
                capture_telemetry, maxima_overrides, crash_root
            ): match
            for match in schedule
        }
        for completed, future in enumerate(as_completed(futures), 1):
            result = future.result()
            results.append(result)
            winners = ", ".join(result["winners"]) or "draw/timeout"
            print(
                f"[{completed}/{len(schedule)}] match {result['id']} "
                f"{result['map']} seed={result['seed']} rotation={result['rotation']}: "
                f"{result['status']} ({winners})",
                flush=True,
            )

    tournament_wall_seconds = round(time.monotonic() - tournament_started, 3)
    results.sort(key=lambda match: match["id"])
    summary = build_summary(results)
    if source_metadata is not None:
        metadata = dict(source_metadata)
        prior_wall_seconds = float(source_metadata.get("wall_seconds", 0.0))
        if args.report_only:
            metadata["reports_regenerated_at"] = datetime.now(timezone.utc).isoformat()
        else:
            metadata.update(
                {
                    "created_at": datetime.now(timezone.utc).isoformat(),
                    "binary": str(binary),
                    "jobs": args.jobs,
                    "initial_max_steps": source_metadata.get("initial_max_steps", source_metadata.get("max_steps")),
                    "max_steps": args.max_steps,
                    "telemetry": capture_telemetry,
                    "maxima_overrides": maxima_overrides,
                    "crash_capture": not args.no_crash_capture,
                    "overtime_wall_seconds": tournament_wall_seconds,
                    "wall_seconds": round(prior_wall_seconds + tournament_wall_seconds, 3),
                    "resumed_from": str(source_path),
                    "schedule_description": (
                        f"{source_metadata['schedule_description']}; step timeouts extended to "
                        f"{args.max_steps} ticks"
                    ),
                }
            )
    else:
        metadata = {
            "created_at": datetime.now(timezone.utc).isoformat(),
            "binary": str(binary),
            "base_seed": base_seed,
            "jobs": args.jobs,
            "max_steps": args.max_steps,
            "telemetry": capture_telemetry,
            "maxima_overrides": maxima_overrides,
            "crash_capture": not args.no_crash_capture,
            "wall_seconds": tournament_wall_seconds,
            "maps": maps,
            "included_low_signal_maps": args.all_maps or bool(args.map_filters),
            "rounds": args.rounds,
            "schedule_mode": "rounds" if args.rounds is not None else "fixed_games",
            "schedule_description": (
                f"{args.rounds} round(s) × {len(maps)} map(s) × 4 rotations"
                if args.rounds is not None
                else f"{len(schedule) // 4} map/seed block(s) × 4 rotations (legacy fixed-game mode)"
            ),
        }
    metadata["score_signal"] = {
        "version": scoring.SIGNAL_VERSION,
        "range": [scoring.SCORE_MIN, scoring.SCORE_MAX],
        "outcome_weight": scoring.OUTCOME_WEIGHT,
        "speed_weight": scoring.SPEED_WEIGHT,
        "dominance_weight": scoring.DOMINANCE_WEIGHT,
    }
    write_reports(output_dir, results, summary, metadata)

    print("\nStandings:")
    for row in summary["versions"]:
        print(
            f"  {row['version']:<10} score={row['tournament_score']:>+8.1f}  "
            f"{row['wins']:>3} wins / {row['games']:>3} games ({row['win_rate']:.1%})"
        )
    comparison = summary["maxima_comparisons"][0]
    print(
        f"\nMaxima vs Original: {comparison['verdict']}",
        flush=True,
    )
    print(f"\nTournament wall time: {tournament_wall_seconds:.1f} seconds")
    print(f"\nReports written to {output_dir}")
    return 1 if summary["matches_failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
