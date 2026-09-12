#!/usr/bin/env python3
"""Tune the standalone Maxima director with paired 2v2 matches.

The sampler is a compact integer TPE implementation. Every candidate is scored
on common map/seed blocks; finalists are then evaluated on held-out maps and
seeds. Match workers may run locally or over SSH on identical remote checkouts.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import shlex
import subprocess
import threading
import time
import warnings
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean, pstdev
from typing import Any

import run_nicowar_2v2_tournament as tournament
import run_nicowar_tournament as ffa


@dataclass(frozen=True)
class Parameter:
    name: str
    low: int
    high: int
    default: int
    impact: str = "medium"
    impact_rank: int = 2
    choices: tuple[int, ...] = ()


# Stage membership remains an experiment concern. Values, defaults, types, and
# bounds come exclusively from the running C++ binary.
PARAMETER_SETS = {
    "director": (
        "emergencies.food_unserved_percent",
        "emergencies.food_critical_percent",
        "emergencies.food_combined_percent",
        "scheduling.posture_commitment_ticks",
        "scoring.posture_switch_margin",
        "military.defense_reserve_min",
        "military.campaign_population_base",
        "military.campaign_force_margin",
        "military.campaign_worker_floor_base",
        "military.attack_unit_cap",
    ),
    "economy": (
        "model.inn_capacity_level1",
        "model.inn_capacity_level2",
        "model.inn_capacity_level3",
        "staffing.control_window_samples",
        "staffing.control_low_permille",
        "staffing.control_high_permille",
        "staffing.control_slack",
        "staffing.control_cooldown_passes",
        "economy.inn_population_offset",
        "economy.inn_population_divisor",
        "economy.food_headroom_warning",
        "economy.food_headroom_critical",
        "economy.swarm_labor_scale_percent",
        "economy.swarm_food_per_worker_percent",
        "economy.swarm_workers_per_building",
        "economy.swarm_pressure_sensitivity",
        "economy.school_population_min",
        "economy.school_utility_min",
        "economy.second_school_utility_min",
        "economy.pool_population_min",
        "economy.pool_utility_min",
        "economy.racetrack_population_min",
        "economy.racetrack_utility_min",
        "construction.population_mid",
        "construction.population_high",
        "economy.growth_site_utility_mid",
        "economy.growth_site_utility_high",
        "upgrades.level1_population_min",
        "upgrades.level2_population_min",
        "upgrades.level1_workers",
        "upgrades.level2_workers",
        "upgrades.level1_trained_units_per_slot",
        "upgrades.level2_trained_units_per_slot",
        "upgrades.level1_inn_base_weight",
        "upgrades.level1_hospital_base_weight",
        "upgrades.level1_racetrack_base_weight",
        "upgrades.level1_pool_base_weight",
        "upgrades.level1_barracks_base_weight",
        "upgrades.level2_inn_base_weight",
        "upgrades.level2_hospital_base_weight",
        "upgrades.level2_racetrack_base_weight",
        "upgrades.level2_pool_base_weight",
        "upgrades.level2_barracks_base_weight",
        "upgrades.first_prestige_trained_workers",
        "upgrades.second_prestige_trained_workers",
        "upgrades.second_prestige_population_min",
    ),
    "tactical": (
        "recon.attack_warning_threshold",
        "recon.colony_warning_threshold",
        "military.tower_active_count",
        "military.tower_emergency_increment",
        "military.tower_bomb_increment",
        "military.defender_explorer_percent",
        "military.defender_explorer_cap",
        "military.defense_reserve_floor",
        "military.defense_enemy_percent",
        "military.offense_warrior_base_percent",
        "recon.offense_population_divisor",
        "military.campaign_deployable_min",
        "scheduling.campaign_stall_ticks",
        "scheduling.campaign_retreat_cooldown_ticks",
        "scoring.target_switch_margin",
        "scoring.target_reachable_weight",
        "scoring.target_warrior_weight",
        "scoring.target_distance_bias",
        "defense.reactive.flag_radius",
        "defense.reactive.move_radius",
        "defense.reactive.unit_cap",
        "defense.reactive.advantage_min",
        "defense.reactive.advantage_percent",
        "explorer_campaign.trained_min",
        "explorer_campaign.large_economy_trained_min",
        "explorer_campaign.multi_flag_trained_min",
        "explorer_campaign.max_flags",
        "explorer_campaign.units_per_flag",
        "fruit.population_min",
        "fruit.units_per_flag",
        "fruit.flag_radius",
    ),
    # High-impact warrior campaign and explorer fruit-clearing controls.
    # Defensive geometry, tower staffing, defender explorers, late-game
    # explorer campaigns, and worker-raiding controls stay fixed so the joint
    # optimizer receives adequate evidence per dimension.
    "tactical-core": (
        "military.defense_reserve_floor",
        "military.defense_enemy_percent",
        "military.offense_warrior_base_percent",
        "recon.offense_population_divisor",
        "military.campaign_deployable_min",
        "scheduling.campaign_stall_ticks",
        "scheduling.campaign_retreat_cooldown_ticks",
        "scoring.target_switch_margin",
        "scoring.target_reachable_weight",
        "scoring.target_warrior_weight",
        "scoring.target_distance_bias",
        "fruit.population_min",
        "fruit.units_per_flag",
        "fruit.flag_radius",
    ),
    "tactical-expanded": (),
    "teamplay": (
        "teamplay.enabled",
        "teamplay.pressure_coordination_enabled",
        "teamplay.defense_enabled",
        "teamplay.allied_pressure_radius",
        "teamplay.defense_min_force",
        "teamplay.defense_strength_percent",
        "teamplay.defense_base_score",
        "teamplay.defense_threat_weight",
        "teamplay.defense_under_attack_bonus",
        "teamplay.defense_unit_value",
        "teamplay.defense_route_distance_weight",
        "teamplay.defense_contact_ttl_ticks",
        "teamplay.defense_max_engagement_ticks",
        "teamplay.defense_cooldown_ticks",
        "teamplay.defense_follow_radius",
        "teamplay.defense_retarget_margin",
        "teamplay.siege_player_pressure_bonus",
        "teamplay.siege_building_pressure_bonus",
    ),
    "preemptive-defense-toggle": ("military.preemptive_defense_enabled",),
    "preemptive-defense": (
        "military.preemptive_defense_enabled",
        "military.preemptive_warriors_min",
        "military.preemptive_warriors_per_zone",
        "military.preemptive_amphibious_enabled",
        "military.preemptive_swimming_warriors_min",
        "military.preemptive_inner_distance",
        "military.preemptive_band_width",
        "military.preemptive_path_slack",
        "military.preemptive_probe_radius",
        "military.preemptive_cross_section_max",
        "military.preemptive_zone_radius",
        "military.preemptive_zone_max",
        "scheduling.preemptive_defense_recompute_ticks",
    ),
    "farming": (
        "farming.farm_protection_enabled",
        "farming.maintenance_clearing_enabled",
        "farming.resource_preserving_circulation_enabled",
        "farming.wheat_invasion_clearing_enabled",
        "farming.wood_firebreak_enabled",
        "farming.proactive_clearing_enabled",
        "farming.normal_interval_ticks",
        "farming.urgent_interval_ticks",
        "farming.wheat_fertility_min",
        "farming.wood_fertility_base_percent",
        "farming.wood_fertility_pressure_percent",
        "farming.wood_pressure_base",
        "farming.wood_pressure_space_divisor",
        "farming.wood_pressure_supply_divisor",
        "farming.wood_pressure_construction_divisor",
        "farming.wood_pressure_growth_divisor",
        "farming.economic_envelope_radius_tiles",
    ),
    "placement": (
        "placement.food_zone_penalty_weight",
        "placement.food_zone_radius",
        "placement.inner_food_zone_multiplier",
        "placement.hospital_food_zone_multiplier",
        "placement.tower_food_zone_multiplier",
    ),
}
PARAMETER_SETS["tactical-expanded"] = (
    PARAMETER_SETS["tactical"] + PARAMETER_SETS["teamplay"]
)

PARAMETERS: tuple[Parameter, ...] = ()
DIRECTOR_PARAMETERS: tuple[Parameter, ...] = ()
ECONOMY_PARAMETERS: tuple[Parameter, ...] = ()
TACTICAL_PARAMETERS: tuple[Parameter, ...] = ()
TACTICAL_EXPANDED_PARAMETERS: tuple[Parameter, ...] = ()
PREEMPTIVE_DEFENSE_TOGGLE_PARAMETERS: tuple[Parameter, ...] = ()
PREEMPTIVE_DEFENSE_PARAMETERS: tuple[Parameter, ...] = ()
STRATEGY_SCHEMA: dict[str, Any] = {}
RESOLVED_STRATEGY: dict[str, Any] = {}
_ACTIVE_STAGE = "director"

# This identifier is persisted in optimizer checkpoints and manifests. Resume
# must never mix observations produced by the retired coordinate-wise sampler
# with this joint Optuna proposal engine.
PROPOSAL_ENGINE = "optuna-tpe-multivariate-group-v1"


def _read_strategy_contract(binary: Path, format_name: str) -> None:
    global STRATEGY_SCHEMA, RESOLVED_STRATEGY
    repo_root = Path(__file__).resolve().parent.parent
    schema_process = subprocess.run(
        [str(binary), "--dump-maxima-schema"],
        cwd=repo_root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    resolved_process = subprocess.run(
        [
            str(binary),
            "--dump-maxima-strategy",
            "--maxima-format",
            format_name,
        ],
        cwd=repo_root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    STRATEGY_SCHEMA = json.loads(schema_process.stdout)
    RESOLVED_STRATEGY = json.loads(resolved_process.stdout)
    if STRATEGY_SCHEMA.get("schemaVersion") != 2:
        raise RuntimeError("Maxima optimizer requires strategy schema version 2")
    if RESOLVED_STRATEGY.get("schemaVersion") != 2:
        raise RuntimeError("resolved Maxima strategy is not schema version 2")


def resolve_candidate(
    binary: Path, parameters: dict[str, int], format_name: str = "2v2"
) -> dict[str, Any]:
    """Have the runtime parser validate and fully resolve one candidate."""
    repo_root = Path(__file__).resolve().parent.parent
    process = subprocess.run(
        [
            str(binary),
            "--dump-maxima-strategy",
            "--maxima-format",
            format_name,
            "--maxima-overrides",
            tuning_string(parameters),
        ],
        cwd=repo_root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return json.loads(process.stdout)


def _parameters_for(keys: tuple[str, ...]) -> tuple[Parameter, ...]:
    metadata = {
        item["key"]: item for item in STRATEGY_SCHEMA.get("parameters", [])
    }
    resolved = {
        item["key"]: item["value"]
        for item in RESOLVED_STRATEGY.get("parameters", [])
    }
    missing = sorted(set(keys) - metadata.keys())
    if missing:
        raise RuntimeError(f"binary strategy schema is missing keys: {missing}")
    return tuple(
        Parameter(
            name=key,
            low=int(metadata[key]["searchMinimum"]),
            high=int(metadata[key]["searchMaximum"]),
            default=int(resolved[key]),
            impact=str(metadata[key]["impact"]),
            impact_rank=int(metadata[key]["impactRank"]),
        )
        for key in keys
    )


def configure_parameters(
    binary: Path, stage: str = "director", format_name: str = "2v2"
) -> None:
    global DIRECTOR_PARAMETERS, ECONOMY_PARAMETERS, TACTICAL_PARAMETERS
    global TACTICAL_EXPANDED_PARAMETERS, PREEMPTIVE_DEFENSE_TOGGLE_PARAMETERS
    global PREEMPTIVE_DEFENSE_PARAMETERS
    _read_strategy_contract(binary, format_name)
    DIRECTOR_PARAMETERS = _parameters_for(PARAMETER_SETS["director"])
    ECONOMY_PARAMETERS = _parameters_for(PARAMETER_SETS["economy"])
    TACTICAL_PARAMETERS = _parameters_for(PARAMETER_SETS["tactical"])
    TACTICAL_EXPANDED_PARAMETERS = _parameters_for(
        PARAMETER_SETS["tactical-expanded"]
    )
    PREEMPTIVE_DEFENSE_TOGGLE_PARAMETERS = _parameters_for(
        PARAMETER_SETS["preemptive-defense-toggle"]
    )
    PREEMPTIVE_DEFENSE_PARAMETERS = _parameters_for(
        PARAMETER_SETS["preemptive-defense"]
    )
    set_parameter_stage(stage)


def set_parameter_stage(stage: str) -> None:
    global PARAMETERS, _ACTIVE_STAGE
    _ACTIVE_STAGE = stage
    if STRATEGY_SCHEMA:
        PARAMETERS = _parameters_for(PARAMETER_SETS[stage])
DEFAULT_TRAIN_MAPS = ("FourSquares1", "G2", "Holiday Island 2", "Migration")
DEFAULT_VALIDATION_MAPS = ("Garden 3", "Isles", "balanced")


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary",
        type=Path,
        default=repo_root / "build-tournament" / "src" / "glob2",
    )
    parser.add_argument("--trials", type=int, default=24)
    parser.add_argument("--startup-trials", type=int, default=8)
    parser.add_argument("--finalists", type=int, default=4)
    parser.add_argument("--train-rounds", type=int, default=1)
    parser.add_argument("--validation-rounds", type=int, default=2)
    parser.add_argument("--max-steps", type=int, default=180000)
    parser.add_argument(
        "--validation-max-steps",
        type=int,
        help="Held-out match ceiling; defaults to --max-steps",
    )
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--seed", type=int, default=0x56335450)
    parser.add_argument("--sampler-seed", type=int, default=0x54504533)
    parser.add_argument(
        "--min-trial-distance",
        type=float,
        default=0.055,
        help="Minimum mean normalized parameter distance between proposals",
    )
    parser.add_argument("--jobs-per-host", type=int, default=4)
    parser.add_argument(
        "--host",
        action="append",
        dest="hosts",
        help="Worker host; use local for this machine. Repeat to form a cluster.",
    )
    parser.add_argument(
        "--remote-root",
        default="/home/bradley/glob2-optimizer",
        help="Checkout path shared by SSH workers",
    )
    parser.add_argument("--train-map", action="append", dest="train_maps")
    parser.add_argument(
        "--validation-map", action="append", dest="validation_maps"
    )
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if not args.hosts:
        args.hosts = ["local"]
    if not args.train_maps:
        args.train_maps = list(DEFAULT_TRAIN_MAPS)
    if not args.validation_maps:
        args.validation_maps = list(DEFAULT_VALIDATION_MAPS)
    if args.validation_max_steps is None:
        args.validation_max_steps = args.max_steps
    positive = (
        args.trials,
        args.startup_trials,
        args.finalists,
        args.train_rounds,
        args.validation_rounds,
        args.max_steps,
        args.validation_max_steps,
        args.timeout,
        args.jobs_per_host,
    )
    if any(value < 1 for value in positive):
        parser.error("trial, round, step, timeout, finalist, and job counts must be positive")
    if args.finalists > args.trials:
        parser.error("--finalists cannot exceed --trials")
    if not 0.0 <= args.min_trial_distance <= 1.0:
        parser.error("--min-trial-distance must be between 0 and 1")
    return args


def default_parameters() -> dict[str, int]:
    return {parameter.name: parameter.default for parameter in PARAMETERS}


def tuning_string(parameters: dict[str, int]) -> str:
    # Candidate validation happens before a worker process is launched.
    for parameter in PARAMETERS:
        value = parameters[parameter.name]
        if value < parameter.low or value > parameter.high:
            raise ValueError(
                f"{parameter.name}={value} is outside "
                f"[{parameter.low}, {parameter.high}]"
            )
    return ",".join(
        f"{parameter.name}={parameters[parameter.name]}" for parameter in PARAMETERS
    )


def random_parameters(rng: random.Random) -> dict[str, int]:
    return {
        parameter.name: (
            rng.choice(parameter.choices)
            if parameter.choices
            else rng.randint(parameter.low, parameter.high)
        )
        for parameter in PARAMETERS
    }


def parameter_distance(left: dict[str, int], right: dict[str, int]) -> float:
    """Mean absolute distance after putting every knob on a 0..1 scale."""
    distances = []
    for parameter in PARAMETERS:
        if parameter.choices:
            left_index = parameter.choices.index(left[parameter.name])
            right_index = parameter.choices.index(right[parameter.name])
            width = max(1, len(parameter.choices) - 1)
            distances.append(abs(left_index - right_index) / width)
        else:
            width = max(1, parameter.high - parameter.low)
            distances.append(abs(left[parameter.name] - right[parameter.name]) / width)
    return mean(distances)


def minimum_distance(
    candidate: dict[str, int], completed: list[dict[str, Any]]
) -> float:
    if not completed:
        return 1.0
    return min(
        parameter_distance(candidate, trial["parameters"]) for trial in completed
    )


def novel_random_parameters(
    rng: random.Random,
    completed: list[dict[str, Any]],
    required_distance: float,
) -> dict[str, int]:
    """Return the most novel of bounded random proposals."""
    best = random_parameters(rng)
    best_distance = minimum_distance(best, completed)
    for _ in range(255):
        candidate = random_parameters(rng)
        distance = minimum_distance(candidate, completed)
        if distance > best_distance:
            best, best_distance = candidate, distance
        if distance >= required_distance:
            return candidate
    return best


def _require_optuna() -> Any:
    try:
        import optuna
    except ImportError as error:
        raise RuntimeError(
            "Maxima optimization requires Optuna; create the optimizer "
            "environment with `python3 -m venv .venv-optimizer` and install "
            "`requirements-optimizer.txt`"
        ) from error
    return optuna


def optuna_version() -> str:
    return str(_require_optuna().__version__)


def optuna_distributions() -> dict[str, Any]:
    """Build the exact integer search contract consumed by Optuna."""
    optuna = _require_optuna()
    return {
        parameter.name: (
            optuna.distributions.CategoricalDistribution(parameter.choices)
            if parameter.choices
            else optuna.distributions.IntDistribution(
                parameter.low, parameter.high
            )
        )
        for parameter in PARAMETERS
    }


def optuna_tpe_parameters(
    rng: random.Random, completed: list[dict[str, Any]],
    startup_trials: int = 24,
) -> dict[str, int]:
    """Ask Optuna's joint multivariate TPE for one deterministic proposal.

    Each proposal receives a seed from the campaign RNG and reconstructs a
    small in-memory study from one BOHB budget. This makes checkpoint replay
    deterministic without pickling private sampler state, while Optuna still
    models all tactical coordinates jointly.
    """
    optuna = _require_optuna()
    distributions = optuna_distributions()
    proposal_seed = rng.getrandbits(32)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", optuna.exceptions.ExperimentalWarning)
        sampler = optuna.samplers.TPESampler(
            seed=proposal_seed,
            n_startup_trials=startup_trials,
            n_ei_candidates=64,
            multivariate=True,
            group=True,
        )
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study = optuna.create_study(direction="maximize", sampler=sampler)
    historical_trials = []
    for record in completed:
        objective = record.get("objective")
        if record.get("failed", 0) or objective is None:
            continue
        objective = float(objective)
        if not math.isfinite(objective):
            continue
        historical_trials.append(optuna.trial.create_trial(
            params={
                parameter.name: record["parameters"][parameter.name]
                for parameter in PARAMETERS
            },
            distributions=distributions,
            value=objective,
        ))
    study.add_trials(historical_trials)
    trial = study.ask()
    result: dict[str, int] = {}
    for parameter in PARAMETERS:
        if parameter.choices:
            value = trial.suggest_categorical(
                parameter.name, parameter.choices
            )
        else:
            value = trial.suggest_int(
                parameter.name, parameter.low, parameter.high
            )
        result[parameter.name] = int(value)
    return result


def novel_tpe_parameters(
    rng: random.Random,
    completed: list[dict[str, Any]],
    required_distance: float,
    novelty_records: list[dict[str, Any]] | None = None,
    startup_trials: int = 24,
) -> dict[str, int]:
    """Return an acquisition-aware Optuna proposal with sufficient novelty."""
    history = novelty_records if novelty_records is not None else completed
    best: dict[str, int] | None = None
    best_distance = -1.0
    for _ in range(64):
        candidate = optuna_tpe_parameters(
            rng, completed, startup_trials=startup_trials
        )
        distance = minimum_distance(candidate, history)
        if distance > best_distance:
            best, best_distance = candidate, distance
        if distance >= required_distance:
            return candidate
    assert best is not None
    return best


def select_diverse_finalists(
    trials: list[dict[str, Any]], count: int, required_distance: float
) -> list[dict[str, Any]]:
    """Select high scorers without spending validation on near-identical vectors."""
    ranked = sorted(trials, key=lambda trial: trial["objective"], reverse=True)
    baseline = trials[0]
    selected: list[dict[str, Any]] = []
    for trial in ranked:
        if all(
            parameter_distance(trial["parameters"], other["parameters"])
            >= required_distance
            for other in selected
        ):
            selected.append(trial)
        if len(selected) == count:
            break
    for trial in ranked:
        if len(selected) == count:
            break
        if trial not in selected:
            selected.append(trial)
    if baseline not in selected:
        selected[-1] = baseline
    return selected


def select_maps(
    available: list[dict[str, str]], requested: list[str]
) -> list[dict[str, str]]:
    by_name = {item["name"].casefold(): item for item in available}
    selected = []
    for name in requested:
        item = by_name.get(name.casefold())
        if item is None:
            raise ValueError(f"map {name!r} is unavailable")
        selected.append(item)
    return selected


class ClusterRunner:
    def __init__(
        self,
        binary: Path,
        repo_root: Path,
        hosts: list[str],
        remote_root: str,
        jobs_per_host: int,
        timeout: int,
    ) -> None:
        self.binary = binary
        self.repo_root = repo_root
        self.hosts = hosts
        self.remote_root = remote_root.rstrip("/")
        self.jobs_per_host = jobs_per_host
        self.timeout = timeout
        self.semaphores = {
            host: threading.Semaphore(jobs_per_host) for host in hosts
        }

    def _remote_match(
        self, host: str, match: dict[str, Any], settings: str
    ) -> dict[str, Any]:
        binary = f"{self.remote_root}/build-tournament/src/glob2"
        command = [
            binary,
            "-nicowar-2v2-match-nox",
            match["map_file"],
            str(match["seed"]),
            str(match["ai_a"]),
            str(match["ai_b"]),
            str(match["partition"]),
            str(match["swap"]),
            str(match["max_steps"]),
        ]
        remote = (
            f"cd {shlex.quote(self.remote_root)} && "
            f"GLOB2_MAXIMA_OVERRIDES={shlex.quote(settings)} "
            + shlex.join(command)
        )
        started = time.monotonic()
        try:
            process = subprocess.run(
                ["ssh", "-o", "BatchMode=yes", host, remote],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=self.timeout,
            )
            result = tournament.parse_worker_output(match, process.stdout, process.returncode)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            result = dict(match)
            result.update(
                {
                    "status": "failed",
                    "engine_status": "process_timeout",
                    "steps": 0,
                    "winner_side": "draw",
                    "players": [],
                    "returncode": None,
                    "error": f"remote worker exceeded {self.timeout} seconds",
                    "output": output,
                }
            )
        result["wall_seconds"] = round(time.monotonic() - started, 3)
        result["worker_host"] = host
        return result

    def _run_one(
        self, host: str, match: dict[str, Any], settings: str
    ) -> dict[str, Any]:
        with self.semaphores[host]:
            if host == "local":
                result = tournament.run_match(
                    self.binary,
                    self.repo_root,
                    match,
                    self.timeout,
                    False,
                    settings,
                )
                result["worker_host"] = "local"
                return result
            return self._remote_match(host, match, settings)

    def run(self, schedule: list[dict[str, Any]], settings: str) -> list[dict[str, Any]]:
        slots = [
            host for host in self.hosts for _ in range(self.jobs_per_host)
        ]
        results: list[dict[str, Any]] = []
        with ThreadPoolExecutor(max_workers=len(slots)) as executor:
            futures = {
                executor.submit(
                    self._run_one, slots[index % len(slots)], match, settings
                ): match
                for index, match in enumerate(schedule)
            }
            for future in as_completed(futures):
                results.append(future.result())
        return sorted(results, key=lambda result: result["id"])


def score_results(results: list[dict[str, Any]]) -> dict[str, Any]:
    completed = [result for result in results if result["status"] == "completed"]
    if len(completed) != len(results):
        return {
            "objective": -1.0,
            "match_score": 0.0,
            "match_score_se": 0.0,
            "match_score_ci95_low": 0.0,
            "match_score_ci95_high": 0.0,
            "wins": 0,
            "losses": 0,
            "draws": 0,
            "failed": len(results) - len(completed),
            "games": len(results),
        }
    block_values: dict[int, list[tuple[float, float]]] = {}
    wins = losses = draws = 0
    for match in completed:
        tournament.scoring.enrich_2v2_match(match)
        if match["winner_side"] == "A":
            outcome = 1.0
            wins += 1
        elif match["winner_side"] == "B":
            outcome = 0.0
            losses += 1
        else:
            outcome = 0.5
            draws += 1
        continuous = float(match["victory_score_a"])
        normalized = (continuous + 100.0) / 200.0
        block_values.setdefault(match["block"], []).append((outcome, normalized))
    block_outcomes = [mean(value[0] for value in block) for block in block_values.values()]
    block_shaped = [mean(value[1] for value in block) for block in block_values.values()]
    score_se = (
        pstdev(block_outcomes) / math.sqrt(len(block_outcomes))
        if len(block_outcomes) > 1
        else 0.0
    )
    score_mean = mean(block_outcomes)
    return {
        "objective": round(mean(block_shaped), 8),
        "victory_score": round(mean(float(match["victory_score_a"]) for match in completed), 6),
        "tournament_score": round(sum(float(match["victory_score_a"]) for match in completed), 3),
        "match_score": round(score_mean, 8),
        "match_score_se": round(score_se, 8),
        "match_score_ci95_low": round(max(0.0, score_mean - 1.96 * score_se), 8),
        "match_score_ci95_high": round(min(1.0, score_mean + 1.96 * score_se), 8),
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "failed": 0,
        "games": len(completed),
        "blocks": len(block_values),
        "average_units_a": round(
            mean(tournament.side_totals(match, "A")["units"] for match in completed), 3
        ),
        "average_units_b": round(
            mean(tournament.side_totals(match, "B")["units"] for match in completed), 3
        ),
    }


def compact_matches(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    compact = []
    for result in results:
        row = dict(result)
        output = row.pop("output", "")
        row.pop("score_telemetry", None)
        row.pop("observer", None)
        row.pop("telemetry", None)
        if row.get("status") != "completed":
            row["output_tail"] = output[-4000:]
        compact.append(row)
    return compact


def evaluate(
    cluster: ClusterRunner,
    parameters: dict[str, int],
    maps: list[dict[str, str]],
    rounds: int,
    seed: int,
    max_steps: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    # This catches relational errors as well as individual bounds before any
    # local or remote match process is launched.
    resolve_candidate(cluster.binary, parameters)
    schedule = tournament.make_schedule(
        rounds, maps, [("maxima", "original")], seed, max_steps
    )
    results = cluster.run(schedule, tuning_string(parameters))
    return score_results(results), results


def write_report(
    output_dir: Path,
    trials: list[dict[str, Any]],
    validations: list[dict[str, Any]],
    best: dict[str, Any],
    metadata: dict[str, Any],
) -> None:
    ranked = sorted(trials, key=lambda trial: trial["objective"], reverse=True)
    lines = [
        "# Maxima TPE optimization",
        "",
        f"- Trials: {len(trials)}",
        f"- Training maps: {', '.join(metadata['train_maps'])}",
        f"- Held-out validation maps: {', '.join(metadata['validation_maps'])}",
        f"- Worker hosts: {', '.join(metadata['hosts'])}",
        f"- Total wall time: {metadata['wall_seconds']:.1f} seconds",
        f"- Selected configuration: trial {best['trial']}",
        "",
        "Training objective is the normalized continuous victory score: decisive outcome anchors the sign, while speed and time-integrated economy, military, resilience, and prestige dominance determine magnitude. Match points remain separately reported.",
        "",
        "## Leading training trials",
        "",
        "| Trial | Sampler | Games | W-L-D | Match score (95% block CI) | Objective |",
        "|---:|---|---:|---:|---:|---:|",
    ]
    for trial in ranked[: min(10, len(ranked))]:
        lines.append(
            f"| {trial['trial']} | {trial['sampler']} | {trial['games']} | "
            f"{trial['wins']}-{trial['losses']}-{trial['draws']} | "
            f"{trial['match_score']:.1%} "
            f"({trial['match_score_ci95_low']:.1%}–{trial['match_score_ci95_high']:.1%}) | "
            f"{trial['objective']:.4f} |"
        )
    lines.extend(
        [
            "",
            "## Held-out validation",
            "",
            "| Trial | Games | W-L-D | Match score (95% block CI) | Objective |",
            "|---:|---:|---:|---:|---:|",
        ]
    )
    for validation in sorted(
        validations, key=lambda item: item["objective"], reverse=True
    ):
        lines.append(
            f"| {validation['trial']} | {validation['games']} | "
            f"{validation['wins']}-{validation['losses']}-{validation['draws']} | "
            f"{validation['match_score']:.1%} "
            f"({validation['match_score_ci95_low']:.1%}–{validation['match_score_ci95_high']:.1%}) | "
            f"{validation['objective']:.4f} |"
        )
    lines.extend(
        [
            "",
            "## Selected strategy overrides",
            "",
            "```text",
            tuning_string(best["parameters"]),
            "```",
        ]
    )
    (output_dir / "summary.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    binary = args.binary if args.binary.is_absolute() else repo_root / args.binary
    configure_parameters(binary, "director", "2v2")
    available = ffa.discover_maps(binary, repo_root)
    train_maps = select_maps(available, args.train_maps)
    validation_maps = select_maps(available, args.validation_maps)
    timestamp = datetime.now(timezone.utc).strftime("maxima-tpe-%Y%m%dT%H%M%SZ")
    output_dir = args.output_dir or repo_root / "tournament-results" / timestamp
    if not output_dir.is_absolute():
        output_dir = repo_root / output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    cluster = ClusterRunner(
        binary,
        repo_root,
        args.hosts,
        args.remote_root,
        args.jobs_per_host,
        args.timeout,
    )
    rng = random.Random(args.sampler_seed)
    trials: list[dict[str, Any]] = []
    started = time.monotonic()
    for index in range(args.trials):
        if index == 0:
            parameters = default_parameters()
            sampler = "baseline"
        elif index < args.startup_trials:
            parameters = novel_random_parameters(
                rng, trials, args.min_trial_distance
            )
            sampler = "random"
        else:
            parameters = novel_tpe_parameters(
                rng, trials, args.min_trial_distance,
                startup_trials=args.startup_trials,
            )
            sampler = "optuna-tpe"
        trial_started = time.monotonic()
        score, matches = evaluate(
            cluster,
            parameters,
            train_maps,
            args.train_rounds,
            args.seed,
            args.max_steps,
        )
        trial = {
            "trial": index,
            "sampler": sampler,
            "parameters": parameters,
            **score,
            "wall_seconds": round(time.monotonic() - trial_started, 3),
        }
        trials.append(trial)
        with (output_dir / "trials.jsonl").open("a") as handle:
            handle.write(json.dumps(trial, sort_keys=True) + "\n")
        (output_dir / f"trial-{index:03d}-matches.json").write_text(
            json.dumps(compact_matches(matches), indent=2) + "\n"
        )
        print(
            f"trial {index:02d} {sampler:8s}: "
            f"{score['wins']}-{score['losses']}-{score['draws']} "
            f"score={score['match_score']:.1%} objective={score['objective']:.4f} "
            f"({trial['wall_seconds']:.1f}s)",
            flush=True,
        )

    finalist_trials = select_diverse_finalists(
        trials, args.finalists, args.min_trial_distance
    )
    validations = []
    validation_seed = args.seed ^ 0xA5A55A5A
    for finalist in finalist_trials:
        score, matches = evaluate(
            cluster,
            finalist["parameters"],
            validation_maps,
            args.validation_rounds,
            validation_seed,
            args.validation_max_steps,
        )
        validation = {
            "trial": finalist["trial"],
            "parameters": finalist["parameters"],
            "training_objective": finalist["objective"],
            **score,
        }
        validations.append(validation)
        (output_dir / f"validation-{finalist['trial']:03d}-matches.json").write_text(
            json.dumps(compact_matches(matches), indent=2) + "\n"
        )
        print(
            f"validation {finalist['trial']:02d}: "
            f"{score['wins']}-{score['losses']}-{score['draws']} "
            f"score={score['match_score']:.1%} objective={score['objective']:.4f}",
            flush=True,
        )

    best = max(
        validations,
        key=lambda item: (item["objective"], item["training_objective"]),
    )
    wall_seconds = round(time.monotonic() - started, 3)
    selected_resolved_strategy = resolve_candidate(binary, best["parameters"])
    metadata = {
        "created_at": datetime.now(timezone.utc).isoformat(),
        "strategy_schema_version": 2,
        "proposal_engine": PROPOSAL_ENGINE,
        "optuna_version": optuna_version(),
        "seed": args.seed,
        "sampler_seed": args.sampler_seed,
        "train_maps": [item["name"] for item in train_maps],
        "validation_maps": [item["name"] for item in validation_maps],
        "train_rounds": args.train_rounds,
        "validation_rounds": args.validation_rounds,
        "max_steps": args.max_steps,
        "validation_max_steps": args.validation_max_steps,
        "hosts": args.hosts,
        "jobs_per_host": args.jobs_per_host,
        "min_trial_distance": args.min_trial_distance,
        "wall_seconds": wall_seconds,
        "score_signal": {
            "version": tournament.scoring.SIGNAL_VERSION,
            "range": [tournament.scoring.SCORE_MIN, tournament.scoring.SCORE_MAX],
            "outcome_weight": tournament.scoring.OUTCOME_WEIGHT,
            "speed_weight": tournament.scoring.SPEED_WEIGHT,
            "dominance_weight": tournament.scoring.DOMINANCE_WEIGHT,
        },
    }
    payload = {
        "metadata": metadata,
        "strategy_schema": STRATEGY_SCHEMA,
        "baseline_strategy": RESOLVED_STRATEGY,
        "resolved_strategy": selected_resolved_strategy,
        "parameters": [parameter.__dict__ for parameter in PARAMETERS],
        "trials": trials,
        "validations": validations,
        "best": best,
    }
    (output_dir / "optimization.json").write_text(json.dumps(payload, indent=2) + "\n")
    (output_dir / "best-maxima-strategy-overrides.txt").write_text(
        tuning_string(best["parameters"]) + "\n"
    )
    (output_dir / "resolved-strategy.json").write_text(
        json.dumps(selected_resolved_strategy, indent=2) + "\n"
    )
    write_report(output_dir, trials, validations, best, metadata)
    print(
        f"selected trial {best['trial']} with held-out victory score "
        f"{best['victory_score']:+.2f} and match points {best['match_score']:.1%}; reports: {output_dir}",
        flush=True,
    )
    return 1 if any(trial["failed"] for trial in trials + validations) else 0


if __name__ == "__main__":
    raise SystemExit(main())
