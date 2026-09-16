#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reader for Neurotica desired-state traces (.atr), the format written by
GLOB2_NEUROTICA_TRACE_PATH.

The wire format is specified in src/ai/neurotica/NeuroticaTrace.h; this module is the
Python side of it, used by the M0 oracle comparison and by the M2 behaviour-
cloning label pipeline. Keep the two in sync — the C++ header is the
authority.

CLI:

    # Summarise one trace
    python3 -m tools.glob2-rl.neurotica_trace summary FILE.atr

    # Compare two traces tick-for-tick (the M0 gate)
    python3 -m tools.glob2-rl.neurotica_trace compare REF.atr CAND.atr --team 0
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional

MAGIC = b"ATR2"
FOOTER_MAGIC = b"ATRE"
HEADER_BYTES = 16

OUTCOME_NAMES = {0: "unknown", 1: "won", 2: "lost"}

# IntBuildingType::Number, from src/building/IntBuildingType.h
BUILDING_NAMES = [
    "swarm", "inn", "hospital", "racetrack", "swimmingpool", "barracks",
    "school", "defencetower", "explorationflag", "warflag", "clearingflag",
    "stonewall", "market",
]


@dataclass
class Building:
    x: int
    y: int
    short_type: int
    level: int
    workers: int
    flag_radius: int
    # Building::priority biased by +1, so 0=low, 1=normal, 2=high.
    priority: int = 1
    min_level_to_flag: int = 0
    # Unit-production ratio, one entry per unit type (worker/explorer/warrior).
    ratio: tuple = (0, 0, 0)

    @property
    def name(self) -> str:
        if 0 <= self.short_type < len(BUILDING_NAMES):
            return BUILDING_NAMES[self.short_type]
        return f"type{self.short_type}"


@dataclass
class Snapshot:
    tick: int
    team: int
    teacher: int
    buildings: List[Building] = field(default_factory=list)
    # Dense per-cell area bitmask (1=guard, 2=clear, 4=forbidden), row-major.
    areas: bytes = b""


@dataclass
class Trace:
    map_w: int
    map_h: int
    num_teams: int
    policy_period: int
    snapshots: List[Snapshot]
    outcomes: List[int]

    def for_team(self, team: int) -> List[Snapshot]:
        return [s for s in self.snapshots if s.team == team]

    def at(self, team: int, tick: int) -> Optional[Snapshot]:
        """Earliest snapshot for `team` at or after `tick`, else None.

        Mirrors TraceReader::at in the C++. Returning None past the end of the
        recording rather than clamping to the last snapshot is deliberate:
        beyond the trace there is no evidence of what the teacher wanted, and
        repeating its final state would be an invented label.
        """
        best = None
        for snapshot in self.snapshots:
            if snapshot.team != team or snapshot.tick < tick:
                continue
            if best is None or snapshot.tick < best.tick:
                best = snapshot
        return best

    def teachers(self) -> Dict[int, int]:
        """team -> teacher id (AI::ImplementationID), from the first snapshot."""
        found: Dict[int, int] = {}
        for snapshot in self.snapshots:
            found.setdefault(snapshot.team, snapshot.teacher)
        return found


class _Cursor:
    def __init__(self, buf: bytes) -> None:
        self.buf = buf
        self.at = 0

    def take(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.at + size > len(self.buf):
            raise ValueError(f"truncated trace at offset {self.at}")
        values = struct.unpack_from(fmt, self.buf, self.at)
        self.at += size
        return values


def load(path: str) -> Trace:
    with open(path, "rb") as handle:
        buf = handle.read()
    if len(buf) < HEADER_BYTES or buf[:4] != MAGIC:
        raise ValueError(f"{path}: not an Neurotica trace (bad magic)")

    cur = _Cursor(buf)
    cur.at = 4
    (count, map_w, map_h, num_teams, period, _pad) = cur.take("<IHHBBH")
    cells = map_w * map_h

    snapshots: List[Snapshot] = []
    for _ in range(count):
        (tick, team, teacher, num_buildings) = cur.take("<IBBH")
        snapshot = Snapshot(tick=tick, team=team, teacher=teacher)
        for _b in range(num_buildings):
            (x, y, short_type, level, workers, radius, priority, min_level,
             r0, r1, r2) = cur.take("<HHBBBBBBBBB")
            snapshot.buildings.append(
                Building(x, y, short_type, level, workers, radius,
                         priority, min_level, (r0, r1, r2)))

        (runs,) = cur.take("<I")
        areas = bytearray()
        for _r in range(runs):
            (value, run) = cur.take("<BH")
            if len(areas) + run > cells:
                raise ValueError(f"{path}: area RLE overruns the map")
            areas.extend(bytes([value]) * run)
        if len(areas) != cells:
            raise ValueError(f"{path}: area RLE covers {len(areas)} of {cells} cells")
        snapshot.areas = bytes(areas)
        snapshots.append(snapshot)

    outcomes: List[int] = []
    if cur.at + 4 <= len(buf) and buf[cur.at:cur.at + 4] == FOOTER_MAGIC:
        cur.at += 4
        (footer_teams,) = cur.take("<B")
        for _t in range(footer_teams):
            (outcome,) = cur.take("<B")
            outcomes.append(outcome)

    return Trace(map_w, map_h, num_teams, period, snapshots, outcomes)


def _anchors(snapshot: Snapshot) -> Dict[tuple, int]:
    """Map (x, y) -> short_type for a snapshot's buildings."""
    return {(b.x, b.y): b.short_type for b in snapshot.buildings}


def compare(ref: Trace, cand: Trace, team: int) -> List[dict]:
    """Tick-aligned comparison of two traces for one team.

    Reports, per tick present in both: how many buildings each had, and the
    agreement between them. `exact` counts cells where both placed the SAME
    building type; `placed` counts cells where both placed anything at all.
    IoU over occupied anchor cells is the headline number — it is what tells
    you whether the reconciler reproduced the teacher's layout or merely
    matched its building count.
    """
    ref_by_tick = {s.tick: s for s in ref.for_team(team)}
    rows = []
    for snapshot in cand.for_team(team):
        other = ref_by_tick.get(snapshot.tick)
        if other is None:
            continue
        a, b = _anchors(other), _anchors(snapshot)
        keys_a, keys_b = set(a), set(b)
        both = keys_a & keys_b
        exact = sum(1 for k in both if a[k] == b[k])
        union = len(keys_a | keys_b)
        rows.append({
            "tick": snapshot.tick,
            "ref_buildings": len(keys_a),
            "cand_buildings": len(keys_b),
            "placed_both": len(both),
            "exact_type": exact,
            "iou": (len(both) / union) if union else 1.0,
            "exact_iou": (exact / union) if union else 1.0,
        })
    return rows


def horizon(trace: Trace, team: int, delta: int) -> dict:
    """How much NEW work a label horizon asks for, for one team.

    This is the counterweight to IoU when choosing delta. Fidelity (IoU)
    rises monotonically as delta shrinks, but a label horizon short enough to
    be trivially satisfiable teaches a behaviour-cloning policy to keep what it
    already has and never build. So the horizon has to be long enough that the
    target contains construction the team has not started yet.

    Reports the mean number of buildings present in state(t+delta) but absent
    at t, and the fraction of sampled ticks where the target asks for anything
    new at all. Pure property of the teacher trace — no Neurotica run needed.
    """
    per_team = trace.for_team(team)
    by_tick = {s.tick: s for s in per_team}
    novel, nonzero, samples = 0, 0, 0
    for snapshot in per_team:
        target = by_tick.get(snapshot.tick + delta)
        if target is None:
            continue
        now = set(_anchors(snapshot))
        want = set(_anchors(target))
        new = len(want - now)
        novel += new
        nonzero += 1 if new else 0
        samples += 1
    if not samples:
        return {"delta": delta, "samples": 0, "mean_new": 0.0, "frac_asking": 0.0}
    return {
        "delta": delta,
        "samples": samples,
        "mean_new": novel / samples,
        "frac_asking": nonzero / samples,
    }


def _cmd_horizon(args) -> int:
    trace = load(args.path)
    print(f"{'delta':>7} {'samples':>8} {'mean_new':>9} {'frac_asking':>12}")
    for delta in args.deltas:
        row = horizon(trace, args.team, delta)
        print(f"{row['delta']:>7} {row['samples']:>8} {row['mean_new']:>9.2f} "
              f"{row['frac_asking']:>12.2f}")
    return 0


def _cmd_summary(args) -> int:
    trace = load(args.path)
    print(f"{args.path}")
    print(f"  map          {trace.map_w}x{trace.map_h}")
    print(f"  teams        {trace.num_teams}")
    print(f"  period       {trace.policy_period} ticks")
    print(f"  snapshots    {len(trace.snapshots)}")
    if trace.snapshots:
        print(f"  tick range   {trace.snapshots[0].tick}..{trace.snapshots[-1].tick}")
    for team, teacher in sorted(trace.teachers().items()):
        per_team = trace.for_team(team)
        last = per_team[-1] if per_team else None
        outcome = OUTCOME_NAMES.get(
            trace.outcomes[team] if team < len(trace.outcomes) else 0, "unknown")
        built = len(last.buildings) if last else 0
        areas = sum(1 for byte in last.areas if byte) if last else 0
        print(f"  team {team}: teacher={teacher} outcome={outcome} "
              f"final_buildings={built} area_cells={areas}")
    return 0


def _cmd_compare(args) -> int:
    ref, cand = load(args.ref), load(args.cand)
    rows = compare(ref, cand, args.team)
    if not rows:
        print("no overlapping ticks for that team")
        return 1
    print(f"{'tick':>8} {'ref':>5} {'cand':>5} {'both':>5} {'exact':>6} "
          f"{'IoU':>6} {'exactIoU':>9}")
    step = max(1, len(rows) // args.rows)
    for row in rows[::step]:
        print(f"{row['tick']:>8} {row['ref_buildings']:>5} {row['cand_buildings']:>5} "
              f"{row['placed_both']:>5} {row['exact_type']:>6} "
              f"{row['iou']:>6.2f} {row['exact_iou']:>9.2f}")
    final = rows[-1]
    mean_iou = sum(r["iou"] for r in rows) / len(rows)
    mean_exact = sum(r["exact_iou"] for r in rows) / len(rows)
    print()
    print(f"mean IoU over {len(rows)} ticks: {mean_iou:.3f}  "
          f"(exact-type {mean_exact:.3f})")
    print(f"final: ref={final['ref_buildings']} buildings, "
          f"cand={final['cand_buildings']}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    summary = sub.add_parser("summary", help="describe one trace")
    summary.add_argument("path")
    summary.set_defaults(func=_cmd_summary)

    comparison = sub.add_parser("compare", help="compare two traces for one team")
    comparison.add_argument("ref")
    comparison.add_argument("cand")
    comparison.add_argument("--team", type=int, default=0)
    comparison.add_argument("--rows", type=int, default=20,
                            help="approximate number of sampled rows to print")
    comparison.set_defaults(func=_cmd_compare)

    horizon_cmd = sub.add_parser("horizon", help="how much new work each delta asks for")
    horizon_cmd.add_argument("path")
    horizon_cmd.add_argument("--team", type=int, default=0)
    horizon_cmd.add_argument("--deltas", type=int, nargs="+",
                             default=[125, 250, 500, 1000, 2000, 4000])
    horizon_cmd.set_defaults(func=_cmd_horizon)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
