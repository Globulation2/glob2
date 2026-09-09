#!/usr/bin/env python3
"""Analyze one or more Nicowar tournament result directories.

The analysis is deliberately dependency-free so it can run on tournament hosts.
It reports feature/score correlations by map and game format and estimates the
score difference associated with each recorded strategy event.  These are
exploratory associations, not causal claims.
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean
from typing import Any


IDENTIFIERS = {
    "match_id", "block", "round", "game_type", "map", "map_file", "seed",
    "team", "version", "role", "engine_status", "signal_quality",
    "victory_score", "normalized_score", "won", "lost", "alive",
    "_source",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="Result directories or game_features.csv files")
    parser.add_argument("--output-dir", type=Path, help="Default: first input directory")
    parser.add_argument("--min-samples", type=int, default=8)
    args = parser.parse_args()
    if args.min_samples < 4:
        parser.error("--min-samples must be at least 4")
    return args


def _table_path(source: Path, name: str) -> Path:
    return source / name if source.is_dir() else source.with_name(name)


def _read_rows(sources: list[Path], name: str) -> list[dict[str, str]]:
    rows = []
    for source in sources:
        path = _table_path(source, name)
        if not path.is_file():
            continue
        with path.open(newline="", encoding="utf-8") as handle:
            rows.extend({**row, "_source": str(path.parent.resolve())} for row in csv.DictReader(handle))
    return rows


def _float(value: Any) -> float | None:
    try:
        number = float(value)
        return number if math.isfinite(number) else None
    except (TypeError, ValueError):
        return None


def pearson(pairs: list[tuple[float, float]]) -> float | None:
    if len(pairs) < 2:
        return None
    xs, ys = zip(*pairs)
    x_mean, y_mean = mean(xs), mean(ys)
    numerator = sum((x - x_mean) * (y - y_mean) for x, y in pairs)
    x_square = sum((x - x_mean) ** 2 for x in xs)
    y_square = sum((y - y_mean) ** 2 for y in ys)
    denominator = math.sqrt(x_square * y_square)
    return numerator / denominator if denominator else None


def correlation_rows(features: list[dict[str, str]], minimum: int) -> list[dict[str, Any]]:
    numeric_features = sorted({key for row in features for key in row} - IDENTIFIERS)
    groups: dict[tuple[str, str], list[dict[str, str]]] = {("overall", "all"): features}
    for dimension in ("game_type", "map"):
        values = sorted({row.get(dimension, "") for row in features if row.get(dimension)})
        for value in values:
            groups[(dimension, value)] = [row for row in features if row.get(dimension) == value]
    result = []
    for (dimension, group), rows in groups.items():
        for feature in numeric_features:
            pairs = []
            for row in rows:
                x, y = _float(row.get(feature)), _float(row.get("victory_score"))
                if x is not None and y is not None:
                    pairs.append((x, y))
            if len(pairs) < minimum:
                continue
            value = pearson(pairs)
            if value is not None:
                result.append({
                    "dimension": dimension, "group": group, "feature": feature,
                    "samples": len(pairs), "correlation_with_victory_score": round(value, 8),
                })
    return sorted(result, key=lambda row: (row["dimension"], row["group"], -abs(row["correlation_with_victory_score"])))


def strategy_effect_rows(
    features: list[dict[str, str]], events: list[dict[str, str]], minimum: int
) -> list[dict[str, Any]]:
    feature_index = {
        (row.get("_source"), row.get("match_id"), row.get("team"), row.get("game_type")): row for row in features
    }
    present: dict[tuple[str, str, str], set[tuple[str, str, str, str]]] = defaultdict(set)
    for event in events:
        participant = (event.get("_source", ""), event.get("match_id", ""), event.get("team", ""), event.get("game_type", ""))
        if participant in feature_index:
            present[(event.get("event", ""), event.get("game_type", ""), event.get("map", ""))].add(participant)
            present[(event.get("event", ""), "all", "all")].add(participant)
    all_participants = set(feature_index)
    result = []
    for (event_name, game_type, map_name), with_event in sorted(present.items()):
        eligible = {
            key for key in all_participants
            if (game_type == "all" or key[3] == game_type)
            and (map_name == "all" or feature_index[key].get("map") == map_name)
        }
        without_event = eligible - with_event
        with_scores = [_float(feature_index[key].get("victory_score")) for key in with_event]
        without_scores = [_float(feature_index[key].get("victory_score")) for key in without_event]
        left = [value for value in with_scores if value is not None]
        right = [value for value in without_scores if value is not None]
        if len(left) < max(2, minimum // 2) or len(right) < max(2, minimum // 2):
            continue
        result.append({
            "event": event_name, "game_type": game_type, "map": map_name,
            "participants_with_event": len(left), "participants_without_event": len(right),
            "mean_score_with_event": round(mean(left), 6),
            "mean_score_without_event": round(mean(right), 6),
            "score_difference": round(mean(left) - mean(right), 6),
        })
    return sorted(result, key=lambda row: -abs(row["score_difference"]))


def write_csv(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def analyze(inputs: list[Path], output_dir: Path, minimum: int = 8) -> dict[str, int]:
    features = _read_rows(inputs, "game_features.csv")
    if not features:
        raise RuntimeError("no game_features.csv rows found; regenerate reports with the redesigned runner")
    events = _read_rows(inputs, "strategy_events.csv")
    correlations = correlation_rows(features, minimum)
    effects = strategy_effect_rows(features, events, minimum)
    output_dir.mkdir(parents=True, exist_ok=True)
    write_csv(
        output_dir / "correlations.csv", correlations,
        ["dimension", "group", "feature", "samples", "correlation_with_victory_score"],
    )
    write_csv(
        output_dir / "strategy_effects.csv", effects,
        ["event", "game_type", "map", "participants_with_event", "participants_without_event",
         "mean_score_with_event", "mean_score_without_event", "score_difference"],
    )
    overall = [row for row in correlations if row["dimension"] == "overall"][:15]
    lines = [
        "# Nicowar tournament exploratory analysis", "",
        f"- Participant/game observations: {len(features)}",
        f"- Game formats: {', '.join(sorted({row['game_type'] for row in features}))}",
        f"- Maps: {len({row['map'] for row in features})}",
        f"- Minimum samples per correlation: {minimum}", "",
        "Associations below are exploratory. Correlation and event presence do not establish that a strategy caused the result; use paired ablations to confirm candidates.", "",
        "## Strongest overall feature associations", "",
        "| Feature | Samples | Correlation with victory score |", "|---|---:|---:|",
    ]
    for row in overall:
        lines.append(f"| {row['feature']} | {row['samples']} | {row['correlation_with_victory_score']:+.3f} |")
    lines.extend(["", "## Largest strategy-event associations", "",
                  "| Event | Format | Map | With/without | Score difference |", "|---|---|---|---:|---:|"])
    for row in effects[:15]:
        lines.append(
            f"| {row['event']} | {row['game_type']} | {row['map']} | "
            f"{row['participants_with_event']}/{row['participants_without_event']} | {row['score_difference']:+.2f} |"
        )
    lines.extend(["", "Full machine-readable results are in `correlations.csv` and `strategy_effects.csv`.", ""])
    (output_dir / "analysis.md").write_text("\n".join(lines), encoding="utf-8")
    return {"features": len(features), "correlations": len(correlations), "strategy_effects": len(effects)}


def main() -> int:
    args = parse_args()
    first = args.inputs[0]
    output_dir = args.output_dir or (first if first.is_dir() else first.parent)
    try:
        counts = analyze(args.inputs, output_dir, args.min_samples)
    except (OSError, RuntimeError) as error:
        print(f"Analysis failed: {error}")
        return 2
    print(
        f"Analyzed {counts['features']} participant-games; wrote {counts['correlations']} "
        f"correlations and {counts['strategy_effects']} strategy-event comparisons to {output_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
