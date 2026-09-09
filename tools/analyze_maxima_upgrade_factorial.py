#!/usr/bin/env python3
"""Paired outcome and telemetry analysis for the Maxima builder factorial."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean, stdev
from typing import Any


FORMATS = ("duel", "ffa3", "ffa4", "ffa5", "2v2")
CHECKPOINTS = (1000, 3000, 5000, 10000, 20000, 30000)
FIELDS = (
    "workers", "population", "warriors", "buildings", "building_sites",
    "construction_assigned", "construction_working", "technology_assigned",
    "technology_working", "build_upgraded_workers", "level1_buildings",
    "level2_buildings", "level3_buildings", "schools", "school_level1",
    "school_level2", "hungry", "critical_food", "unserved_food",
)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_dir", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def summary(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"n": 0, "mean": 0.0, "se": 0.0, "low": 0.0, "high": 0.0}
    average = mean(values)
    se = stdev(values) / math.sqrt(len(values)) if len(values) > 1 else 0.0
    return {"n": len(values), "mean": average, "se": se,
            "low": average - 1.96 * se, "high": average + 1.96 * se}


def outcome(match: dict[str, Any]) -> str:
    if match["kind"] == "2v2":
        return {"A": "win", "B": "loss"}.get(match.get("winner_side"), "draw")
    player = next(item for item in match["players"] if item["role"] == "candidate")
    return "win" if player["won"] else "loss" if player["lost"] else "draw"


def nearest(rows: list[dict[str, Any]], checkpoint: int) -> list[dict[str, Any]]:
    ticks = {int(row["tick"]) for row in rows}
    if not ticks:
        return []
    tick = min(ticks, key=lambda item: abs(item - checkpoint))
    return [row for row in rows if int(row["tick"]) == tick] if abs(tick - checkpoint) <= 500 else []


def block_effect(rows: list[dict[str, Any]]) -> dict[str, Any]:
    blocks: dict[tuple[Any, ...], list[float]] = defaultdict(list)
    for row in rows:
        blocks[(row["format"], row["round"], row["block"], row["low_mask"])].append(row["delta"])
    values = [mean(samples) for samples in blocks.values()]
    result = summary(values)
    result["games"] = len(rows)
    return result


def main() -> int:
    args = parse_args()
    result_dir = args.result_dir.resolve()
    output = (args.output or result_dir / "deep_findings.md").resolve()
    with (result_dir / "results.json").open(encoding="utf-8") as handle:
        payload = json.load(handle)
    configurations = sorted(payload["configurations"], key=lambda item: int(item["id"]))
    games = int(payload["metadata"]["games_per_variant"])
    factors = [name for name, _values in (
        ("early_school", None), ("initial_staffing", None),
        ("upgrade_parallelism", None))]
    names = {int(item["id"]): item["name"] for item in configurations}
    compact: dict[int, dict[int, dict[str, Any]]] = {}
    integrity: dict[str, Any] = {}
    for mask, name in names.items():
        matches = payload["matches"][name]
        compact[mask] = {(int(item["id"]) - 1) % games + 1: item for item in matches}
        integrity[name] = {"matches": len(matches), "status": dict(Counter(
            item["status"] for item in matches)), "hosts": dict(Counter(
            item["worker_host"] for item in matches))}

    outcome_effects: dict[str, Any] = {}
    outcome_rows: list[dict[str, Any]] = []
    for bit, factor in enumerate(factors):
        rows = []
        for low_mask in range(8):
            if low_mask & (1 << bit):
                continue
            high_mask = low_mask | (1 << bit)
            for match_id in sorted(set(compact[low_mask]) & set(compact[high_mask])):
                low, high = compact[low_mask][match_id], compact[high_mask][match_id]
                for metric, index in (("formal", 0), ("shaped", 1)):
                    rows.append({"factor": factor, "metric": metric,
                                 "format": low["format"], "map": low["map"],
                                 "round": int(low["round"]), "block": int(low["block"]),
                                 "low_mask": low_mask,
                                 "delta": float(high["_portfolio_score"][index])
                                          - float(low["_portfolio_score"][index])})
        outcome_effects[factor] = {}
        for metric in ("formal", "shaped"):
            selected = [row for row in rows if row["metric"] == metric]
            outcome_effects[factor][metric] = {
                "overall": block_effect(selected),
                "formats": {fmt: block_effect([row for row in selected if row["format"] == fmt])
                            for fmt in FORMATS},
            }
        outcome_rows.extend(rows)

    telemetry: dict[tuple[int, int, int, str], float] = {}
    opponent: dict[tuple[int, int, str], float] = {}
    spool = result_dir / "match-telemetry"
    status = Counter()
    for path in sorted(spool.glob("match-*.json.gz")):
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            match = json.load(handle)
        status[match.get("status", "missing")] += 1
        if match.get("status") != "completed":
            continue
        raw_id = int(match["id"])
        mask = (raw_id - 1) // games
        match_id = (raw_id - 1) % games + 1
        observer = match.get("observer", [])
        for checkpoint in CHECKPOINTS:
            sample = nearest(observer, checkpoint)
            maxima = [row for row in sample if row.get("version") == "Maxima"]
            nicowar = [row for row in sample if row.get("version") == "Nicowar"]
            for field in FIELDS:
                try:
                    telemetry[(mask, match_id, checkpoint, field)] = mean(
                        float(row[field]) for row in maxima)
                    if mask == 0:
                        opponent[(match_id, checkpoint, field)] = mean(
                            float(row[field]) for row in nicowar)
                except (KeyError, ValueError, TypeError):
                    pass

    telemetry_effects: dict[str, Any] = {}
    telemetry_rows: list[dict[str, Any]] = []
    for bit, factor in enumerate(factors):
        telemetry_effects[factor] = {}
        for checkpoint in CHECKPOINTS:
            telemetry_effects[factor][str(checkpoint)] = {}
            for field in FIELDS:
                rows = []
                for low_mask in range(8):
                    if low_mask & (1 << bit):
                        continue
                    high_mask = low_mask | (1 << bit)
                    for match_id in range(1, games + 1):
                        low = telemetry.get((low_mask, match_id, checkpoint, field))
                        high = telemetry.get((high_mask, match_id, checkpoint, field))
                        if low is None or high is None:
                            continue
                        match = compact[low_mask][match_id]
                        rows.append({"factor": factor, "checkpoint": checkpoint,
                                     "field": field, "format": match["format"],
                                     "round": int(match["round"]),
                                     "block": int(match["block"]),
                                     "low_mask": low_mask, "delta": high - low})
                effect = block_effect(rows)
                telemetry_effects[factor][str(checkpoint)][field] = effect
                telemetry_rows.append({"factor": factor, "checkpoint": checkpoint,
                                       "field": field, **effect})

    baseline_gap: dict[str, Any] = {}
    for checkpoint in CHECKPOINTS:
        baseline_gap[str(checkpoint)] = {}
        for field in FIELDS:
            differences = []
            maxima_values = []
            nicowar_values = []
            for match_id in range(1, games + 1):
                maxima = telemetry.get((0, match_id, checkpoint, field))
                nico = opponent.get((match_id, checkpoint, field))
                if maxima is None or nico is None:
                    continue
                maxima_values.append(maxima); nicowar_values.append(nico)
                differences.append(maxima - nico)
            baseline_gap[str(checkpoint)][field] = {
                "n": len(differences),
                "maxima": mean(maxima_values) if maxima_values else 0.0,
                "nicowar": mean(nicowar_values) if nicowar_values else 0.0,
                "delta": mean(differences) if differences else 0.0,
            }

    analysis = {"integrity": integrity, "spool_status": dict(status),
                "outcome_effects": outcome_effects,
                "telemetry_effects": telemetry_effects,
                "baseline_gap": baseline_gap}
    with (result_dir / "deep_analysis.json").open("w", encoding="utf-8") as handle:
        json.dump(analysis, handle, indent=2, sort_keys=True); handle.write("\n")
    with (result_dir / "factor_telemetry.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(telemetry_rows[0]))
        writer.writeheader(); writer.writerows(telemetry_rows)
    with (result_dir / "paired_outcome_effects.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(outcome_rows[0]))
        writer.writeheader(); writer.writerows(outcome_rows)

    ranked = sorted(configurations,
                    key=lambda item: payload["scores"][item["name"]]["objective"], reverse=True)
    lines = ["# Maxima builder-pipeline factorial: deep analysis", "",
             "## Variant results", "",
             "| Variant | W-L-D | Portfolio | Worst format | Objective |",
             "|---|---:|---:|---:|---:|"]
    for item in ranked:
        name = item["name"]; matches = payload["matches"][name]
        counts = Counter(outcome(match) for match in matches); score = payload["scores"][name]
        lines.append(f"| {name} | {counts['win']}-{counts['loss']}-{counts['draw']} | "
                     f"{score['portfolio_score']:.1%} | {score['worst_format_score']:.1%} | "
                     f"{score['objective']:.4f} |")
    lines.extend(["", "## Paired causal effects", "",
                  "Averaged over matched map/seed/seat blocks and the other two factors.", "",
                  "| Factor | Formal effect | 95% interval | Shaped effect |",
                  "|---|---:|---:|---:|"])
    for factor in factors:
        formal = outcome_effects[factor]["formal"]["overall"]
        shaped = outcome_effects[factor]["shaped"]["overall"]
        lines.append(f"| {factor} | {formal['mean']:+.2%} | {formal['low']:+.2%} to "
                     f"{formal['high']:+.2%} | {shaped['mean']:+.3f} |")
    lines.extend(["", "## Mechanism deltas", "",
                  "Positive values mean enabling that factor increased Maxima's metric. Later checkpoints are survival-conditioned.", "",
                  "| Factor | Tick | Builders | L2 buildings | Construction working | Workers | Population | Warriors |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|"])
    for factor in factors:
        for checkpoint in CHECKPOINTS:
            values = telemetry_effects[factor][str(checkpoint)]
            lines.append(f"| {factor} | {checkpoint:,} | "
                         f"{values['build_upgraded_workers']['mean']:+.2f} | "
                         f"{values['level2_buildings']['mean']:+.2f} | "
                         f"{values['construction_working']['mean']:+.2f} | "
                         f"{values['workers']['mean']:+.2f} | "
                         f"{values['population']['mean']:+.2f} | "
                         f"{values['warriors']['mean']:+.2f} |")
    lines.extend(["", "## Untuned baseline: Maxima minus Nicowar", "",
                  "| Tick | Builders | L2 buildings | Construction working | Workers | Population | Warriors |",
                  "|---:|---:|---:|---:|---:|---:|---:|"])
    for checkpoint in CHECKPOINTS:
        gap = baseline_gap[str(checkpoint)]
        lines.append(f"| {checkpoint:,} | {gap['build_upgraded_workers']['delta']:+.2f} | "
                     f"{gap['level2_buildings']['delta']:+.2f} | "
                     f"{gap['construction_working']['delta']:+.2f} | "
                     f"{gap['workers']['delta']:+.2f} | {gap['population']['delta']:+.2f} | "
                     f"{gap['warriors']['delta']:+.2f} |")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
