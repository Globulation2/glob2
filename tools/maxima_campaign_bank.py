"""Campaign manifests with map/seed clustering and explicit alliance membership.

From-start rows use tick-zero saves, before any AI decision. Diagnostic events
select the immediately preceding periodic save, never a later outcome. They
are labeled observed-context screens, not side-effect-free shadow probes.
"""
from __future__ import annotations

from bisect import bisect_left
from collections import defaultdict
from pathlib import Path
import json
from functools import lru_cache

import build_maxima_checkpoint_bank as bank
import run_maxima_switch_ablation as ablation


MAJOR = {"farming.enabled", "recon.enabled", "colonization.enabled",
         "economy.food_service_safeguards_enabled",
         "economy.large_economy_adaptation_enabled", "upgrades.enabled"}
TOPOLOGY = {"FourSquares1": "partitioned", "G2": "land",
            "Garden_3": "land", "Holiday_Island_2": "islands",
            "Isles": "islands", "Migration": "migration", "balanced": "land"}


def event_switches(event: str, values: dict[str, str]) -> set[str]:
    def positive(key: str) -> bool:
        try:
            return float(values.get(key, "0")) > 0
        except ValueError:
            return False
    keys = set()
    if event == "farming_policy":
        for key, evidence in (
            ("farm_protection_enabled", "protected_seeds"),
        ):
            if positive(evidence):
                keys.add("farming." + key)
    if event == "maintenance_clearing":
        for key, evidence in (
            ("maintenance_clearing_enabled", "added"),
            ("resource_preserving_circulation_enabled", "reservation_resources_preserved"),
            ("wheat_invasion_clearing_enabled", "wheat_invasion_wood"),
            ("wood_firebreak_enabled", "firebreak_wood"),
        ):
            if positive(evidence):
                keys.add("farming." + key)
    if event == "land_clearing_started":
        keys.add("farming.proactive_clearing_enabled")
    if event == "placement_planner":
        for key, evidence in (
            ("food_preservation_enabled", "u_farm_loss"),
            ("defensive_siting_enabled", "u_threat"),
            ("artery_routing_enabled", "u_artery"),
            ("spacing_compactness_enabled", "u_compact"),
        ):
            if values.get(evidence) not in (None, "0", "0.0"):
                keys.add("placement." + key)
    direct = {
        "swarm_retirement_issued": {"economy.swarm_retirement_enabled"},
        "target_quarantined": {"tactics.failed_target_quarantine_enabled"},
        "mission_retargeted": {"tactics.siege_enabled"},
        "dig_out_started": {"tactics.dig_out_enabled"},
        "explorer_strike_launched": {"explorer_campaign.enabled"},
        "recon_mission_created": {"recon.scouting_missions_enabled"},
    }
    keys.update(direct.get(event, set()))
    if event == "mission_selected":
        kind = values.get("kind", "")
        if kind == "siege": keys.add("tactics.siege_enabled")
        if kind == "raid": keys.add("raiding.enabled")
    return keys


def source_rows(directory: Path) -> list[dict]:
    return [row for row in json.loads((directory / "source-runs.json").read_text())
            if not row["error"]]


def contexts(source: dict, namespace: str) -> tuple[dict, list[dict], list[dict]]:
    return _contexts(json.dumps(source, sort_keys=True), namespace,
                     Path(source["log"]).stat().st_mtime_ns)


@lru_cache(maxsize=4096)
def _contexts(serialized: str, namespace: str, log_mtime: int) -> tuple[dict, list[dict], list[dict]]:
    del log_mtime
    source = json.loads(serialized)
    text = Path(source["log"]).read_text(errors="replace")
    saves, events, players = [], [], {}
    for line in text.splitlines():
        if line.startswith("MAXIMA_CHECKPOINT_SAVED\t"):
            row = bank.fields(line)
            row["tick"] = int(row["tick"])
            saves.append(row)
        elif line.startswith("MAXIMA_ABLATION_OPPORTUNITY\t"):
            row = bank.fields(line)
            players[int(row["player"])] = int(row["team"])
            events.append({**row, "tick": int(row["tick"]), "kind": "opportunity_probe"})
        elif line.startswith("NICOWAR_SCENARIO_PLAYER_RESULT\t"):
            parts = line.split("\t")
            players[int(parts[3])] = int(parts[4])
        elif line.startswith("MAXIMA_TELEMETRY\t"):
            parts = line.split("\t")
            values = bank.fields(line)
            for key in event_switches(parts[3], values):
                events.append({"switch": key, "team": int(parts[2]),
                               "tick": max(0, int(parts[1])-1),
                               "kind": "observed_context", "event": parts[3]})
    if source["format"] == "2v2":
        partition = ({0, 1}, {0, 2}, {0, 3})[int(source["partition"])]
        allies = sorted(set(range(4)) - partition if source["swap"] else partition)
        focal = allies[int(source["round"]) % len(allies)]
        players = {team: team for team in range(4)}
    else:
        focal = int(source["candidate_seat"])
        if focal not in players:
            raise ValueError(f"source log has no focal player mapping: {source['log']}")
        allies = [players[focal]]
    map_name = Path(source["map"]).stem
    # Deliberately excludes seat, opponent, checkpoint and continuation.
    # Different formats are reported separately; map/seed is the independent unit.
    block = f"{map_name}-seed{source['seed']}"
    identity = f"{namespace}-{source['name']}-p{focal}"
    metadata = {
        "source_id": identity, "source_block": block,
        "source_seed": source["seed"], "map": map_name,
        "topology": TOPOLOGY.get(map_name, "unspecified"),
        "format": source["format"], "opponent_ai": source.get("opponent_ai", 5),
        "focal_player": focal, "focal_team": players[focal], "allied_teams": allies,
        "source_log": source["log"],
    }
    eligible = [event for event in events if int(event["team"]) == players[focal]]
    return metadata, sorted(saves, key=lambda r: r["tick"]), eligible


def make_row(metadata: dict, save: dict, switch: str, mode: str, horizon: int,
             event: dict | None = None) -> dict:
    metric = ablation.default_metric(switch)
    return {**metadata,
            "checkpoint_id": f"{metadata['source_id']}-{mode}-{switch.replace('.', '-')}-{save['tick']}",
            "path": save["path"], "switch": switch, "mode": mode,
            "horizon": horizon, "metric": metric, "mpid": ablation.SCORE_METRICS[metric],
            "checkpoint_tick": save["tick"], "save_checksum": save.get("checksum"),
            "rng_checksum": save.get("rng"), "selection_evidence": event or {"tick": 0}}


def build(directories: list[Path], switches: list[str], mode: str, horizon: int,
          quota: int | None = None) -> list[dict]:
    rows = []
    for directory in directories:
        for source in source_rows(directory):
            meta, saves, events = contexts(source, directory.name)
            if not saves:
                continue
            ticks = [save["tick"] for save in saves]
            for switch in switches:
                if mode == "from-start":
                    if saves[0]["tick"] != 0:
                        raise ValueError("from-start screen requires a tick-zero checkpoint")
                    rows.append(make_row(meta, saves[0], switch, mode, horizon))
                else:
                    matches = sorted((e for e in events if e["switch"] == switch),
                                     key=lambda e: e["tick"])
                    for event in matches:
                        # Strictly precedes the observed action/decision, including
                        # events emitted after a farming order was enqueued.
                        index = bisect_left(ticks, event["tick"])-1
                        if index < 0 or event["tick"]-ticks[index] > 1000:
                            continue
                        rows.append(make_row(meta, saves[index], switch, mode, horizon, event))
                        break
    if quota is None:
        return rows
    # Map-stratified round-robin source blocks; retain their seat rotations.
    kept = []
    for switch in switches:
        groups = defaultdict(list)
        for row in rows:
            if row["switch"] == switch:
                groups[row["source_block"]].append(row)
        strata = defaultdict(list)
        for key in sorted(groups):
            strata[groups[key][0]["map"]].append(key)
        chosen = []
        while len(chosen) < quota:
            advanced = False
            for map_name in sorted(strata):
                if strata[map_name] and len(chosen) < quota:
                    chosen.append(strata[map_name].pop(0)); advanced = True
            if not advanced: break
        for key in chosen: kept.extend(groups[key])
    return kept


def write(path: Path, rows: list[dict], switches: list[str]) -> None:
    coverage = {key: len({r["source_block"] for r in rows if r["switch"] == key})
                for key in switches}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"schema_version": 2, "coverage_blocks": coverage,
                               "checkpoints": rows}, indent=2) + "\n")
