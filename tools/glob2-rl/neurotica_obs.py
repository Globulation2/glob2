#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reader for Neurotica observation files (.aob), written by GLOB2_NEUROTICA_OBS_PATH.

The wire format is specified in src/ai/neurotica/NeuroticaObservation.h, which is the
authority; this is the Python side. Together with neurotica_trace.py it forms the
behaviour-cloning pipeline:

    input  = observation planes at tick t        (this module)
    label  = that team's state at tick t + delta (neurotica_trace.py)

The two live in separate files because the label is a property of the future
and cannot be written at time t. Joining them here means delta can be swept
without regenerating the corpus.

CLI:
    python3 tools/glob2-rl/neurotica_obs.py summary FILE.aob
    python3 tools/glob2-rl/neurotica_obs.py check FILE.aob        # sanity + fog audit
    python3 tools/glob2-rl/neurotica_obs.py pairs FILE.aob FILE.atr --delta 500
"""

from __future__ import annotations

import argparse
import struct
import zlib
from dataclasses import dataclass
from typing import List, Optional

import numpy as np

MAGIC = b"AOB1"

STATIC_PLANES = ["terrain_grass", "terrain_sand", "terrain_water", "fertility"]

BUILDINGS = ["swarm", "inn", "hospital", "racetrack", "swimmingpool", "barracks",
             "school", "defencetower", "explorationflag", "warflag",
             "clearingflag", "stonewall", "market"]

DYNAMIC_PLANES = (
    [f"resource_{i}" for i in range(8)]
    + [f"my_{b}" for b in BUILDINGS]
    + ["my_building_level", "my_building_site", "my_building_workers"]
    + [f"enemy_{b}" for b in BUILDINGS]
    + ["ally_building",
       "my_unit_worker", "my_unit_explorer", "my_unit_warrior",
       "enemy_unit_worker", "enemy_unit_explorer", "enemy_unit_warrior",
       "area_guard", "area_clear", "area_forbidden",
       "discovery",
       "grad_wood", "grad_wheat", "grad_stone",
       "grad_forbidden", "grad_guard", "grad_clear"]
)

# Planes that must be zero wherever the team has never seen the cell. Own
# buildings, own units and own areas are deliberately exempt: a team knows
# where its own things are regardless of what it can currently see.
FOG_GATED = ([f"resource_{i}" for i in range(8)]
             + [f"enemy_{b}" for b in BUILDINGS]
             + ["ally_building", "enemy_unit_worker", "enemy_unit_explorer",
                "enemy_unit_warrior"])


@dataclass
class Record:
    tick: int
    team: int
    teacher: int
    planes: np.ndarray  # (num_dynamic, h, w) uint8


class ObservationFile:
    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            self.buf = handle.read()
        if len(self.buf) < 20 or self.buf[:4] != MAGIC:
            raise ValueError(f"{path}: not an Neurotica observation file")
        (self.count, self.w, self.h, self.n_static, self.n_dynamic,
         self.num_teams, _pad, static_len) = struct.unpack_from("<IHHBBBBI", self.buf, 4)
        at = 20
        raw = zlib.decompress(self.buf[at:at + static_len])
        self.static = np.frombuffer(raw, dtype=np.uint8).reshape(
            self.n_static, self.h, self.w)
        self._first = at + static_len
        if self.n_dynamic != len(DYNAMIC_PLANES):
            raise ValueError(
                f"{path}: file has {self.n_dynamic} dynamic planes, this reader "
                f"knows {len(DYNAMIC_PLANES)} — the C++ enum and this list drifted")

    def __len__(self) -> int:
        return self.count

    def records(self):
        at = self._first
        for _ in range(self.count):
            tick, team, teacher, _pad, clen = struct.unpack_from("<IBBHI", self.buf, at)
            at += 12
            raw = zlib.decompress(self.buf[at:at + clen])
            at += clen
            planes = np.frombuffer(raw, dtype=np.uint8).reshape(
                self.n_dynamic, self.h, self.w)
            yield Record(tick, team, teacher, planes)

    def plane(self, record: Record, name: str) -> np.ndarray:
        return record.planes[DYNAMIC_PLANES.index(name)]


def _cmd_summary(args) -> int:
    obs = ObservationFile(args.path)
    print(f"{args.path}")
    print(f"  map      {obs.w}x{obs.h}")
    print(f"  planes   {obs.n_static} static + {obs.n_dynamic} dynamic")
    print(f"  records  {obs.count}  (teams: {obs.num_teams})")
    ticks = [r.tick for r in obs.records()]
    if ticks:
        print(f"  ticks    {min(ticks)}..{max(ticks)}")
    raw_per = obs.n_dynamic * obs.w * obs.h
    print(f"  raw/record {raw_per/1024:.0f} KiB, on disk "
          f"{len(obs.buf)/max(obs.count,1)/1024:.1f} KiB "
          f"({raw_per/(len(obs.buf)/max(obs.count,1)):.0f}x)")
    return 0


def _cmd_check(args) -> int:
    obs = ObservationFile(args.path)
    problems = 0
    nonzero = np.zeros(obs.n_dynamic, dtype=np.int64)
    checked = 0
    for record in obs.records():
        checked += 1
        nonzero += (record.planes.reshape(obs.n_dynamic, -1) != 0).sum(axis=1)
        # Fog audit: nothing the team cannot see may appear in a gated plane.
        unseen = obs.plane(record, "discovery") == 0
        for name in FOG_GATED:
            leaked = int((record.planes[DYNAMIC_PLANES.index(name)][unseen] != 0).sum())
            if leaked:
                print(f"  FOG LEAK tick={record.tick} team={record.team} "
                      f"plane={name} cells={leaked}")
                problems += 1
    print(f"checked {checked} records")
    dead = [DYNAMIC_PLANES[i] for i in range(obs.n_dynamic) if nonzero[i] == 0]
    print(f"always-zero planes ({len(dead)}): {', '.join(dead) if dead else 'none'}")
    print(f"fog leaks: {problems}")
    return 1 if problems else 0


def _cmd_pairs(args) -> int:
    import neurotica_trace
    obs = ObservationFile(args.obs)
    trace = neurotica_trace.load(args.trace)
    made, missing = 0, 0
    for record in obs.records():
        label = trace.at(record.team, record.tick + args.delta)
        if label is None:
            missing += 1
            continue
        made += 1
    print(f"delta={args.delta}: {made} training pairs, {missing} observations "
          f"past the end of the trace")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("summary"); s.add_argument("path"); s.set_defaults(func=_cmd_summary)
    c = sub.add_parser("check"); c.add_argument("path"); c.set_defaults(func=_cmd_check)
    p = sub.add_parser("pairs"); p.add_argument("obs"); p.add_argument("trace")
    p.add_argument("--delta", type=int, default=500); p.set_defaults(func=_cmd_pairs)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
