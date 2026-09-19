#!/usr/bin/env python3
"""Summarize the retained games made by playtest.py, including economy counters."""
import argparse
import gzip
import json
import pathlib
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=pathlib.Path)
args = parser.parse_args()
rows = []
for result_path in sorted(args.directory.glob("game-*/result.json")):
    result = json.loads(result_path.read_text())
    counters = {}
    with gzip.open(result_path.parent / "run.log.gz", "rt", errors="replace") as log:
        for line in log:
            if line.startswith("GLOB2_MEASURE"):
                values = {k: int(v) for k, v in re.findall(r"(\S+?)=(-?\d+)", line)}
                counters[values["team"]] = values
    for team in result["teams"]:
        values = counters[team["team"]]
        rows.append({
            "game": result_path.parent.name,
            "status": result["status"],
            "ticks": result["ticks"],
            "team": team["team"],
            "start": team["start"],
            "units": team["units"],
            "peak_units": max(row[0] for row in team["history"]),
            "eliminated_tick": team.get("eliminated_tick"),
            "worker_births": values["births_0"],
            "wheat_harvested": values["harvested_1"],
            "wheat_delivered": values["delivered_1"],
            "wood_harvested": values["harvested_0"],
            "wood_delivered": values["delivered_0"],
            "workers_killed": values["deaths_0_0"],
            "workers_starved": values["deaths_0_1"],
            "warriors_killed": values["deaths_2_0"],
            "warriors_starved": values["deaths_2_1"],
        })
print(json.dumps(rows, indent=2))
