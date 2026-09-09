#!/usr/bin/env python3
"""Build a stratified pre-trigger manifest from Maxima harvest logs and saves."""

from __future__ import annotations

import argparse
import json
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path
from typing import Any

import run_maxima_switch_ablation as ablation


def fields(line: str) -> dict[str, str]:
    result = {}
    for item in line.rstrip().split("\t")[1:]:
        if "=" in item:
            key, value = item.split("=", 1)
            result[key] = value
    return result


def parse_log(path: Path, max_lead: int) -> list[dict[str, Any]]:
    saves: list[tuple[int, str, str, str]] = []
    opportunities = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("MAXIMA_CHECKPOINT_SAVED\t"):
            row = fields(line)
            saves.append((int(row["tick"]), row["path"], row.get("checksum", ""), row.get("rng", "")))
        elif line.startswith("MAXIMA_ABLATION_OPPORTUNITY\t"):
            opportunities.append(fields(line))
    saves.sort()
    ticks = [row[0] for row in saves]
    candidates = []
    seen_switches = set()
    for event in opportunities:
        switch = event.get("switch", "")
        # One state per switch per source game is the independent default.
        if switch not in ablation.ALL_SWITCHES or switch in seen_switches:
            continue
        opportunity_tick = int(event["tick"])
        index = bisect_right(ticks, opportunity_tick) - 1
        if index < 0:
            continue
        checkpoint_tick, checkpoint_path, checksum, rng = saves[index]
        if opportunity_tick - checkpoint_tick > max_lead:
            continue
        checkpoint = Path(checkpoint_path)
        if not checkpoint.is_absolute():
            checkpoint = (path.parent / checkpoint).resolve()
        if not checkpoint.is_file():
            continue
        seen_switches.add(switch)
        metric = ablation.default_metric(switch)
        candidates.append({
            "checkpoint_id": f"{path.stem}-{switch.replace('.', '-')}-{checkpoint_tick}",
            "path": str(checkpoint), "source_block": path.stem,
            "focal_player": int(event["player"]), "focal_team": int(event["team"]),
            "switch": switch, "horizon": 20000, "metric": metric,
            "mpid": ablation.SCORE_METRICS[metric],
            "checkpoint_tick": checkpoint_tick, "opportunity_tick": opportunity_tick,
            "lead_ticks": opportunity_tick - checkpoint_tick,
            "severity": int(event.get("severity", 0)),
            "context_hash": event.get("context_hash", ""),
            "save_checksum": checksum, "rng_checksum": rng,
            "source_log": str(path.resolve()),
        })
    return candidates


def severity_bins(rows: list[dict[str, Any]]) -> None:
    by_switch: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_switch[row["switch"]].append(row)
    for values in by_switch.values():
        ordered = sorted(values, key=lambda row: (row["severity"], row["source_block"]))
        size = len(ordered)
        for index, row in enumerate(ordered):
            row["severity_bin"] = min(2, (3 * index) // max(1, size))


def select(rows: list[dict[str, Any]], quota: int) -> list[dict[str, Any]]:
    severity_bins(rows)
    by_switch_bin: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_switch_bin[(row["switch"], row["severity_bin"])].append(row)
    selected = []
    for switch in ablation.ALL_SWITCHES:
        bins = [sorted(by_switch_bin[(switch, level)],
                       key=lambda row: (row["source_block"], row["checkpoint_tick"]))
                for level in range(3)]
        while len([row for row in selected if row["switch"] == switch]) < quota:
            advanced = False
            for values in bins:
                if values and len([row for row in selected if row["switch"] == switch]) < quota:
                    selected.append(values.pop(0)); advanced = True
            if not advanced:
                break
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--quota", type=int, default=40)
    parser.add_argument("--max-lead", type=int, default=1000)
    args = parser.parse_args()
    if args.quota < 1 or args.max_lead < 0:
        parser.error("quota must be positive and max-lead nonnegative")
    candidates = []
    for log in args.logs:
        candidates.extend(parse_log(log.resolve(), args.max_lead))
    chosen = select(candidates, args.quota)
    coverage = {}
    for switch in ablation.ALL_SWITCHES:
        available = [row for row in candidates if row["switch"] == switch]
        retained = [row for row in chosen if row["switch"] == switch]
        coverage[switch] = {
            "source_games_available": len(available),
            "source_games_retained": len(retained),
            "quota": args.quota,
        }
    payload = {
        "schema_version": 1,
        "selection": "one pre-trigger checkpoint per switch/source; round-robin severity tertiles",
        "checkpoint_count": len(chosen), "coverage": coverage,
        "checkpoints": chosen,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(chosen)} checkpoints to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
