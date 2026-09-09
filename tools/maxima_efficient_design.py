"""Budgeted experiment routing and conservative finite-look decisions.

Finite-look bounds use Maurer & Pontil (2009), Theorem 11, with a union
bound over both tails, four looks, and 53 switches x 5 formats x 2 screen
families. Assumes independent source blocks; seat rotations are averaged.
https://www.cs.mcgill.ca/~colt2009/papers/012.pdf
"""
from __future__ import annotations
import csv
import hashlib
import json
import math
import os
from pathlib import Path
from statistics import mean, variance
from collections import defaultdict
import run_maxima_switch_ablation as ablation

MODES = {'farming.enabled': 'FS 30k', 'recon.enabled': 'FS 30k', 'tactics.enabled': 'CP+END', 'colonization.enabled': 'FS+CP', 'teamplay.enabled': 'FS+CP+END', 'defense.reactive.enabled': 'CP+END', 'military.preemptive_defense_enabled': 'CP+END', 'military.explorer_defense_enabled': 'CP+END', 'economy.food_service_safeguards_enabled': 'FS 30k', 'upgrades.enabled': 'FS+CP', 'emergencies.food_enabled': 'CP+END', 'emergencies.colony_enabled': 'CP+END', 'postures.recover_enabled': 'SH+CP', 'postures.defend_enabled': 'SH+CP+END', 'postures.expand_enabled': 'SH+CP', 'postures.develop_enabled': 'SH+CP', 'postures.mobilize_enabled': 'SH+CP+END', 'postures.campaign_enabled': 'SH+CP+END', 'postures.finish_enabled': 'SH+CP+END', 'staffing.inn_adaptive_staffing_enabled': 'CP', 'economy.large_economy_adaptation_enabled': 'FS+CP', 'economy.amphibious_network_maintenance_enabled': 'CP', 'economy.worker_birth_throttle_enabled': 'CP', 'repairs.enabled': 'CP+END', 'military.counterattack_enabled': 'CP+END', 'military.warrior_training_backlog_throttle_enabled': 'CP', 'placement.food_preservation_enabled': 'SH+CP', 'placement.defensive_siting_enabled': 'SH+CP+END', 'placement.artery_routing_enabled': 'SH+CP', 'tactics.siege_enabled': 'CP+END', 'tactics.dig_out_enabled': 'CP+END', 'raiding.enabled': 'CP+END', 'teamplay.defense_enabled': 'CP+END', 'explorer_campaign.enabled': 'CP+END', 'recon.scouting_missions_enabled': 'CP', 'recon.force_memory_enabled': 'CP', 'farming.farm_protection_enabled': 'SH+CP', 'farming.barrier_topology_enabled': 'CP', 'farming.coastal_porosity_enabled': 'FS+CP', 'farming.resource_preserving_circulation_enabled': 'FS+CP', 'farming.gate_clearing_enabled': 'CP', 'farming.maintenance_clearing_enabled': 'CP', 'farming.proactive_clearing_enabled': 'CP', 'economy.swarm_retirement_enabled': 'CP', 'military.preemptive_amphibious_enabled': 'CP+END', 'placement.spacing_compactness_enabled': 'SH+CP', 'tactics.failed_target_quarantine_enabled': 'CP+END', 'tactics.siege_target_lock_enabled': 'CP+END', 'teamplay.pressure_coordination_enabled': 'SH+CP+END', 'fruit.enabled': 'CP', 'recon.economic_watch_enabled': 'CP', 'farming.wheat_invasion_clearing_enabled': 'FS+CP', 'farming.wood_firebreak_enabled': 'FS+CP'}
LOOKS = (40, 80, 160, 320)
MAX_BLOCKS = 320
ALPHA = .05
FAMILIES = len(MODES) * 5 * 2

def route(keys, mode):
    return [key for key in keys if mode in MODES[key].split()[0].split("+")]

def horizon(key):
    # Only shorten families explicitly assigned 10k in the frozen design.
    return 10000 if key in {"staffing.inn_adaptive_staffing_enabled",
                            "economy.worker_birth_throttle_enabled"} else 20000

def bound(values, metric):
    n=len(values)
    if n<32:return None
    # Component scores are in [-1,1]; victory score is in [-100,100].
    limit=200.0 if metric=="victory_score" else 2.0
    if any(not math.isfinite(v) or abs(v)>limit+1e-7 for v in values):
        raise ValueError("score outside declared sequential bounds")
    log=math.log(4 * len(LOOKS) * FAMILIES / ALPHA)
    radius=math.sqrt(2*variance(values)*log/n)+7*(2*limit)*log/(3*(n-1))
    center=mean(values)
    return [max(-limit,center-radius),min(limit,center+radius)]

def decisions(directory, keys, look):
    groups=defaultdict(lambda:defaultdict(list));meta={}
    with (directory/"paired.csv").open() as handle:
        for row in csv.DictReader(handle):
            groups[row["switch"]][row["source_block"]].append(float(row["difference"]))
            meta[row["switch"]]=(row["metric"],float(row["mpid"]))
    result={}
    for key in keys:
        values=[mean(v) for v in groups[key].values()]
        status="continue";bounds=None;required=None
        if len(values)<32:status="insufficient qualified opportunities"
        else:
            metric,mpid=meta[key];bounds=bound(values,metric)
            required=ablation.required_blocks(values,mpid,32)
            if bounds[1]<-mpid:status="harmful; needs fresh confirmation"
            elif bounds[0]>mpid:status="helpful"
            elif bounds[0]>=-mpid and bounds[1]<=mpid:status="equivalent within MPID"
            elif look==40 and required>MAX_BLOCKS:status="unresolved: required sample exceeds budget"
            elif look==MAX_BLOCKS:status="unresolved: maximum budget reached"
        result[key]={"status":status,"qualified_blocks":len(values),"bounds":bounds,
                     "required_blocks_90pct":required,"look":look}
    return result

def cumulative_rows(existing, candidates, keys, quota):
    # Freeze previous choices: adding banks must never replace pilot states.
    result=list(existing);ids={r["checkpoint_id"] for r in result}
    for key in keys:
        blocks={r["source_block"] for r in result if r["switch"]==key}
        maps=defaultdict(lambda:defaultdict(list))
        for row in candidates:
            if row["switch"]==key and row["source_block"] not in blocks:
                maps[row["map"]][row["source_block"]].append(row)
        queues={m:iter(sorted(b)) for m,b in maps.items()}
        while len(blocks)<quota:
            advanced=False
            for m in sorted(queues):
                block=next(queues[m],None)
                if block is None:continue
                advanced=True;blocks.add(block)
                for row in maps[m][block]:
                    if row["checkpoint_id"] not in ids:result.append(row);ids.add(row["checkpoint_id"])
                if len(blocks)>=quota:break
            if not advanced:break
    return result

def seed_cache(output, destination, rows, entries):
    """Reuse only all-four-repeat qualified cases with identical settings/state."""
    target={r["checkpoint_id"]:r for r in rows};dest=destination/"logs"
    dest.mkdir(parents=True,exist_ok=True);copied=0
    fields=("checkpoint_id","source_block","switch","path","horizon","metric","mpid",
            "focal_player","focal_team","allied_teams","save_checksum","rng_checksum")
    for entry in entries:
        source=Path(entry["summary"]).parent
        if source==destination or not (source/"paired.csv").exists():continue
        old=ablation.read_manifest(Path(entry["manifest"]))
        qualified=set()
        with (source/"paired.csv").open() as handle:
            qualified={r["checkpoint_id"] for r in csv.DictReader(handle)}
        for oldrow in old:
            key=oldrow["checkpoint_id"];new=target.get(key)
            if not new or key not in qualified:continue
            if any(oldrow.get(k)!=new.get(k) for k in fields):continue
            # Shared immutable source path must still agree with the recorded bank.
            candidates=[]
            for arm in ("on","off"):
                for rep in range(2):
                    name=f"{key}-{arm}-{rep}.log";src=source/"logs"/name
                    if src.with_suffix(".log.gz").exists():src=src.with_suffix(".log.gz")
                    if not src.exists():break
                    candidates.append(src)
            if len(candidates)!=4:continue
            for src in candidates:
                dst=dest/src.name
                if dst.exists():continue
                # Independent inode: the runner atomically rewrites resumed logs.
                import shutil
                shutil.copy2(src,dst);copied+=1
    return copied
