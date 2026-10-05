#!/usr/bin/env python3
"""Summarize observational gradient effects; timing runs must not enable this audit."""
import argparse
import collections
import csv
import json
import gzip
import math
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    def percentile(p):
        return values[max(0, math.ceil(p * len(values)) - 1)]
    return {"count": len(values), "median": percentile(.5), "p95": percentile(.95),
            "max": values[-1], "sum": sum(values)}


def open_csv(path):
    return path.open() if path.exists() else gzip.open(str(path) + ".gz", "rt")


def market_changes(row):
    both_available = int(row['live_resource']) >= 0 and int(row['fresh_resource']) >= 0
    live, fresh = int(row['live_choice']), int(row['fresh_choice'])
    return (both_available and (live < 0) != (fresh < 0),
            both_available and live >= 0 and fresh >= 0 and live != fresh)


def analyze(directory):
    directory = Path(directory)
    groups = collections.defaultdict(collections.Counter)
    harms = collections.defaultdict(list)
    cases = collections.defaultdict(list)
    ages = collections.defaultdict(list)
    reasons = collections.defaultdict(lambda: {"live": collections.Counter(), "fresh": collections.Counter()})
    with open_csv(directory / "building-gradient-impact-decisions.csv") as stream:
        for row in csv.DictReader(stream):
            kind = row["kind"]
            live, fresh = int(row["live_choice"]), int(row["fresh_choice"])
            stats = groups[kind]
            stats["decisions"] += 1
            stats["pending"] += int(row["pending"])
            ages[kind].append(int(row["live_age"]))
            for side in ("live", "fresh"):
                reasons[kind][side][row[side + "_reason"]] += 1
            stats["changed"] += int(row["changed"])
            if kind == "resource":
                changed_market, changed_identity = market_changes(row)
                stats["market_vs_harvesting_changed"] += changed_market
                stats["market_identity_changed"] += changed_identity
                live, fresh = int(row["live_resource"]), int(row["fresh_resource"])
            stats["live_failed_fresh_succeeded"] += live < 0 <= fresh
            stats["live_succeeded_fresh_failed"] += fresh < 0 <= live
            stats["resource_type_changed"] += row["live_resource"] != row["fresh_resource"]
            harm = int(row["harm_cost"])
            stats["worse_step"] += harm > 0
            if kind == "movement":
                changed = int(row["changed"]) != 0
                costs_known = int(row["live_score"]) >= 0 and int(row["fresh_score"]) >= 0
                alternatives = changed and 0 <= live < 8 and 0 <= fresh < 8 and costs_known
                stats["changed_without_extra_cost"] += alternatives and harm == 0
                stats["equal_cost_alternatives"] += alternatives and row["live_score"] == row["fresh_score"]
                stats["stale_goal_stop"] += live == 8 and 0 <= fresh < 8
            if kind == 'movement':
                comparable = int(row['live_score']) >= 0 and int(row['fresh_score']) >= 0
                stats['cost_comparable_decisions'] += comparable
                stats['cost_unavailable_decisions'] += not comparable
                if comparable:
                    harms[kind].append(harm)
            if kind in ("movement", "hiring", "hiring_candidate", "resource", "resource_site") and int(row["changed"]) and len(cases[kind]) < 20:
                cases[kind].append(row)
    outcomes = collections.defaultdict(list)
    censored = collections.Counter()
    censor_reasons = collections.defaultdict(collections.Counter)
    resources = collections.Counter()
    market_resources = collections.Counter()
    with open_csv(directory / "building-gradient-impact-outcomes.csv") as stream:
        for row in csv.DictReader(stream):
            kind = row["kind"]
            if int(row["censored"]):
                censored[kind] += 1
                censor_reasons[kind][row.get("censor_reason", "unspecified")] += 1
            else:
                outcomes[kind].append(row)
            if kind == "harvested":
                resources[row["resource"]] += 1
            elif kind == 'market_acquired':
                market_resources[row['resource']] += 1
    with open_csv(directory / "building-gradient-impact-ticks.csv") as stream:
        ticks = list(csv.DictReader(stream))
    summary = {"decisions": {k: dict(v, changed_fraction=v["changed"] / v["decisions"],
                additional_step_cost=distribution(harms[k]), field_age_ticks=distribution(ages[k]),
                rejection_reasons={side: dict(counts) for side, counts in reasons[k].items()}) for k, v in groups.items()},
               "outcomes": {kind: {"elapsed_ticks": distribution([int(r["elapsed_ticks"]) for r in rows]),
                    "distance": distribution([int(r["distance"]) for r in rows]),
                    "reversals": distribution([int(r["reversals"]) for r in rows])} for kind, rows in outcomes.items()},
               "censored": dict(censored), "censor_reasons": {k: dict(v) for k, v in censor_reasons.items()}, "actual_harvested_resources": dict(resources),
               "actual_market_resources": dict(market_resources),
               "case_inventory": dict(cases), "ticks": len(ticks)}
    if ticks:
        summary["economy"] = {"delivery_events": int(ticks[-1]["deliveries_total"]),
            "construction_completions": sum(int(r["construction_completions"]) for r in ticks),
            "unfilled_slot_ticks": sum(int(r["unfilled_slots"]) for r in ticks),
            "hungry_unit_ticks": sum(int(r["hungry_units"]) for r in ticks),
            "deaths": int(ticks[-1]["deaths_total"]) - int(ticks[0]["deaths_total"])}
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.directory), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end="")
