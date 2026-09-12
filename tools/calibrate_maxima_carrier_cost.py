#!/usr/bin/env python3
"""Fit Maxima's carrier cost per harvesting tile from headless telemetry.

Maxima's relocation policy prices distance in worker-ticks: each unit of wheat
an inn or swarm consumes costs one carrier round trip, and the trip takes
`carrier_fixed_ticks_per_trip + 2 * distance * carrier_ticks_per_tile`. Neither
constant is derived from unit speed tables; both are measured here, the way
`food.ticks_per_meal` was.

Generate a log with the game itself, from a tree built in place:

    build/src/glob2 -test-games-nox 1 --map Garden_3 \\
        --matchup maxima,maxima,maxima,maxima -maxima-telemetry > garden3.log

The engine times every round trip that starts at a building: the tick a carrier
leaves after a delivery, the cell it harvests, and the tick it delivers again
(`Unit::fetchStartTick`, `Building::recordDeliveryTrip`). Maxima's
`food_delivery` telemetry publishes the building's cumulative trip count, trip
ticks and trip tiles on every building pass; `food_consumer` publishes the food
ledger's `quality` for the same building every ~1000 ticks, the supply-weighted
mean route distance in hundredths of a tile that the relocation policy will use.

Each window between consecutive `food_consumer` samples yields the mean trip
duration and the mean harvesting distance of the trips completed inside it.
Two weighted least-squares lines are reported:

- trip ticks against actual harvesting tiles: the slope is the round-trip cost
  per tile (`carrier_ticks_per_tile` is half of it), the intercept the fixed
  cost of harvesting and delivering (`carrier_fixed_ticks_per_trip`);
- actual harvesting tiles against ledger quality: how much of a change in the
  policy's proxy is realised as a change in the distance carriers walk. Its
  slope is the default for `food.relocation_distance_realisation_percent`.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def parse_log(path):
    """Yield (tick, team, event, fields) for every Maxima telemetry line."""
    with open(path, "rt", errors="replace") as handle:
        for line in handle:
            if not line.startswith("MAXIMA_TELEMETRY\t"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4:
                continue
            fields = {}
            for item in parts[4:]:
                key, _, value = item.partition("=")
                fields[key] = value
            try:
                yield int(parts[1]), int(parts[2]), parts[3], fields
            except ValueError:
                continue


def collect_windows(paths, min_coverage, min_trips):
    passes = defaultdict(list)     # (game, team, id) -> delivery samples
    consumers = defaultdict(list)  # (game, team, id) -> ledger samples
    for game, path in enumerate(paths):
        for tick, team, event, f in parse_log(path):
            try:
                if event == "food_delivery":
                    passes[(game, team, int(f["building_id"]))].append((
                        tick, int(f["trip_samples"]), int(f["trip_ticks"]),
                        int(f["trip_tiles"]), int(f["enrolled"]),
                        int(f["level"]), f["kind"]))
                elif event == "food_consumer" and int(f["key"]) >= 0:
                    consumers[(game, team, int(f["key"]))].append((
                        tick, int(f["quality"]), int(f["coverage"]), f["kind"]))
            except (KeyError, ValueError):
                continue

    windows = []
    for key, samples in consumers.items():
        samples.sort()
        building_passes = sorted(passes.get(key, ()))
        if len(building_passes) < 2:
            continue
        for start, end in zip(samples, samples[1:]):
            t0, quality, coverage, kind = start
            t1 = end[0]
            inside = [p for p in building_passes if t0 <= p[0] <= t1]
            if len(inside) < 2 or inside[0][5] != inside[-1][5]:
                continue  # too few passes, or an upgrade completed inside
            trips = inside[-1][1] - inside[0][1]
            ticks = inside[-1][2] - inside[0][2]
            tiles = inside[-1][3] - inside[0][3]
            span = inside[-1][0] - inside[0][0]
            if trips < min_trips or span <= 0 or coverage < min_coverage:
                continue
            enrolled_area = sum(a[4] * (b[0] - a[0]) for a, b in zip(inside, inside[1:]))
            windows.append({
                "game": key[0], "team": key[1], "building": key[2], "kind": kind,
                "level": inside[0][5], "tick": t0, "span": span,
                "quality_tiles": quality / 100.0, "coverage": coverage,
                "trips": trips, "trip_ticks": ticks / trips, "trip_tiles": tiles / trips,
                "enrolled": enrolled_area / span,
                # Share of enrolled carrier time spent on timed round trips.
                "utilisation": ticks / enrolled_area if enrolled_area > 0 else float("nan"),
            })
    return windows


def weighted_fit(points):
    """Weighted least squares of y on x. Points are (x, y, weight)."""
    sw = sum(w for _, _, w in points)
    if sw <= 0 or len(points) < 3:
        return None
    mx = sum(w * x for x, _, w in points) / sw
    my = sum(w * y for _, y, w in points) / sw
    sxx = sum(w * (x - mx) ** 2 for x, _, w in points)
    sxy = sum(w * (x - mx) * (y - my) for x, y, w in points)
    if sxx <= 0:
        return None
    slope = sxy / sxx
    intercept = my - slope * mx
    ss_tot = sum(w * (y - my) ** 2 for _, y, w in points)
    ss_res = sum(w * (y - intercept - slope * x) ** 2 for x, y, w in points)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")
    return intercept, slope, r2, mx


def binned_medians(windows, x_key, out):
    bins = defaultdict(list)
    for w in windows:
        bins[int(w[x_key] // 2) * 2].append(w["trip_ticks"])
    for low in sorted(bins):
        values = bins[low]
        print(f"     {low:>3}-{low + 2:<3} tiles  median {statistics.median(values):7.1f} ticks"
              f"  ({len(values)} windows)", file=out)


def report(label, windows, out):
    trips = sum(w["trips"] for w in windows)
    print(f"== {label}: {len(windows)} windows, {trips} round trips", file=out)
    actual = weighted_fit([(w["trip_tiles"], w["trip_ticks"], w["trips"]) for w in windows])
    if actual:
        intercept, slope, r2, mean_x = actual
        print(f"   trip_ticks = {intercept:.1f} + {slope:.2f} * harvest_tiles"
              f"   (weighted R^2 {r2:.3f}, mean {mean_x:.1f} tiles)", file=out)
        print(f"   -> carrier_fixed_ticks_per_trip ~ {intercept:.0f}, "
              f"carrier_ticks_per_tile ~ {slope / 2:.1f} (half the round-trip slope)", file=out)
        binned_medians(windows, "trip_tiles", out)
    else:
        print("   not enough spread in harvesting distance to fit", file=out)
    proxy = weighted_fit([(w["quality_tiles"], w["trip_tiles"], w["trips"]) for w in windows])
    if proxy:
        intercept, slope, r2, mean_x = proxy
        print(f"   harvest_tiles = {intercept:.2f} + {slope:.2f} * ledger_quality_tiles"
              f"   (weighted R^2 {r2:.3f}, mean quality {mean_x:.1f} tiles)", file=out)
        print(f"   -> relocation_distance_realisation_percent ~ {100 * slope:.0f}", file=out)
        bins = defaultdict(list)
        for w in windows:
            bins[int(w["quality_tiles"] // 2) * 2].append(w["trip_tiles"])
        for low in sorted(bins):
            values = bins[low]
            print(f"     quality {low:>3}-{low + 2:<3}  median harvest {statistics.median(values):5.1f} tiles"
                  f"  ({len(values)} windows)", file=out)
    utilisation = [w["utilisation"] for w in windows if w["utilisation"] == w["utilisation"]]
    if utilisation:
        print(f"   median carrier utilisation {statistics.median(utilisation):.2f}", file=out)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--min-coverage", type=int, default=0,
                        help="drop windows whose ledger coverage is below this percent")
    parser.add_argument("--min-trips", type=int, default=3,
                        help="drop windows with fewer completed round trips")
    parser.add_argument("--csv", type=Path, help="write every retained window here")
    args = parser.parse_args()
    windows = collect_windows(args.logs, args.min_coverage, args.min_trips)
    if not windows:
        print("no usable windows: was the game run with -maxima-telemetry?", file=sys.stderr)
        return 1
    report("all buildings", windows, sys.stdout)
    for kind in ("inn", "swarm"):
        subset = [w for w in windows if w["kind"] == kind]
        if subset:
            report(kind, subset, sys.stdout)
    if args.csv:
        with open(args.csv, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(windows[0]))
            writer.writeheader()
            writer.writerows(windows)
        print(f"wrote {len(windows)} windows to {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
