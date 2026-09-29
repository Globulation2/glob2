#!/usr/bin/env python3
"""Summarize control effects from the shared control_study.py ablation output."""
import argparse
import gzip
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("study", type=Path)
args = parser.parse_args()
opener = gzip.open if args.study.suffix == ".gz" else open
with opener(args.study, "rt") as source:
    rows = [json.loads(line) for line in source]

defaults = {"opening-ration": 50, "central-concentration": 65,
            "wheat-amount": 100, "wood-amount": 100,
            "stone-amount": 100, "algae-amount": 100}


def metric(row, control):
    records = row["telemetry"]
    if control == "opening-ration":
        return statistics.mean(r["value"] for r in records
                               if r["key"] == "hungry-marches.home.wheat")
    if control == "central-concentration":
        central = {r["subject"]: r["value"] for r in records
                   if r["key"] == "hungry-marches.field.central"}
        potential = {r["subject"]: r["value"] for r in records
                     if r["key"] == "hungry-marches.field.growth-potential"}
        return sum(v for k, v in potential.items() if central[k]) / sum(potential.values())
    return row["m"]["tiles:" + control.removesuffix("-amount")]


summary = {}
for control, default in defaults.items():
    selected = [r for r in rows if r["w"] == r["h"] == 256 and r["teams"] == 4
                and (r["study"] == "baseline" or r["control"] == control)]
    values = sorted({default if r["study"] == "baseline" else r["value"]
                     for r in selected})
    measurements = []
    for value in values:
        group = [r for r in selected
                 if (default if r["study"] == "baseline" else r["value"]) == value]
        successful = [metric(r, control) for r in group if r["ok"]]
        measurements.append(dict(value=value, expected=len(group), successful=len(successful),
                                 mean=statistics.mean(successful) if successful else None))
    means = [m["mean"] for m in measurements]
    summary[control] = dict(values=measurements,
                            strictly_increasing=all(a is not None and b is not None and a < b
                                                    for a, b in zip(means, means[1:])))
print(json.dumps(summary, indent=2))
