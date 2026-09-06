#!/usr/bin/env python3
"""Aggregate scripts/pathfind_ab.sh output directories: per-algorithm means
of units, buildings, deliveries, random steps while working and tiles walked
per delivery. usage: pathfind_ab_summary.py <outdir>..."""
import glob, re, sys, statistics

def parse(log):
    teams = {}
    for line in open(log, errors="replace"):
        m = re.match(r"GLOB2_TL team=(\d+) tick=\d+ units=(\d+) bld=(\d+)", line)
        if m:
            teams.setdefault(int(m[1]), {}).update(units=int(m[2]), bld=int(m[3]))
        m = re.match(r"GLOB2_PF_TEAM team=(\d+) (.*)", line)
        if m:
            kv = dict(p.split("=") for p in m[2].split())
            teams.setdefault(int(m[1]), {}).update({k: int(v) for k, v in kv.items()})
    return teams

def main():
    for outdir in sys.argv[1:]:
        rows = {"BASE": [], "ALT": []}
        for log in sorted(glob.glob(f"{outdir}/*.log")):
            tag = "altT1" if log.endswith("-altT1.log") else "altT0"
            alt_team = 1 if tag == "altT1" else 0
            for team, d in parse(log).items():
                if "deliveries" not in d:
                    continue
                moves = d["moves_walk"] + d["moves_swim"]
                d["tiles_per_delivery"] = moves / max(1, d["deliveries"])
                d["diag_share"] = d["moves_diagonal"] / max(1, moves)
                rows["ALT" if team == alt_team else "BASE"].append(d)
        print(f"== {outdir}: {len(rows['BASE'])} baseline / {len(rows['ALT'])} alternative team-games")
        keys = ["units", "bld", "deliveries", "random_while_working", "moves_swim", "tiles_per_delivery", "diag_share"]
        print("%-22s %10s %10s %8s" % ("metric", "BASE", "ALT", "delta"))
        for k in keys:
            b = statistics.mean(r[k] for r in rows["BASE"])
            a = statistics.mean(r[k] for r in rows["ALT"])
            delta = (a - b) / b * 100 if b else 0
            print("%-22s %10.2f %10.2f %+7.1f%%" % (k, b, a, delta))

main()
