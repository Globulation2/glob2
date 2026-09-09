#!/usr/bin/env python3
"""Run a paired 2x2 factorial of Maxima construction and birth throughput."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import subprocess
import sys
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean
from typing import Any

import optimize_maxima_portfolio as portfolio
import run_maxima_all_formats_tournament as tournament


MAPS = {
    "duel": ("strange2", "Isles", "Garden 3", "A big pond"),
    "ffa3": ("Migration", "Isles", "Garden 3", "Sand River"),
    "ffa4": ("Migration", "Isles", "Garden 3", "Sand River"),
    "ffa5": ("Island of the Renfur", "Centerfolds 2", "Sand River", "Playground"),
    "2v2": ("Migration", "Isles", "FourSquares1", "G2"),
}

CONSTRUCTION = {
    # Raise site concurrency together with per-site staffing. The preceding
    # all-map confirmation showed that raising staffing alone left Maxima with
    # too few simultaneous sites to approach Nicowar's construction throughput.
    "construction.sites_low": 3,
    "construction.sites_mid": 4,
    "construction.sites_high": 5,
    "construction.growth_utility_high_sites": 5,
    "construction.growth_utility_mid_sites": 4,
    "construction.growth_utility_low_sites": 3,
    "construction.policy_high_sites": 4,
    "construction.policy_low_sites": 3,
    "staffing.construction_inn_workers": 3,
    "staffing.construction_swarm_workers": 3,
    "staffing.construction_training_workers": 6,
    "staffing.construction_hospital_workers": 5,
    "staffing.construction_large_workers": 9,
}

BIRTH = {
    # Growth utility can add three workers and low local corn can double the
    # result. Seven is therefore the highest safe base under the engine's hard
    # per-building assignment limit of 20.
    "economy.committed_swarm_workers": 14,
    "economy.swarm_workers_per_building_tight": 5,
    "economy.swarm_workers_per_building_healthy": 6,
    "economy.swarm_workers_per_building_surplus": 8,
}

INN = {
    "staffing.inn_level1_normal_workers": 2,
    "staffing.inn_level2_normal_workers": 5,
    "staffing.inn_level3_normal_workers": 8,
    "staffing.inn_level1_low_corn_workers": 3,
    "staffing.inn_level2_low_corn_workers": 8,
    "staffing.inn_level3_low_corn_workers": 11,
}

FACTORS = (("construction", CONSTRUCTION), ("birth", BIRTH))
BASE_PARAMETERS: dict[str, int] = {}
EXPERIMENT_TITLE = "Maxima broad-staffing factorial ablation"
EXPERIMENT_DESCRIPTION = (
    "Each raised factor changes a coherent throughput bundle. The complete "
    "factorial design estimates each change both alone and in combination."
)
OUTPUT_STEM = "maxima-staffing-ablation"
CHECKPOINTS = (5000, 10000, 20000, 30000)
FIELDS = (
    "workers", "population", "buildings", "building_sites", "free_workers",
    "worker_jobs_open", "assigned_worker_slots", "construction_assigned",
    "construction_working", "swarm_assigned", "swarm_working", "inn_assigned",
    "inn_working", "technology_assigned", "technology_working",
    "military_assigned", "military_working", "warriors", "hungry",
)


def override_string(parameters: dict[str, int]) -> str:
    """Serialize the explicitly supplied ablation keys without optimizer globals."""
    return ",".join(f"{key}={value}" for key, value in parameters.items())


class DirectSSHPortfolioCluster(portfolio.PortfolioCluster):
    """Use bounded independent SSH sessions for telemetry-heavy matches.

    Per-slot control masters can fail to deliver channel EOF after a verbose
    remote match exits. Independent sessions cost one authentication per match,
    but ServerAlive bounds a broken transport and makes completion observable.
    """

    def _ssh_arguments(self, host: str, group: int) -> list[str]:
        del group
        return [
            "ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8",
            "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=2",
            "-o", "ControlMaster=no", "-o", "ControlPath=none",
        ]

    def _prepare_connection(self, host: str, group: int) -> bool:
        if host == "local":
            return True
        try:
            process = subprocess.run(
                self._ssh_arguments(host, group) + [host, "true"],
                text=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=15,
            )
        except (OSError, subprocess.TimeoutExpired):
            return False
        return process.returncode == 0

    def _reset_connection(self, host: str, group: int) -> None:
        del host, group


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build-tournament/src/glob2")
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--seed", type=int, default=0x53544146)
    parser.add_argument("--max-steps", type=int, default=120000)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--worker", action="append", type=portfolio.parse_worker, dest="workers")
    parser.add_argument("--remote-root", default="/home/bradley/glob2-optimizer")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-telemetry", action="store_true")
    args = parser.parse_args()
    if not args.workers:
        args.workers = list(portfolio.STANDARD_WORKERS)
    if min(args.rounds, args.max_steps, args.timeout) < 1 or args.retries < 0:
        parser.error("rounds, max-steps, and timeout must be positive; retries must be nonnegative")
    return args


def variants() -> list[dict[str, Any]]:
    result = []
    for mask in range(1 << len(FACTORS)):
        enabled = [name for index, (name, _) in enumerate(FACTORS) if mask & (1 << index)]
        parameters: dict[str, int] = dict(BASE_PARAMETERS)
        for index, (_, values) in enumerate(FACTORS):
            if mask & (1 << index):
                parameters.update(values)
        result.append({
            "id": mask,
            "name": "+".join(enabled) if enabled else "baseline",
            "factors": enabled,
            "parameters": parameters,
            "overrides": override_string(parameters),
        })
    return result


def select_maps(binary: Path, root: Path) -> dict[str, list[dict[str, Any]]]:
    discovered = portfolio.discover_maps(binary, root)
    by_name = {item["name"].casefold(): item for item in discovered}
    selected: dict[str, list[dict[str, Any]]] = {}
    for format_name, names in MAPS.items():
        missing = [name for name in names if name.casefold() not in by_name]
        if missing:
            raise RuntimeError(f"missing {format_name} ablation maps: {missing}")
        selected[format_name] = [by_name[name.casefold()] for name in names]
    return selected


def validate(binary: Path, configurations: list[dict[str, Any]]) -> None:
    for configuration in configurations:
        if not configuration["parameters"]:
            continue
        for format_name in portfolio.FORMATS:
            runtime_format = "ffa5plus" if format_name == "ffa5" else format_name
            subprocess.run(
                [str(binary), "--dump-maxima-strategy", "--maxima-format", runtime_format,
                 "--maxima-overrides", configuration["overrides"]],
                check=True, text=True, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )


def outcomes(results: list[dict[str, Any]]) -> Counter[str]:
    return Counter(tournament.outcome(result) for result in results)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def nearest_rows(observer: list[dict[str, Any]], checkpoint: int) -> list[dict[str, Any]]:
    if not observer:
        return []
    ticks = sorted({int(row["tick"]) for row in observer})
    tick = min(ticks, key=lambda value: abs(value - checkpoint))
    if abs(tick - checkpoint) > 500:
        return []
    return [row for row in observer if int(row["tick"]) == tick]


def checkpoint_rows(spool: Path) -> list[dict[str, Any]]:
    samples: dict[tuple[str, str, int, str], list[tuple[float, float]]] = defaultdict(list)
    for path in sorted(spool.glob("match-*.json.gz")):
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            match = json.load(handle)
        if match.get("status") != "completed":
            continue
        observer = match.get("observer", [])
        for checkpoint in CHECKPOINTS:
            rows = nearest_rows(observer, checkpoint)
            maxima = [row for row in rows if row.get("version") == "Maxima"]
            nicowar = [row for row in rows if row.get("version") == "Nicowar"]
            if not maxima or not nicowar:
                continue
            for field in FIELDS:
                try:
                    maxima_value = mean(float(row[field]) for row in maxima)
                    nicowar_value = mean(float(row[field]) for row in nicowar)
                except (KeyError, ValueError):
                    continue
                samples[(match["variant"], match["format"], checkpoint, field)].append(
                    (maxima_value, nicowar_value)
                )
    rows = []
    for (variant, format_name, checkpoint, field), values in sorted(samples.items()):
        maxima_value = mean(value[0] for value in values)
        nicowar_value = mean(value[1] for value in values)
        rows.append({
            "variant": variant,
            "format": format_name,
            "checkpoint": checkpoint,
            "field": field,
            "matches": len(values),
            "maxima": round(maxima_value, 6),
            "nicowar": round(nicowar_value, 6),
            "delta": round(maxima_value - nicowar_value, 6),
            "ratio": round(maxima_value / nicowar_value, 6) if nicowar_value else "",
        })
    return rows


def resume_results(spool: Path, games_per_variant: int,
                   variant_count: int) -> tuple[dict[int, list[dict[str, Any]]], set[int]]:
    results: dict[int, list[dict[str, Any]]] = {
        index: [] for index in range(variant_count)
    }
    completed_ids: set[int] = set()
    for path in sorted(spool.glob("match-*.json.gz")):
        try:
            with gzip.open(path, "rt", encoding="utf-8") as handle:
                full = json.load(handle)
        except (EOFError, json.JSONDecodeError):
            print(f"Ignoring incomplete spool file during resume: {path.name}", flush=True)
            continue
        match_id = int(full["id"])
        variant_id = (match_id - 1) // games_per_variant
        if full.get("status") == "completed":
            compact = portfolio.compact_results([full])[0]
            results[variant_id].append(compact)
            completed_ids.add(match_id)
        else:
            print(f"Rerunning failed spool file: {path.name}", flush=True)
    return results, completed_ids


def factor_effects(configurations: list[dict[str, Any]], scores: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for factor, _ in FACTORS:
        high = [scores[item["name"]] for item in configurations if factor in item["factors"]]
        low = [scores[item["name"]] for item in configurations if factor not in item["factors"]]
        for metric in ("portfolio_score", "objective", "worst_format_score"):
            high_value = mean(float(item[metric]) for item in high)
            low_value = mean(float(item[metric]) for item in low)
            rows.append({
                "factor": factor,
                "metric": metric,
                "high_mean": round(high_value, 8),
                "low_mean": round(low_value, 8),
                "main_effect": round(high_value - low_value, 8),
            })
    return rows


def write_summary(output: Path, configurations: list[dict[str, Any]],
                  scores: dict[str, dict[str, Any]], results: dict[int, list[dict[str, Any]]],
                  effects: list[dict[str, Any]], elapsed: float, games_per_variant: int) -> None:
    ranked = sorted(configurations, key=lambda item: scores[item["name"]]["objective"], reverse=True)
    lines = [
        f"# {EXPERIMENT_TITLE}", "",
        f"- Variants: {len(configurations)}", f"- Games per variant: {games_per_variant}",
        f"- Total games: {sum(len(value) for value in results.values())}",
        f"- Wall time: {elapsed:.1f} seconds", "",
        EXPERIMENT_DESCRIPTION, "",
        "## Ranked result", "",
        "| Variant | Factors raised | W-L-D | Portfolio | Worst format | Objective |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for item in ranked:
        score = scores[item["name"]]
        counts = outcomes(results[item["id"]])
        lines.append(
            f"| {item['name']} | {', '.join(item['factors']) or '—'} | "
            f"{counts['win']}-{counts['loss']}-{counts['draw']} | "
            f"{score['portfolio_score']:.1%} | {score['worst_format_score']:.1%} | {score['objective']:.4f} |"
        )
    lines.extend(["", "## Factorial main effects", "",
        "Positive means enabling the factor improved the metric after averaging across the other factors.", "",
                  "| Factor | Portfolio effect | Worst-format effect | Objective effect |",
                  "|---|---:|---:|---:|"])
    by_factor = {(row["factor"], row["metric"]): row["main_effect"] for row in effects}
    for factor, _ in FACTORS:
        lines.append(
            f"| {factor} | {by_factor[(factor, 'portfolio_score')]:+.1%} | "
            f"{by_factor[(factor, 'worst_format_score')]:+.1%} | "
            f"{by_factor[(factor, 'objective')]:+.4f} |"
        )
    lines.extend(["", "## Per-format scores", "",
                  "| Variant | 2v2 | Duel | FFA3 | FFA4 | FFA5 |",
                  "|---|---:|---:|---:|---:|---:|"])
    for item in ranked:
        formats = scores[item["name"]]["formats"]
        lines.append(
            f"| {item['name']} | {formats['2v2']['score']:.1%} | {formats['duel']['score']:.1%} | "
            f"{formats['ffa3']['score']:.1%} | {formats['ffa4']['score']:.1%} | "
            f"{formats['ffa5']['score']:.1%} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else root / args.binary
    output = args.output_dir or root / "tournament-results" / datetime.now(timezone.utc).strftime(
        f"{OUTPUT_STEM}-%Y%m%dT%H%M%SZ"
    )
    if not output.is_absolute():
        output = root / output
    output.mkdir(parents=True, exist_ok=True)

    configurations = variants()
    validate(binary, configurations)
    maps = select_maps(binary, root)
    base_schedule = portfolio.make_portfolio_schedule(maps, args.rounds, args.seed, args.max_steps)
    spool = output / "match-telemetry"
    prior_results, completed_ids = resume_results(
        spool, len(base_schedule), len(configurations)
    )
    workloads = []
    for configuration in configurations:
        schedule = []
        for match in base_schedule:
            item = dict(match)
            item["id"] = configuration["id"] * len(base_schedule) + int(match["id"])
            item["variant"] = configuration["name"]
            item["telemetry"] = not args.no_telemetry
            if item["id"] not in completed_ids:
                schedule.append(item)
        workloads.append((configuration["id"], schedule, configuration["overrides"]))

    print(
        f"Running {len(configurations)} factorial variants x {len(base_schedule)} paired games; "
        f"resuming {len(completed_ids)} completed and scheduling "
        f"{sum(len(schedule) for _, schedule, _ in workloads)} "
        f"on {sum(count for _, count in args.workers)} slots...",
        flush=True,
    )
    started = time.monotonic()
    cluster = DirectSSHPortfolioCluster(
        binary, root, args.workers, args.remote_root, args.timeout, args.retries,
        True, tournament.make_spool_sink(spool),
    )
    new_results = cluster.run_many(workloads)
    results = {
        configuration["id"]: sorted(
            prior_results[configuration["id"]] + new_results[configuration["id"]],
            key=lambda result: int(result["id"]),
        )
        for configuration in configurations
    }
    elapsed = time.monotonic() - started
    scores = {
        configuration["name"]: portfolio.score_results(results[configuration["id"]])
        for configuration in configurations
    }
    for configuration in configurations:
        portfolio.print_score(configuration["name"], scores[configuration["name"]])

    effects = factor_effects(configurations, scores)
    checkpoints = [] if args.no_telemetry else checkpoint_rows(spool)
    write_csv(output / "factor_effects.csv", effects)
    write_csv(output / "checkpoint_metrics.csv", checkpoints)
    payload = {
        "metadata": {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "rounds": args.rounds, "seed": args.seed, "max_steps": args.max_steps,
            "workers": args.workers, "wall_seconds": round(elapsed, 3),
            "maps": MAPS, "games_per_variant": len(base_schedule),
            "telemetry": not args.no_telemetry,
        },
        "configurations": configurations,
        "scores": scores,
        "factor_effects": effects,
        "matches": {
            configuration["name"]: portfolio.compact_results(results[configuration["id"]])
            for configuration in configurations
        },
    }
    (output / "results.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_summary(output, configurations, scores, results, effects, elapsed, len(base_schedule))
    print(f"Report: {output / 'summary.md'}", flush=True)
    return 0 if all(score["failed"] == 0 for score in scores.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
