#!/usr/bin/env python3
"""Continuous tournament scoring and analysis-ready feature extraction.

The public score is zero-sum and ranges from -100 to +100.  A decisive result
anchors its sign; speed and the time-integrated state trajectory determine how
large the victory or loss was.  Draws and step-limit games receive only the
continuous dominance component, so they still carry useful optimization signal.
"""

from __future__ import annotations

import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean
from typing import Any, Iterable


SCORE_PREFIX = "NICOWAR_SCORE_TELEMETRY\t"
SIGNAL_VERSION = 1
SCORE_MIN = -100.0
SCORE_MAX = 100.0
OUTCOME_WEIGHT = 55.0
SPEED_WEIGHT = 15.0
DOMINANCE_WEIGHT = 30.0

CORE_METRICS = (
    "population", "workers", "explorers", "warriors", "buildings",
    "building_sites", "food", "food_capacity", "total_hp", "attack_power",
    "defense_power", "prestige", "alive",
)


def _number(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _coerce(value: str) -> int | float | str:
    try:
        return int(value)
    except ValueError:
        try:
            return float(value)
        except ValueError:
            return value


def parse_score_line(line: str) -> dict[str, Any] | None:
    if not line.startswith(SCORE_PREFIX):
        return None
    fields = line.split("\t")
    if len(fields) < 5:
        return None
    values: dict[str, Any] = {}
    for field in fields[4:]:
        if "=" in field:
            key, value = field.split("=", 1)
            values[key] = _coerce(value)
    return {
        "tick": int(fields[1]),
        "team": int(fields[2]),
        "version": fields[3],
        **values,
    }


def symmetric_margin(left: float, right: float, stabilizer: float) -> float:
    """Bound a scale-free difference to [-1, 1], including the zero/zero case."""
    return max(-1.0, min(1.0, (left - right) / (abs(left) + abs(right) + stabilizer)))


def aggregate_snapshot(snapshots: Iterable[dict[str, Any]]) -> dict[str, float]:
    rows = list(snapshots)
    result = {metric: sum(_number(row.get(metric)) for row in rows) for metric in CORE_METRICS}
    result["alive"] = sum(_number(row.get("alive"), 1.0) for row in rows)
    return result


def component_advantages(left: dict[str, Any], right: dict[str, Any]) -> dict[str, float]:
    economy = (
        0.50 * symmetric_margin(_number(left.get("workers")), _number(right.get("workers")), 4.0)
        + 0.30 * symmetric_margin(_number(left.get("buildings")), _number(right.get("buildings")), 2.0)
        + 0.10 * symmetric_margin(_number(left.get("building_sites")), _number(right.get("building_sites")), 1.0)
        + 0.10 * symmetric_margin(_number(left.get("food_capacity")), _number(right.get("food_capacity")), 20.0)
    )
    military = (
        0.45 * symmetric_margin(_number(left.get("warriors")), _number(right.get("warriors")), 3.0)
        + 0.35 * symmetric_margin(_number(left.get("attack_power")), _number(right.get("attack_power")), 100.0)
        + 0.20 * symmetric_margin(_number(left.get("defense_power")), _number(right.get("defense_power")), 100.0)
    )
    resilience = (
        0.75 * symmetric_margin(_number(left.get("total_hp")), _number(right.get("total_hp")), 250.0)
        + 0.25 * symmetric_margin(_number(left.get("alive")), _number(right.get("alive")), 1.0)
    )
    prestige = symmetric_margin(
        _number(left.get("prestige")), _number(right.get("prestige")), 1.0
    )
    dominance = 0.35 * economy + 0.40 * military + 0.15 * resilience + 0.10 * prestige
    return {
        "economy": economy,
        "military": military,
        "resilience": resilience,
        "prestige": prestige,
        "dominance": max(-1.0, min(1.0, dominance)),
    }


def _weighted_average(series: list[tuple[int, float]], final_tick: int) -> float:
    if not series:
        return 0.0
    ordered = sorted(series)
    if len(ordered) == 1:
        return ordered[0][1]
    total = 0.0
    weight = 0.0
    for index, (tick, value) in enumerate(ordered):
        next_tick = ordered[index + 1][0] if index + 1 < len(ordered) else final_tick
        duration = max(1, next_tick - tick)
        total += value * duration
        weight += duration
    return total / weight if weight else mean(value for _, value in ordered)


def _fallback_trajectory(match: dict[str, Any]) -> list[dict[str, Any]]:
    tick = int(match.get("steps", 0))
    rows = []
    for player in match.get("players", []):
        rows.append({
            "tick": tick,
            "team": player["team"],
            "version": player.get("version", "unknown"),
            "alive": int(bool(player.get("alive"))),
            "won": int(bool(player.get("won"))),
            "lost": int(bool(player.get("lost"))),
            "population": player.get("units", 0),
            "workers": player.get("workers", player.get("units", 0)),
            "explorers": player.get("explorers", 0),
            "warriors": player.get("warriors", 0),
            "buildings": player.get("buildings", 0),
            "building_sites": 0,
            "food": 0,
            "food_capacity": 0,
            "total_hp": 0,
            "attack_power": 0,
            "defense_power": 0,
            "prestige": player.get("prestige", 0),
        })
    return rows


def trajectory(match: dict[str, Any]) -> tuple[list[dict[str, Any]], str]:
    rows = match.get("score_telemetry") or match.get("observer") or []
    if rows:
        # Resume files can contain a repeated scheduled/final sample at one tick.
        unique = {(int(row["tick"]), int(row["team"])): row for row in rows}
        return sorted(unique.values(), key=lambda row: (int(row["tick"]), int(row["team"]))), "trajectory"
    return _fallback_trajectory(match), "final_state_fallback"


def pair_score(
    match: dict[str, Any], left_teams: set[int], right_teams: set[int], outcome: int
) -> dict[str, Any]:
    rows, quality = trajectory(match)
    by_tick: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_tick[int(row["tick"])].append(row)
    components: dict[str, list[tuple[int, float]]] = defaultdict(list)
    for tick, tick_rows in sorted(by_tick.items()):
        left = aggregate_snapshot(row for row in tick_rows if int(row["team"]) in left_teams)
        right = aggregate_snapshot(row for row in tick_rows if int(row["team"]) in right_teams)
        if not left_teams or not right_teams:
            continue
        values = component_advantages(left, right)
        for name, value in values.items():
            components[name].append((tick, value))
    steps = max(1, int(match.get("steps", 0)))
    averages = {name: _weighted_average(values, steps) for name, values in components.items()}
    dominance = averages.get("dominance", 0.0)
    duration_fraction = min(1.0, steps / max(1, int(match.get("max_steps", steps))))
    speed = float(outcome) * (1.0 - duration_fraction)
    score = OUTCOME_WEIGHT * outcome + SPEED_WEIGHT * speed + DOMINANCE_WEIGHT * dominance
    if outcome > 0:
        score = max(5.0, score)
    elif outcome < 0:
        score = min(-5.0, score)
    score = max(SCORE_MIN, min(SCORE_MAX, score))

    dominance_series = components.get("dominance", [])
    signs = []
    for _, value in dominance_series:
        sign = 1 if value > 0.05 else (-1 if value < -0.05 else 0)
        if sign and (not signs or sign != signs[-1]):
            signs.append(sign)
    volatility = (
        mean(abs(dominance_series[index][1] - dominance_series[index - 1][1])
             for index in range(1, len(dominance_series)))
        if len(dominance_series) > 1 else 0.0
    )
    early_values = [value for tick, value in dominance_series if tick <= steps * 0.60]
    early_lead = mean(early_values) if early_values else dominance
    comeback = bool(outcome and early_lead * outcome < -0.05)
    return {
        "victory_score": round(score, 6),
        "normalized_score": round((score - SCORE_MIN) / (SCORE_MAX - SCORE_MIN), 8),
        "outcome_component": round(OUTCOME_WEIGHT * outcome, 6),
        "speed_component": round(SPEED_WEIGHT * speed, 6),
        "dominance_component": round(DOMINANCE_WEIGHT * dominance, 6),
        "economy_advantage": round(averages.get("economy", 0.0), 8),
        "military_advantage": round(averages.get("military", 0.0), 8),
        "resilience_advantage": round(averages.get("resilience", 0.0), 8),
        "prestige_advantage": round(averages.get("prestige", 0.0), 8),
        "duration_fraction": round(duration_fraction, 8),
        "lead_changes": max(0, len(signs) - 1),
        "dominance_volatility": round(volatility, 8),
        "comeback": comeback,
        "signal_quality": quality,
        "signal_version": SIGNAL_VERSION,
    }


def _pair_outcome(left: dict[str, Any], right: dict[str, Any]) -> int:
    if bool(left.get("won")) != bool(right.get("won")):
        return 1 if left.get("won") else -1
    if bool(left.get("lost")) != bool(right.get("lost")):
        return -1 if left.get("lost") else 1
    return 0


def enrich_ffa_match(match: dict[str, Any]) -> dict[str, Any]:
    players = match.get("players", [])
    if match.get("score_version") == SIGNAL_VERSION and all(
        "victory_score" in player for player in players
    ):
        return match
    for player in players:
        comparisons = [
            pair_score(match, {int(player["team"])}, {int(other["team"])}, _pair_outcome(player, other))
            for other in players if other is not player
        ]
        if not comparisons:
            continue
        for key in (
            "victory_score", "normalized_score", "outcome_component", "speed_component",
            "dominance_component", "economy_advantage", "military_advantage",
            "resilience_advantage", "prestige_advantage", "duration_fraction",
            "lead_changes", "dominance_volatility",
        ):
            player[key] = round(mean(_number(item[key]) for item in comparisons), 8)
        player["comeback"] = any(item["comeback"] for item in comparisons)
        player["signal_quality"] = comparisons[0]["signal_quality"]
        player["signal_version"] = SIGNAL_VERSION
    match["score_version"] = SIGNAL_VERSION
    return match


def enrich_2v2_match(match: dict[str, Any]) -> dict[str, Any]:
    if match.get("score_version") == SIGNAL_VERSION and "score" in match:
        return match
    side_a = {int(player["team"]) for player in match.get("players", []) if player.get("side") == "A"}
    side_b = {int(player["team"]) for player in match.get("players", []) if player.get("side") == "B"}
    winner = match.get("winner_side", "draw")
    outcome = 1 if winner == "A" else (-1 if winner == "B" else 0)
    score = pair_score(match, side_a, side_b, outcome)
    match["score"] = score
    match["victory_score_a"] = score["victory_score"]
    match["victory_score_b"] = -score["victory_score"]
    match["score_version"] = SIGNAL_VERSION
    for player in match.get("players", []):
        direction = 1.0 if player.get("side") == "A" else -1.0
        player["victory_score"] = round(direction * score["victory_score"], 8)
        player["normalized_score"] = round((player["victory_score"] + 100.0) / 200.0, 8)
        player["economy_advantage"] = round(direction * score["economy_advantage"], 8)
        player["military_advantage"] = round(direction * score["military_advantage"], 8)
        player["lead_changes"] = score["lead_changes"]
        player["dominance_volatility"] = score["dominance_volatility"]
        player["comeback"] = score["comeback"] if direction > 0 else False
        player["signal_quality"] = score["signal_quality"]
        player["signal_version"] = SIGNAL_VERSION
    return match


def _phase(tick: int, steps: int) -> str:
    fraction = tick / max(1, steps)
    return "early" if fraction <= 1 / 3 else ("mid" if fraction <= 2 / 3 else "late")


def participant_feature_rows(match: dict[str, Any], game_type: str) -> list[dict[str, Any]]:
    if game_type == "2v2" or match.get("kind") == "2v2":
        enrich_2v2_match(match)
    else:
        enrich_ffa_match(match)
    rows, quality = trajectory(match)
    by_team: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for snapshot in rows:
        by_team[int(snapshot["team"])].append(snapshot)
    event_counts = Counter(int(event["team"]) for event in match.get("telemetry", []))
    features = []
    for player in match.get("players", []):
        team = int(player["team"])
        snapshots = sorted(by_team.get(team, []), key=lambda item: int(item["tick"]))
        row: dict[str, Any] = {
            "match_id": match.get("id"), "block": match.get("block"),
            "round": match.get("round"), "game_type": game_type,
            "map": match.get("map"), "map_file": match.get("map_file"),
            "seed": match.get("seed"), "team": team,
            "version": player.get("version"), "role": player.get("role", player.get("side", "player")),
            "steps": match.get("steps", 0), "max_steps": match.get("max_steps", 0),
            "engine_status": match.get("engine_status"), "won": int(bool(player.get("won"))),
            "lost": int(bool(player.get("lost"))), "alive": int(bool(player.get("alive"))),
            "victory_score": player.get("victory_score", 0.0),
            "normalized_score": player.get("normalized_score", 0.5),
            "economy_advantage": player.get("economy_advantage", 0.0),
            "military_advantage": player.get("military_advantage", 0.0),
            "lead_changes": player.get("lead_changes", 0),
            "dominance_volatility": player.get("dominance_volatility", 0.0),
            "comeback": int(bool(player.get("comeback"))),
            "signal_quality": player.get("signal_quality", quality),
            "strategy_events": event_counts[team],
            "players_in_game": len(match.get("players", [])),
        }
        for metric in CORE_METRICS:
            values = [_number(snapshot.get(metric)) for snapshot in snapshots]
            if not values:
                continue
            row[f"final_{metric}"] = values[-1]
            row[f"average_{metric}"] = round(mean(values), 6)
            row[f"peak_{metric}"] = max(values)
            for phase in ("early", "mid", "late"):
                phase_values = [
                    _number(snapshot.get(metric)) for snapshot in snapshots
                    if _phase(int(snapshot["tick"]), int(match.get("steps", 0))) == phase
                ]
                row[f"{phase}_{metric}"] = round(mean(phase_values), 6) if phase_values else ""
        if snapshots:
            row["map_width"] = snapshots[-1].get("map_width", "")
            row["map_height"] = snapshots[-1].get("map_height", "")
            width, height = _number(row["map_width"]), _number(row["map_height"])
            if width and height:
                row["map_area"] = width * height
                row["tiles_per_player"] = round(
                    width * height / max(1, len(match.get("players", []))), 4
                )
        features.append(row)
    return features


def strategy_event_rows(match: dict[str, Any], game_type: str) -> list[dict[str, Any]]:
    grouped: dict[tuple[int, str, str], list[dict[str, Any]]] = defaultdict(list)
    for event in match.get("telemetry", []):
        grouped[(int(event["team"]), str(event.get("ai_version", "")), str(event["event"]))].append(event)
    rows = []
    for (team, version, event_name), events in sorted(grouped.items()):
        categorical: dict[str, Counter[str]] = defaultdict(Counter)
        for event in events:
            for key, value in event.items():
                if key not in {"tick", "team", "ai_version", "event"} and not str(value).replace("-", "", 1).replace(".", "", 1).isdigit():
                    categorical[key][str(value)] += 1
        rows.append({
            "match_id": match.get("id"), "block": match.get("block"),
            "round": match.get("round"), "game_type": game_type, "map": match.get("map"),
            "seed": match.get("seed"), "team": team, "version": version,
            "event": event_name, "count": len(events),
            "first_tick": min(int(event["tick"]) for event in events),
            "last_tick": max(int(event["tick"]) for event in events),
            "categorical_modes": json.dumps(
                {key: counts.most_common(1)[0][0] for key, counts in sorted(categorical.items())},
                sort_keys=True,
            ),
        })
    return rows


def _write_dynamic_csv(path: Path, rows: list[dict[str, Any]], preferred: list[str]) -> None:
    extras = sorted({key for row in rows for key in row} - set(preferred))
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=preferred + extras, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def write_analysis_tables(output_dir: Path, results: list[dict[str, Any]], game_type: str) -> dict[str, int]:
    trajectory_rows: list[dict[str, Any]] = []
    feature_rows: list[dict[str, Any]] = []
    event_rows: list[dict[str, Any]] = []
    for match in results:
        if match.get("status") != "completed":
            continue
        match_game_type = str(match.get("format", game_type))
        feature_rows.extend(participant_feature_rows(match, match_game_type))
        event_rows.extend(strategy_event_rows(match, match_game_type))
        for snapshot in trajectory(match)[0]:
            trajectory_rows.append({
                "match_id": match.get("id"), "block": match.get("block"),
                "round": match.get("round"), "game_type": match_game_type,
                "map": match.get("map"), "seed": match.get("seed"), **snapshot,
            })
    _write_dynamic_csv(
        output_dir / "trajectories.csv", trajectory_rows,
        ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "version"],
    )
    _write_dynamic_csv(
        output_dir / "game_features.csv", feature_rows,
        ["match_id", "block", "round", "game_type", "map", "seed", "team", "version", "role", "victory_score"],
    )
    _write_dynamic_csv(
        output_dir / "strategy_events.csv", event_rows,
        ["match_id", "block", "round", "game_type", "map", "seed", "team", "version", "event", "count", "first_tick", "last_tick"],
    )
    return {
        "trajectories": len(trajectory_rows),
        "game_features": len(feature_rows),
        "strategy_events": len(event_rows),
    }


def write_raw_telemetry_tables(output_dir: Path, results: list[dict[str, Any]], game_type: str) -> None:
    events: list[dict[str, Any]] = []
    observer: list[dict[str, Any]] = []
    for match in results:
        context = {
            "match_id": match.get("id"), "block": match.get("block"),
            "round": match.get("round"), "game_type": match.get("format", game_type),
            "map": match.get("map"), "seed": match.get("seed"),
        }
        events.extend({**context, **event} for event in match.get("telemetry", []))
        observer.extend({**context, **snapshot} for snapshot in match.get("observer", []))
    _write_dynamic_csv(
        output_dir / "telemetry.csv", events,
        ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "ai_version", "event"],
    )
    _write_dynamic_csv(
        output_dir / "observer.csv", observer,
        ["match_id", "block", "round", "game_type", "map", "seed", "tick", "team", "version"],
    )
