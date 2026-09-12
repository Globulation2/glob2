#!/usr/bin/env python3
"""Start metrics by colony index, pooled over seeds and generators.

Rolls every playable generator at one size and colony count over a range of seeds with
MapGeneratorStudy's `quality` output and averages each colony's measured start (score, wheat
and wood distance, room, isolation, nearest-rival distance, build sites) by its colony index.
A placement rule that favoured one index over another would show here; a flat table says the
starts colonies get do not depend on their index.

    python3 tools/colony_start_metrics.py [--seeds 1-40] [--size 256] [--colonies 4]
                                          [--generators 9,11,...] [--study build/src/MapGeneratorStudy]
"""
import argparse
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLUMNS = ["colony", "wheatDistance", "woodDistance", "catchmentTiles", "buildSites",
           "resourceAmount", "rivalDistance", "rivalsWithinThreat", "meanFertility", "wheat",
           "wood", "fertility", "depth", "room", "isolation", "total"]


def parse_range(text):
    if "-" in text:
        a, b = text.split("-", 1)
        return list(range(int(a), int(b) + 1))
    return [int(v) for v in text.split(",")]


def playable_generators(study, profile):
    out = subprocess.run([study, "--catalog"], capture_output=True, text=True, check=True).stdout
    import json
    catalog = json.loads(out)
    return [g["method"] for g in catalog if not g.get("editorOnly")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seeds", default="1-40")
    ap.add_argument("--size", type=int, default=256)
    ap.add_argument("--colonies", type=int, default=4)
    ap.add_argument("--generators", help="comma-separated legacy ids; default: every playable generator")
    ap.add_argument("--study", default=str(ROOT / "build" / "src" / "MapGeneratorStudy"))
    ap.add_argument("--profile", default="glob2-colony-metrics")
    args = ap.parse_args()
    dec = args.size.bit_length() - 1
    if 1 << dec != args.size:
        sys.exit("size must be a power of two")
    generators = parse_range(args.generators) if args.generators else playable_generators(args.study, args.profile)
    rows = []
    for gen in generators:
        for seed in parse_range(args.seeds):
            cmd = [args.study, str(gen), str(seed), args.profile, f"width={dec}", f"height={dec}",
                   f"teams={args.colonies}", "quality"]
            out = subprocess.run(cmd, capture_output=True, text=True).stdout
            for line in out.splitlines():
                if line.startswith("COLONY,"):
                    values = line.split(",")[1:]
                    rows.append((gen, dict(zip(COLUMNS, map(float, values)))))
    if not rows:
        sys.exit("no colonies scored; is the study tool built?")

    def mean(sub, key):
        return statistics.mean(r[key] for _, r in sub)

    def table(sub, title):
        maps = len(sub) // args.colonies
        print(f"\n{title}: mean by colony index over {maps} maps")
        print("colony  total  wheatD  woodD   room   isol  rivalD  sites")
        for c in range(args.colonies):
            s = [(g, r) for g, r in sub if int(r["colony"]) == c]
            print(f"  {c}    {mean(s, 'total'):.3f}  {mean(s, 'wheatDistance'):5.1f}  {mean(s, 'woodDistance'):5.1f}"
                  f"  {mean(s, 'room'):.3f}  {mean(s, 'isolation'):.3f}  {mean(s, 'rivalDistance'):6.1f}"
                  f"  {mean(s, 'buildSites'):6.1f}")

    table(rows, "All generators pooled")
    for gen in generators:
        sub = [(g, r) for g, r in rows if g == gen]
        if sub:
            table(sub, f"Generator {gen}")


if __name__ == "__main__":
    main()
