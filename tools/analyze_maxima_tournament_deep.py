#!/usr/bin/env python3
"""Deep execution and outcome analysis for an all-format Maxima tournament."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean, median
from typing import Any, Iterable


PHASES = ("early", "mid", "late", "final")
CHECKPOINTS = (5000, 10000, 20000, 30000)
METRICS = ("workers", "population", "warriors", "buildings", "attack_power", "food_capacity", "explorers")
OPERATIONAL_METRICS = (
    "workers", "free_workers", "working_units", "worker_jobs_open", "assigned_worker_slots",
    "hungry", "critical_food", "unserved_food", "swarms", "swarm_assigned",
    "swarm_working", "swarm_empty", "swarm_production_timeout", "swarm_corn",
    "swarm_corn_distance", "inns", "inn_assigned", "inn_working", "inn_corn",
    "building_sites", "construction_assigned", "construction_working", "warriors",
    "explorers", "military_assigned", "military_working", "technology_assigned",
    "technology_working", "schools", "barracks", "pools", "racetracks",
    "clearing_flags", "clearing_flag_assigned", "clearing_flag_units",
)
KEY_EVENTS = (
    "strategy_loaded", "farming_fertility_cache", "farming_policy",
    "maintenance_clearing", "land_clearing_started", "land_clearing_finished",
    "recon_mission_created", "recon_suspended", "recon_resumed",
    "preemptive_defense_updated", "reactive_defense_updated",
    "colony_swarm_selected", "colony_swarm_completed", "colony_swarm_failed",
    "mission_selected", "mission_phase_changed", "mission_finished",
    "mission_retargeted", "attack_finished", "dig_out_started",
    "explorer_strike_launched", "swarm_retirement_issued",
)
GATES = (
    "campaign_economy_ready", "campaign_force_ready", "campaign_population_ready",
    "campaign_ready_idle", "campaign_safe", "colonization_eligible",
    "clearing_active", "recovery", "space_constrained",
)


def number(value: Any) -> float | None:
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def average(values: Iterable[float | None]) -> float | None:
    clean = [value for value in values if value is not None]
    return mean(clean) if clean else None


def pearson(pairs: list[tuple[float, float]]) -> float | None:
    if len(pairs) < 2:
        return None
    xs, ys = zip(*pairs)
    xm, ym = mean(xs), mean(ys)
    numerator = sum((x - xm) * (y - ym) for x, y in pairs)
    denominator = math.sqrt(sum((x - xm) ** 2 for x in xs) * sum((y - ym) ** 2 for y in ys))
    return numerator / denominator if denominator else None


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_matches(directory: Path) -> dict[int, dict[str, Any]]:
    """Load the legacy matches table or the confirmation runner's results bundle."""
    csv_path = directory / "matches.csv"
    if csv_path.is_file():
        return {int(row["id"]): row for row in load_csv(csv_path)}
    payload = json.loads((directory / "results.json").read_text(encoding="utf-8"))
    result: dict[int, dict[str, Any]] = {}
    for variant_matches in payload.get("matches", {}).values():
        for match in variant_matches:
            maxima = [player for player in match.get("players", []) if player.get("version") == "Maxima"]
            if any(player.get("won") for player in maxima):
                outcome = "win"
            elif maxima and all(player.get("lost") for player in maxima):
                outcome = "loss"
            else:
                outcome = "draw"
            result[int(match["id"])] = {**match, "outcome": outcome}
    return result


def spooled(directory: Path):
    for path in sorted((directory / "match-telemetry").glob("match-*.json.gz")):
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            yield json.load(handle)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fields = list(rows[0]) if rows else []
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        if fields:
            writer.writeheader()
            writer.writerows(rows)


def fmt(value: float | None, digits: int = 2) -> str:
    return "—" if value is None else f"{value:+.{digits}f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve()
    matches = load_matches(directory)
    features = load_csv(directory / "game_features.csv")

    per_match: dict[int, dict[str, list[dict[str, str]]]] = defaultdict(lambda: defaultdict(list))
    participants: dict[tuple[int, int], dict[str, Any]] = {}
    for row in features:
        match_id, team = int(row["match_id"]), int(row["team"])
        per_match[match_id][row["version"]].append(row)
        if row["version"] == "Maxima":
            participants[(match_id, team)] = {
                "format": row["game_type"], "map": row["map"],
                "score": number(row.get("victory_score")) or 0.0,
                "normalized_score": number(row.get("normalized_score")) or 0.5,
                "won": row.get("won") == "1", "lost": row.get("lost") == "1",
                "steps": int(float(row.get("steps") or 0)),
            }

    phase_values: dict[tuple[str, str, str], list[tuple[float, float]]] = defaultdict(list)
    outcome_phase_values: dict[tuple[str, str, str, str], list[float]] = defaultdict(list)
    for match_id, versions in per_match.items():
        if not versions.get("Maxima") or not versions.get("Nicowar"):
            continue
        format_name = matches[match_id]["format"]
        outcome_name = matches[match_id]["outcome"]
        for phase in PHASES:
            for metric in METRICS:
                key = f"{phase}_{metric}"
                maxima = average(number(row.get(key)) for row in versions["Maxima"])
                nicowar = average(number(row.get(key)) for row in versions["Nicowar"])
                if maxima is None or nicowar is None:
                    continue
                phase_values[(format_name, phase, metric)].append((maxima, nicowar))
                outcome_phase_values[(format_name, outcome_name, phase, metric)].append(maxima - nicowar)

    phase_rows = []
    for (format_name, phase, metric), values in sorted(phase_values.items()):
        maxima = mean(left for left, _ in values)
        nicowar = mean(right for _, right in values)
        phase_rows.append({
            "format": format_name, "phase": phase, "metric": metric, "matches": len(values),
            "maxima_mean": round(maxima, 6), "nicowar_mean": round(nicowar, 6),
            "difference": round(maxima - nicowar, 6),
            "ratio": round(maxima / nicowar, 6) if nicowar else "",
        })
    write_csv(directory / "phase_comparison.csv", phase_rows)

    event_counts: dict[tuple[int, int], Counter[str]] = defaultdict(Counter)
    event_first: dict[tuple[int, int, str], int] = {}
    posture_snapshots: Counter[tuple[str, str]] = Counter()
    gate_counts: Counter[tuple[str, str, str]] = Counter()
    gate_totals: Counter[tuple[str, str]] = Counter()
    detail_counts: Counter[tuple[str, str, str, str]] = Counter()
    topology: dict[tuple[int, int], dict[str, float]] = {}
    operational_values: dict[tuple[str, str, str], list[tuple[float, float]]] = defaultdict(list)
    checkpoint_values: dict[tuple[str, int, str], list[tuple[float, float]]] = defaultdict(list)

    for match in spooled(directory):
        match_id = int(match["id"])
        format_name = str(match["format"])
        duration = max(1, int(match.get("steps", 0)))
        operational: dict[tuple[str, str, str], list[float]] = defaultdict(list)
        checkpoint_snapshots: dict[tuple[str, int, int], dict[str, Any]] = {}
        for snapshot in match.get("observer", []):
            tick = int(snapshot["tick"])
            fraction = tick / duration
            phase = "early" if fraction <= 1 / 3 else ("mid" if fraction <= 2 / 3 else "late")
            version = str(snapshot.get("version", ""))
            team = int(snapshot["team"])
            for checkpoint in CHECKPOINTS:
                if tick <= checkpoint:
                    key = (version, team, checkpoint)
                    if key not in checkpoint_snapshots or int(checkpoint_snapshots[key]["tick"]) < tick:
                        checkpoint_snapshots[key] = snapshot
            for metric in OPERATIONAL_METRICS:
                value = number(snapshot.get(metric))
                if value is not None:
                    operational[(version, phase, metric)].append(value)
        for phase in ("early", "mid", "late"):
            for metric in OPERATIONAL_METRICS:
                maxima = average(operational.get(("Maxima", phase, metric), []))
                nicowar = average(operational.get(("Nicowar", phase, metric), []))
                if maxima is not None and nicowar is not None:
                    operational_values[(format_name, phase, metric)].append((maxima, nicowar))
        for checkpoint in CHECKPOINTS:
            if duration < checkpoint:
                continue
            for metric in METRICS:
                maxima = average(number(snapshot.get(metric)) for (version, _, point), snapshot in checkpoint_snapshots.items() if version == "Maxima" and point == checkpoint)
                nicowar = average(number(snapshot.get(metric)) for (version, _, point), snapshot in checkpoint_snapshots.items() if version == "Nicowar" and point == checkpoint)
                if maxima is not None and nicowar is not None:
                    checkpoint_values[(format_name, checkpoint, metric)].append((maxima, nicowar))
        for event in match.get("telemetry", []):
            team = int(event["team"])
            participant = (match_id, team)
            if participant not in participants:
                continue
            name = str(event["event"])
            event_counts[participant][name] += 1
            event_first.setdefault((match_id, team, name), int(event["tick"]))
            if name == "director_snapshot":
                posture_snapshots[(format_name, str(event.get("posture", "unknown")))] += 1
                for gate in GATES:
                    if gate in event:
                        gate_totals[(format_name, gate)] += 1
                        gate_counts[(format_name, gate, str(event[gate]))] += 1
                if participant not in topology:
                    topology[participant] = {
                        key: value for key in (
                            "global_water_percent", "global_largest_land_percent",
                            "global_shoreline_density", "global_chokepoint_density",
                            "terrain_abundance", "topology_complexity",
                        ) if (value := number(event.get(key))) is not None
                    }
            if name in {"mission_selected", "mission_finished", "mission_phase_changed", "posture_changed", "colony_swarm_failed"}:
                category = str(event.get("kind") or event.get("outcome") or event.get("phase") or event.get("posture") or event.get("stage") or event.get("reason") or "unspecified")
                detail_counts[(format_name, name, category, str(event.get("reason", "")))] += 1

    denominators = Counter(value["format"] for value in participants.values())
    coverage_rows = []
    all_events = sorted({event for counts in event_counts.values() for event in counts})
    for format_name in sorted(denominators):
        eligible = [key for key, value in participants.items() if value["format"] == format_name]
        for event in all_events:
            present = [key for key in eligible if event_counts[key][event]]
            first_fractions = [
                event_first[(key[0], key[1], event)] / max(1, participants[key]["steps"])
                for key in present
            ]
            coverage_rows.append({
                "format": format_name, "event": event, "instances": len(eligible),
                "instances_with_event": len(present),
                "coverage": round(len(present) / len(eligible), 8) if eligible else 0,
                "event_count": sum(event_counts[key][event] for key in eligible),
                "mean_count_when_present": round(mean(event_counts[key][event] for key in present), 6) if present else "",
                "median_first_tick_fraction": round(median(first_fractions), 6) if first_fractions else "",
            })
    write_csv(directory / "event_coverage.csv", coverage_rows)

    effect_rows = []
    for format_name in sorted(denominators):
        eligible = [key for key, value in participants.items() if value["format"] == format_name]
        for event in all_events:
            with_event = [participants[key]["normalized_score"] for key in eligible if event_counts[key][event]]
            without = [participants[key]["normalized_score"] for key in eligible if not event_counts[key][event]]
            if len(with_event) < 4 or len(without) < 4:
                continue
            effect_rows.append({
                "format": format_name, "event": event,
                "with_event": len(with_event), "without_event": len(without),
                "mean_normalized_with": round(mean(with_event), 8),
                "mean_normalized_without": round(mean(without), 8),
                "difference": round(mean(with_event) - mean(without), 8),
            })
    write_csv(directory / "execution_effects.csv", effect_rows)

    posture_rows = []
    posture_totals = Counter()
    for (format_name, _), count in posture_snapshots.items():
        posture_totals[format_name] += count
    for (format_name, posture), count in sorted(posture_snapshots.items()):
        posture_rows.append({
            "format": format_name, "posture": posture, "snapshots": count,
            "share": round(count / posture_totals[format_name], 8),
        })
    write_csv(directory / "posture_usage.csv", posture_rows)

    gate_rows = []
    for (format_name, gate), total in sorted(gate_totals.items()):
        true_count = gate_counts[(format_name, gate, "1")] + gate_counts[(format_name, gate, "true")]
        gate_rows.append({
            "format": format_name, "gate": gate, "snapshots": total,
            "true_count": true_count, "true_rate": round(true_count / total, 8) if total else 0,
            "values": json.dumps({value: count for (fmt, item, value), count in gate_counts.items() if fmt == format_name and item == gate}, sort_keys=True),
        })
    write_csv(directory / "gate_rates.csv", gate_rows)

    detail_rows = [
        {"format": key[0], "event": key[1], "category": key[2], "reason": key[3], "count": count}
        for key, count in sorted(detail_counts.items())
    ]
    write_csv(directory / "strategy_details.csv", detail_rows)

    operational_rows = []
    for (format_name, phase, metric), values in sorted(operational_values.items()):
        maxima = mean(value[0] for value in values)
        nicowar = mean(value[1] for value in values)
        operational_rows.append({
            "format": format_name, "phase": phase, "metric": metric,
            "matches": len(values), "maxima_mean": round(maxima, 8),
            "nicowar_mean": round(nicowar, 8), "difference": round(maxima - nicowar, 8),
            "ratio": round(maxima / nicowar, 8) if nicowar else "",
        })
    write_csv(directory / "operational_comparison.csv", operational_rows)

    checkpoint_rows = []
    for (format_name, checkpoint, metric), values in sorted(checkpoint_values.items()):
        maxima = mean(value[0] for value in values)
        nicowar = mean(value[1] for value in values)
        checkpoint_rows.append({
            "format": format_name, "tick": checkpoint, "metric": metric,
            "matches": len(values), "maxima_mean": round(maxima, 8),
            "nicowar_mean": round(nicowar, 8), "difference": round(maxima - nicowar, 8),
            "ratio": round(maxima / nicowar, 8) if nicowar else "",
        })
    write_csv(directory / "checkpoint_comparison.csv", checkpoint_rows)

    topology_rows = []
    for key, values in topology.items():
        topology_rows.append({
            "match_id": key[0], "team": key[1], **participants[key], **values,
        })
    write_csv(directory / "topology_outcomes.csv", topology_rows)
    topology_correlations = {}
    for metric in ("global_water_percent", "global_largest_land_percent", "global_shoreline_density", "global_chokepoint_density", "terrain_abundance", "topology_complexity"):
        pairs = [(float(row[metric]), float(row["normalized_score"])) for row in topology_rows if metric in row]
        topology_correlations[metric] = pearson(pairs)

    key_coverage = {(row["format"], row["event"]): row for row in coverage_rows}
    phase_index = {(row["format"], row["phase"], row["metric"]): row for row in phase_rows}
    formats = sorted(denominators)
    lines = [
        "# Deep Maxima tournament analysis", "",
        f"- Matches: {len(matches)}", f"- Maxima instances: {len(participants)}",
        "- Interpretation: event presence and correlation are diagnostic associations, not causal effects.", "",
        "## Economy and force trajectory", "",
        "Values are Maxima minus the mean Nicowar player in the same match.", "",
        "| Format | Early workers | Mid workers | Late workers | Early warriors | Mid warriors | Late warriors | Final population |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for format_name in formats:
        values = []
        for phase, metric in (("early", "workers"), ("mid", "workers"), ("late", "workers"),
                              ("early", "warriors"), ("mid", "warriors"), ("late", "warriors"),
                              ("final", "population")):
            row = phase_index.get((format_name, phase, metric))
            values.append(fmt(float(row["difference"])) if row else "—")
        lines.append(f"| {format_name} | " + " | ".join(values) + " |")

    lines.extend(["", "## Strategy execution coverage", "",
                  "Coverage is the share of Maxima AI instances that emitted each event at least once.", "",
                  "| Event | " + " | ".join(formats) + " |",
                  "|---|" + "---:|" * len(formats)])
    for event in KEY_EVENTS:
        values = []
        for format_name in formats:
            row = key_coverage.get((format_name, event))
            values.append(f"{float(row['coverage']):.1%}" if row else "0.0%")
        lines.append(f"| {event} | " + " | ".join(values) + " |")

    operational_index = {(row["format"], row["phase"], row["metric"]): row for row in operational_rows}
    lines.extend(["", "## Operational pressure", "",
                  "Ratios are Maxima divided by Nicowar within the same match and phase.", "",
                  "| Format | Early swarm working | Mid swarm working | Mid construction working | Mid hungry | Mid military working |",
                  "|---|---:|---:|---:|---:|---:|"])
    for format_name in formats:
        values = []
        for phase, metric in (("early", "swarm_working"), ("mid", "swarm_working"),
                              ("mid", "construction_working"), ("mid", "hungry"),
                              ("mid", "military_working")):
            row = operational_index.get((format_name, phase, metric))
            ratio = number(row.get("ratio")) if row else None
            values.append("—" if ratio is None else f"{ratio:.2f}×")
        lines.append(f"| {format_name} | " + " | ".join(values) + " |")

    checkpoint_index = {(row["format"], int(row["tick"]), row["metric"]): row for row in checkpoint_rows}
    lines.extend(["", "## Fixed-tick worker gap", "",
                  "| Format | Tick 5k | Tick 10k | Tick 20k | Tick 30k |",
                  "|---|---:|---:|---:|---:|"])
    for format_name in formats:
        values = []
        for checkpoint in CHECKPOINTS:
            row = checkpoint_index.get((format_name, checkpoint, "workers"))
            values.append(fmt(float(row["difference"])) if row else "—")
        lines.append(f"| {format_name} | " + " | ".join(values) + " |")

    lines.extend(["", "## Posture occupancy", "",
                  "| Format | Recover | Defend | Expand | Develop | Mobilize | Campaign | Finish |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|"])
    posture_index = {(row["format"], row["posture"]): float(row["share"]) for row in posture_rows}
    for format_name in formats:
        lines.append("| " + format_name + " | " + " | ".join(
            f"{posture_index.get((format_name, posture), 0):.1%}"
            for posture in ("recover", "defend", "expand", "develop", "mobilize", "campaign", "finish")
        ) + " |")

    lines.extend(["", "## Topology associations", "",
                  "| Topology field | Correlation with Maxima normalized score |", "|---|---:|"])
    for metric, value in topology_correlations.items():
        lines.append(f"| {metric} | {fmt(value, 3)} |")

    lines.extend(["", "## Largest event-presence score associations", "",
                  "| Format | Event | With/without | Normalized-score difference |", "|---|---|---:|---:|"])
    for row in sorted(effect_rows, key=lambda item: abs(float(item["difference"])), reverse=True)[:25]:
        lines.append(
            f"| {row['format']} | {row['event']} | {row['with_event']}/{row['without_event']} | {float(row['difference']):+.3f} |"
        )
    lines.extend(["", "Machine-readable details: `phase_comparison.csv`, `operational_comparison.csv`, `checkpoint_comparison.csv`, `event_coverage.csv`, `execution_effects.csv`, `posture_usage.csv`, `gate_rates.csv`, `strategy_details.csv`, and `topology_outcomes.csv`.", ""])
    (directory / "deep-analysis.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote deep analysis for {len(matches)} matches and {len(participants)} Maxima instances")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
