#!/usr/bin/env python3
"""Variant-aware paired analysis for the all-map Maxima staffing confirmation."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean, stdev
from typing import Any, Iterable


VARIANTS = ("baseline", "construction+birth")
FORMATS = ("duel", "ffa3", "ffa4", "ffa5", "2v2")
CHECKPOINT_FIELDS = (
    "assigned_worker_slots", "construction_assigned", "construction_working",
    "swarm_assigned", "swarm_working", "inn_assigned", "inn_working",
    "free_workers", "worker_jobs_open", "workers", "population", "buildings",
    "building_sites", "warriors", "military_working", "hungry",
)
FEATURE_FIELDS = (
    "early_workers", "mid_workers", "late_workers", "average_workers",
    "early_population", "mid_population", "late_population", "average_population",
    "early_buildings", "mid_buildings", "late_buildings",
    "early_warriors", "mid_warriors", "late_warriors",
    "early_attack_power", "mid_attack_power", "late_attack_power",
    "economy_advantage", "military_advantage", "normalized_score",
)
TACTICAL_EVENTS = (
    "mission_selected", "mission_phase_changed", "mission_finished",
    "attack_finished", "mission_retargeted", "explorer_strike_launched",
)
SYSTEM_EVENTS = (
    "director_snapshot", "farming_policy", "placement_planner",
    "recon_mission_created", "reactive_defense_updated",
    "preemptive_defense_updated", "land_clearing_started",
    "colony_swarm_selected", "colony_swarm_completed", "colony_swarm_failed",
)
RAW_EVENTS = (
    "director_snapshot", "mission_selected", "mission_phase_changed",
    "mission_finished", "attack_finished", "mission_retargeted",
    "explorer_strike_launched", "colony_swarm_selected",
    "colony_swarm_completed", "colony_swarm_failed",
)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "result_dir", nargs="?", type=Path,
        default=root / "tournament-results/maxima-staffing-confirmation-all-maps-5seeds-20260904",
    )
    parser.add_argument("--bootstrap", type=int, default=50000)
    return parser.parse_args()


def ci(values: Iterable[float]) -> dict[str, float | int]:
    samples = list(values)
    average = mean(samples) if samples else 0.0
    standard_error = stdev(samples) / math.sqrt(len(samples)) if len(samples) > 1 else 0.0
    return {
        "n": len(samples), "mean": average, "se": standard_error,
        "low": average - 1.96 * standard_error,
        "high": average + 1.96 * standard_error,
    }


def quantile(sorted_values: list[float], probability: float) -> float:
    if not sorted_values:
        return 0.0
    position = probability * (len(sorted_values) - 1)
    lower = int(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] * (1 - fraction) + sorted_values[upper] * fraction


def outcome(match: dict[str, Any]) -> str:
    if match["kind"] == "2v2":
        return {"A": "win", "B": "loss"}.get(match.get("winner_side"), "draw")
    candidate = next(player for player in match["players"] if player["role"] == "candidate")
    return "win" if candidate["won"] else "loss" if candidate["lost"] else "draw"


def paired_outcomes(payload: dict[str, Any], bootstrap_count: int) -> dict[str, Any]:
    games_per_variant = int(payload["metadata"]["games_per_variant"])
    matches = {
        variant: {int(match["id"]) - (games_per_variant if variant != "baseline" else 0): match
                  for match in payload["matches"][variant]}
        for variant in VARIANTS
    }
    if set(matches["baseline"]) != set(matches["construction+birth"]):
        raise RuntimeError("variant schedules do not contain identical normalized match ids")
    identity = ("block", "round", "format", "map_file", "seed", "kind",
                "candidate_seat", "position_offset")
    retries = {variant: Counter() for variant in VARIANTS}
    transitions = Counter()
    per_game: list[dict[str, Any]] = []
    for match_id in sorted(matches["baseline"]):
        base = matches["baseline"][match_id]
        candidate = matches["construction+birth"][match_id]
        if any(base.get(key) != candidate.get(key) for key in identity):
            raise RuntimeError(f"pair identity mismatch at normalized match {match_id}")
        retries["baseline"][int(base.get("attempt", 1))] += 1
        retries["construction+birth"][int(candidate.get("attempt", 1))] += 1
        transitions[(outcome(base), outcome(candidate))] += 1
        per_game.append({
            "id": match_id, "block": int(base["block"]), "round": int(base["round"]),
            "format": base["format"], "map": base["map"],
            "formal": float(candidate["_portfolio_score"][0]) - float(base["_portfolio_score"][0]),
            "shaped": float(candidate["_portfolio_score"][1]) - float(base["_portfolio_score"][1]),
            "steps": int(candidate["steps"]) - int(base["steps"]),
        })

    blocks: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in per_game:
        blocks[(row["format"], row["block"])].append(row)
    block_rows = []
    for (format_name, block), rows in sorted(blocks.items()):
        block_rows.append({
            "format": format_name, "block": block, "round": rows[0]["round"],
            "map": rows[0]["map"],
            "formal": mean(row["formal"] for row in rows),
            "shaped": mean(row["shaped"] for row in rows),
            "steps": mean(row["steps"] for row in rows),
            "games": len(rows),
        })
    format_effects = {
        format_name: {
            "formal": ci(row["formal"] for row in block_rows if row["format"] == format_name),
            "shaped": ci(row["shaped"] for row in block_rows if row["format"] == format_name),
            "steps": ci(row["steps"] for row in block_rows if row["format"] == format_name),
        }
        for format_name in FORMATS
    }
    values_by_format = {
        name: [row["formal"] for row in block_rows if row["format"] == name]
        for name in FORMATS
    }
    rng = random.Random(0x434F4E46)
    bootstraps = []
    for _ in range(bootstrap_count):
        bootstraps.append(mean(
            mean(rng.choice(values_by_format[name]) for _item in values_by_format[name])
            for name in FORMATS
        ))
    bootstraps.sort()
    portfolio = mean(format_effects[name]["formal"]["mean"] for name in FORMATS)
    portfolio_effect = {
        "mean": portfolio, "low": quantile(bootstraps, 0.025),
        "high": quantile(bootstraps, 0.975),
        "probability_positive": sum(value > 0 for value in bootstraps) / len(bootstraps),
    }
    map_effects = []
    for key, base in payload["scores"]["baseline"]["maps"].items():
        candidate = payload["scores"]["construction+birth"]["maps"][key]
        map_effects.append({
            "format": base["format"], "map": base["map"], "games": base["games"],
            "baseline": float(base["score"]), "candidate": float(candidate["score"]),
            "difference": float(candidate["score"]) - float(base["score"]),
        })
    round_effects = []
    for format_name in FORMATS:
        for round_number in sorted({row["round"] for row in block_rows}):
            values = [row["formal"] for row in block_rows
                      if row["format"] == format_name and row["round"] == round_number]
            if values:
                round_effects.append({"format": format_name, "round": round_number, **ci(values)})
    return {
        "integrity": {
            "games_per_variant": games_per_variant, "paired_games": len(per_game),
            "paired_blocks": len(block_rows),
            "statuses": {variant: Counter(match["status"] for match in matches[variant].values())
                         for variant in VARIANTS},
            "hosts": {variant: Counter(match["worker_host"] for match in matches[variant].values())
                      for variant in VARIANTS},
            "attempts": retries,
        },
        "transitions": {f"{before}->{after}": count for (before, after), count in transitions.items()},
        "portfolio_effect": portfolio_effect, "format_effects": format_effects,
        "map_effects": sorted(map_effects, key=lambda row: row["difference"], reverse=True),
        "round_effects": round_effects, "block_rows": block_rows,
    }


def feature_effects(path: Path, games_per_variant: int) -> dict[str, Any]:
    values: dict[tuple[str, int], dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    formats: dict[int, str] = {}
    maps: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["role"] != "candidate" or row["version"] != "Maxima":
                continue
            raw_id = int(row["match_id"])
            variant = "baseline" if raw_id <= games_per_variant else "construction+birth"
            match_id = raw_id if variant == "baseline" else raw_id - games_per_variant
            formats[match_id] = row["game_type"]
            maps[match_id] = row["map"]
            for field in FEATURE_FIELDS:
                if row.get(field) not in (None, ""):
                    values[(variant, match_id)][field].append(float(row[field]))
    match_values = {
        key: {field: mean(samples) for field, samples in fields.items()}
        for key, fields in values.items()
    }
    result: dict[str, Any] = {}
    for field in FEATURE_FIELDS:
        differences = defaultdict(list)
        for match_id in formats:
            base = match_values.get(("baseline", match_id), {}).get(field)
            candidate = match_values.get(("construction+birth", match_id), {}).get(field)
            if base is None or candidate is None:
                continue
            differences["overall"].append(candidate - base)
            differences[formats[match_id]].append(candidate - base)
        result[field] = {group: ci(samples) for group, samples in differences.items()}
    return result


def checkpoint_effects(path: Path) -> dict[str, Any]:
    rows: dict[tuple[str, str, int, str], dict[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            rows[(row["variant"], row["format"], int(row["checkpoint"]), row["field"])] = row
    effects: dict[str, Any] = {}
    for checkpoint in (5000, 10000, 20000, 30000):
        effects[str(checkpoint)] = {}
        for field in CHECKPOINT_FIELDS:
            format_rows = {}
            for format_name in FORMATS:
                base = rows.get(("baseline", format_name, checkpoint, field))
                candidate = rows.get(("construction+birth", format_name, checkpoint, field))
                if not base or not candidate:
                    continue
                base_value = float(base["maxima"])
                candidate_value = float(candidate["maxima"])
                format_rows[format_name] = {
                    "baseline": base_value, "candidate": candidate_value,
                    "difference": candidate_value - base_value,
                    "baseline_matches": int(base["matches"]),
                    "candidate_matches": int(candidate["matches"]),
                }
            if format_rows:
                effects[str(checkpoint)][field] = {
                    "formats": format_rows,
                    "baseline": mean(row["baseline"] for row in format_rows.values()),
                    "candidate": mean(row["candidate"] for row in format_rows.values()),
                    "difference": mean(row["difference"] for row in format_rows.values()),
                }
        for prefix in ("construction", "swarm", "inn"):
            assigned = effects[str(checkpoint)].get(f"{prefix}_assigned")
            working = effects[str(checkpoint)].get(f"{prefix}_working")
            if assigned and working:
                baseline = working["baseline"] / assigned["baseline"] if assigned["baseline"] else 0.0
                candidate = working["candidate"] / assigned["candidate"] if assigned["candidate"] else 0.0
                effects[str(checkpoint)][f"{prefix}_realization"] = {
                    "baseline": baseline,
                    "candidate": candidate,
                    "difference": candidate - baseline,
                }
    return effects


def event_effects(path: Path, games_per_variant: int) -> dict[str, Any]:
    totals = defaultdict(Counter)
    matches: dict[tuple[str, str], set[int]] = defaultdict(set)
    maps: dict[tuple[str, str], set[str]] = defaultdict(set)
    loaded_settings: dict[str, set[str]] = defaultdict(set)
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["version"] != "Maxima":
                continue
            raw_id = int(row["match_id"])
            variant = "baseline" if raw_id <= games_per_variant else "construction+birth"
            event = row["event"]
            totals[variant][event] += int(row["count"])
            matches[(variant, event)].add(raw_id)
            maps[(variant, event)].add(row["map"])
            if event == "strategy_loaded":
                try:
                    settings = json.loads(row["categorical_modes"]).get("settings")
                    if settings:
                        loaded_settings[variant].add(settings)
                except json.JSONDecodeError:
                    pass
    result = {}
    for event in sorted(set(totals["baseline"]) | set(totals["construction+birth"])):
        result[event] = {}
        for variant in VARIANTS:
            result[event][variant] = {
                "count": totals[variant][event],
                "matches": len(matches[(variant, event)]),
                "maps": len(maps[(variant, event)]),
            }
    return {"events": result, "loaded_settings": {key: sorted(value) for key, value in loaded_settings.items()}}


def raw_event_effects(path: Path, games_per_variant: int) -> dict[str, Any]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        header = next(csv.reader(handle))
    indexes = {name: index for index, name in enumerate(header)}
    pattern = ",(" + "|".join(re.escape(event) for event in RAW_EVENTS) + "),"
    process = subprocess.Popen(
        ["rg", "--no-line-number", pattern, str(path)],
        stdout=subprocess.PIPE, text=True, encoding="utf-8",
    )
    assert process.stdout is not None
    categories: dict[tuple[str, str, str], Counter[str]] = defaultdict(Counter)
    numeric: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    rows = Counter()
    reader = csv.reader(process.stdout)
    category_fields = ("reason", "kind", "phase", "previous", "posture", "campaign_state", "stage")
    numeric_fields = (
        "campaign_economy_ready", "campaign_force_ready", "campaign_population_ready",
        "campaign_ready_idle", "campaign_safe", "combat_ready_idle", "desired_warriors",
        "warriors", "workers", "population", "food_headroom", "construction_budget",
    )
    for row in reader:
        if len(row) != len(header):
            continue
        event = row[indexes["event"]]
        if event not in RAW_EVENTS:
            continue
        raw_id = int(row[indexes["match_id"]])
        variant = "baseline" if raw_id <= games_per_variant else "construction+birth"
        rows[(variant, event)] += 1
        for field in category_fields:
            value = row[indexes[field]]
            if value:
                categories[(variant, event, field)][value] += 1
        for field in numeric_fields:
            value = row[indexes[field]]
            if value:
                try:
                    numeric[(variant, event, field)].append(float(value))
                except ValueError:
                    pass
    if process.wait() not in (0, 1):
        raise RuntimeError("rg failed while extracting raw telemetry events")
    result: dict[str, Any] = {}
    for event in RAW_EVENTS:
        result[event] = {}
        for variant in VARIANTS:
            result[event][variant] = {
                "rows": rows[(variant, event)],
                "categories": {
                    field: dict(categories[(variant, event, field)])
                    for field in category_fields if categories[(variant, event, field)]
                },
                "numeric": {
                    field: {"n": len(numeric[(variant, event, field)]),
                            "mean": mean(numeric[(variant, event, field)])}
                    for field in numeric_fields if numeric[(variant, event, field)]
                },
            }
    return result


def percent(value: float, signed: bool = False) -> str:
    return f"{value:+.2%}" if signed else f"{value:.1%}"


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    outcomes = payload["outcomes"]
    checkpoints = payload["checkpoints"]
    events = payload["events"]["events"]
    raw = payload["raw_events"]
    portfolio = outcomes["portfolio_effect"]
    lines = [
        "# Maxima construction+birth confirmation: deep findings", "",
        "## Verdict", "",
        f"The construction+birth bundle is **not a new default**. Its equal-format paired effect was "
        f"{percent(portfolio['mean'], True)} (stratified bootstrap 95% interval "
        f"{percent(portfolio['low'], True)} to {percent(portfolio['high'], True)}; "
        f"P(effect > 0)={portfolio['probability_positive']:.1%}). The result is practically flat and "
        "does not confirm the smaller ablation's apparent gain.", "",
        "| Format | Paired effect | 95% interval | Blocks |",
        "|---|---:|---:|---:|",
    ]
    for format_name in FORMATS:
        effect = outcomes["format_effects"][format_name]["formal"]
        lines.append(
            f"| {format_name} | {percent(effect['mean'], True)} | "
            f"{percent(effect['low'], True)} to {percent(effect['high'], True)} | {effect['n']} |"
        )
    lines.extend(["", "## Economic mechanism", "",
                  "Equal-format observer means; positive deltas favor construction+birth.", "",
                  "| Tick | Construction assigned | Construction working | Construction realization | Swarm assigned | Swarm working | Workers | Population | Warriors |",
                  "|---:|---:|---:|---:|---:|---:|---:|---:|---:|"])
    for tick in (5000, 10000, 20000, 30000):
        row = checkpoints[str(tick)]
        realization = row["construction_realization"]
        lines.append(
            f"| {tick:,} | {row['construction_assigned']['difference']:+.2f} | "
            f"{row['construction_working']['difference']:+.2f} | "
            f"{realization['difference']:+.1%} | {row['swarm_assigned']['difference']:+.2f} | "
            f"{row['swarm_working']['difference']:+.2f} | {row['workers']['difference']:+.2f} | "
            f"{row['population']['difference']:+.2f} | {row['warriors']['difference']:+.2f} |"
        )
    lines.extend(["", "## Strategy execution coverage", "",
                  "| Event | Baseline matches | Candidate matches | Baseline events | Candidate events |",
                  "|---|---:|---:|---:|---:|"])
    for event in SYSTEM_EVENTS + TACTICAL_EVENTS:
        base = events.get(event, {}).get("baseline", {"matches": 0, "count": 0})
        candidate = events.get(event, {}).get("construction+birth", {"matches": 0, "count": 0})
        lines.append(
            f"| {event} | {base['matches']} | {candidate['matches']} | "
            f"{base['count']} | {candidate['count']} |"
        )
    lines.extend(["", "## Tactical conversion", ""])
    for event in ("mission_selected", "mission_finished", "attack_finished"):
        lines.append(f"### `{event}`")
        for variant in VARIANTS:
            categories = raw[event][variant]["categories"]
            details = []
            for field in ("kind", "reason", "phase"):
                if field in categories:
                    details.append(field + ": " + ", ".join(
                        f"{key}={value}" for key, value in sorted(
                            categories[field].items(), key=lambda item: item[1], reverse=True
                        )
                    ))
            lines.append(f"- {variant}: {raw[event][variant]['rows']} events; " + "; ".join(details))
        lines.append("")
    best = outcomes["map_effects"][:10]
    worst = list(reversed(outcomes["map_effects"][-10:]))
    lines.extend(["## Largest map effects", "", "| Direction | Format | Map | Effect | Games |",
                  "|---|---|---|---:|---:|"])
    for direction, rows in (("gain", best), ("loss", worst)):
        for row in rows:
            lines.append(
                f"| {direction} | {row['format']} | {row['map']} | "
                f"{percent(row['difference'], True)} | {row['games']} |"
            )
    lines.extend(["", "## Interpretation", "",
                  "The bundle executes as configured and materially raises nominal staffing, but the extra slots do not convert proportionally into active construction or a durable population/military advantage. It redistributes timing and helps some FFA3/2v2 blocks while hurting duel starts and particular maps. The limiting problem is therefore assignment realization and downstream force conversion, not a globally low staffing constant.", "",
                  "The next experiment should gate extra construction staffing on reachable free labor plus material readiness, while leaving birth staffing independently switchable. This directly tests whether the unused assigned slots are the mechanism behind the null aggregate result."])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    result_dir = args.result_dir.resolve()
    with (result_dir / "results.json").open(encoding="utf-8") as handle:
        tournament_payload = json.load(handle)
    games_per_variant = int(tournament_payload["metadata"]["games_per_variant"])
    result = {
        "outcomes": paired_outcomes(tournament_payload, args.bootstrap),
        "features": feature_effects(result_dir / "game_features.csv", games_per_variant),
        "checkpoints": checkpoint_effects(result_dir / "checkpoint_metrics.csv"),
        "events": event_effects(result_dir / "strategy_events.csv", games_per_variant),
        "raw_events": raw_event_effects(result_dir / "telemetry.csv", games_per_variant),
    }
    with (result_dir / "paired_analysis.json").open("w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    write_markdown(result_dir / "findings.md", result)
    print(result_dir / "findings.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
