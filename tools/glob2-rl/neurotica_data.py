#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Behaviour-cloning dataset for Neurotica.

Pairs an observation record (tick t) with the desired-state label derived from
the same team's trace at tick t+delta. See NeuroticaObservation.h and
NeuroticaTrace.h for the formats, and the project notes for why the label is
"the state this team had a while later".

Records are decompressed lazily. A game's observations are ~880 KB raw each and
there are hundreds per game, so the corpus only fits on disk compressed; an
offset index built once per file makes random access cheap enough to stream
from.
"""

from __future__ import annotations

import os
import pickle

# Bump whenever the contents of a Sample's label bytes change.
LABEL_SCHEMA = 3
import struct
import zlib
from dataclasses import dataclass
from typing import List, Optional, Tuple

import numpy as np
import torch
from torch.utils.data import Dataset

import neurotica_obs as nobs
import neurotica_trace as ntrace

NUM_BUILDING_CLASSES = 14  # none + IntBuildingType::NB_BUILDING


@dataclass
class Sample:
    path_obs: str
    path_trace: str
    offset: int          # byte offset of the record payload in the .aob
    clen: int
    n_dynamic: int
    w: int
    h: int
    tick: int
    team: int
    teacher: int
    outcome: int
    # Label, extracted once at index time. Re-parsing the trace per sample
    # would re-read and re-decode a multi-megabyte file for every single
    # example; storing the label instead costs a few hundred bytes each.
    label_buildings: bytes = b""   # int16 x7: x, y, type, workers, ratio[3]
    label_areas: bytes = b""       # zlib(dense h*w area bitmask)


def _index_obs(path: str) -> Tuple[dict, List[dict]]:
    """Record offsets for one .aob, so samples can be read without a full scan."""
    with open(path, "rb") as fh:
        buf = fh.read()
    if len(buf) < 20 or buf[:4] != nobs.MAGIC:
        raise ValueError(f"{path}: bad magic")
    (count, w, h, n_static, n_dynamic, num_teams, _pad,
     static_len) = struct.unpack_from("<IHHBBBBI", buf, 4)
    header = dict(count=count, w=w, h=h, n_static=n_static, n_dynamic=n_dynamic,
                  static_len=static_len, static_off=20)
    at = 20 + static_len
    records = []
    for _ in range(count):
        tick, team, teacher, _p, clen = struct.unpack_from("<IBBHI", buf, at)
        at += 12
        records.append(dict(tick=tick, team=team, teacher=teacher, off=at, clen=clen))
        at += clen
    return header, records


class NeuroticaBC(Dataset):
    """(observation planes, desired-state label) pairs.

    `delta` is the label horizon in ticks. `winners_only` keeps a team's samples
    only when that team won: pooled teacher data is the point, but training
    equally hard on losing play teaches the net to reproduce losing play.
    """

    def __init__(self, corpus_dir: str, delta: int = 500, winners_only: bool = True,
                 cache: Optional[str] = None, limit_games: Optional[int] = None,
                 require_size: Optional[Tuple[int, int]] = (128, 128)):
        self.delta = delta
        self.samples: List[Sample] = []
        self.static_cache = {}

        tag = f"{require_size[0]}x{require_size[1]}" if require_size else "any"
        # LABEL_SCHEMA is in the filename on purpose: the index stores the
        # label bytes themselves, so changing what a label contains must
        # invalidate it. Without this, adding `workers` to the label quads
        # silently reused an index full of triples and failed on reshape.
        cache = cache or os.path.join(
            corpus_dir,
            f"index_v{LABEL_SCHEMA}_d{delta}_w{int(winners_only)}_{tag}.pkl")
        if os.path.exists(cache):
            with open(cache, "rb") as fh:
                self.samples, self.shape = pickle.load(fh)
            return

        games = sorted(f[:-4] for f in os.listdir(corpus_dir) if f.endswith(".aob"))
        if limit_games:
            games = games[:limit_games]
        shape = None
        for name in games:
            obs_path = os.path.join(corpus_dir, name + ".aob")
            trace_path = os.path.join(corpus_dir, name + ".atr")
            if not os.path.exists(trace_path):
                continue
            try:
                header, records = _index_obs(obs_path)
                trace = ntrace.load(trace_path)
            except Exception:
                continue  # a killed or truncated game; skip it rather than die
            if require_size and (header["w"], header["h"]) != require_size:
                # Batching needs one shape, and anything under 128x128 is not a
                # playable map anyway, so a stray small map is a corpus bug
                # rather than something to accommodate.
                continue
            shape = (header["n_dynamic"], header["h"], header["w"])
            for rec in records:
                outcome = trace.outcomes[rec["team"]] if rec["team"] < len(trace.outcomes) else 0
                if winners_only and outcome != 1:
                    continue
                label = trace.at(rec["team"], rec["tick"] + delta)
                if label is None:
                    continue  # no label this far ahead
                # workers rides along: maxUnitWorking is how the teachers
                # concentrate labour, and it is the one desired-state plane the
                # early economy turns on. The trace has carried it all along.
                # ratio rides along too. A swarm defaults to ratio[0]=1 and
                # zero elsewhere (Building Lifecycle.cpp), i.e. workers only
                # and never a warrior, and the policy could not reach the
                # plane -- so Neurotica has never been able to field an army.
                triples = np.array(
                    [(b.x, b.y, b.short_type, b.workers,
                      b.ratio[0], b.ratio[1], b.ratio[2])
                     for b in label.buildings],
                    dtype=np.int16).tobytes() if label.buildings else b""
                self.samples.append(Sample(
                    obs_path, trace_path, rec["off"], rec["clen"],
                    header["n_dynamic"], header["w"], header["h"],
                    rec["tick"], rec["team"], rec["teacher"], outcome,
                    triples, zlib.compress(label.areas, 1)))
        self.shape = shape
        with open(cache, "wb") as fh:
            pickle.dump((self.samples, self.shape), fh)

    def __len__(self) -> int:
        return len(self.samples)

    def _static(self, path: str) -> np.ndarray:
        if path not in self.static_cache:
            with open(path, "rb") as fh:
                buf = fh.read(1 << 22)
            (_c, w, h, n_static, _nd, _nt, _p, slen) = struct.unpack_from("<IHHBBBBI", buf, 4)
            raw = zlib.decompress(buf[20:20 + slen])
            self.static_cache[path] = np.frombuffer(raw, dtype=np.uint8).reshape(n_static, h, w)
        return self.static_cache[path]

    def __getitem__(self, i: int):
        s = self.samples[i]
        with open(s.path_obs, "rb") as fh:
            fh.seek(s.offset)
            raw = zlib.decompress(fh.read(s.clen))
        dynamic = np.frombuffer(raw, dtype=np.uint8).reshape(s.n_dynamic, s.h, s.w)
        static = self._static(s.path_obs)

        # Tick as two extra planes. The corpus carries no team scalars yet, and
        # game phase is the one global the label depends on most strongly:
        # what belongs on a map at tick 2,000 is not what belongs at 40,000.
        t = np.float32(min(s.tick, 60000) / 60000.0)
        tick_planes = np.stack([
            np.full((s.h, s.w), t, dtype=np.float32),
            np.full((s.h, s.w), np.float32(np.sqrt(t)), dtype=np.float32)])

        obs = np.concatenate([
            static.astype(np.float32) / 255.0,
            dynamic.astype(np.float32) / 255.0,
            tick_planes], axis=0)

        building = np.zeros((s.h, s.w), dtype=np.int64)
        # One triple per building, so a bincount over the type column is the
        # exact per-type building count -- not a cell count.
        counts = np.zeros(13, dtype=np.float32)
        # Desired staffing per cell, and a mask marking where it is defined.
        # Only anchor cells of labelled buildings carry a target; everywhere
        # else the loss must not pull toward zero.
        workers = np.zeros((s.h, s.w), dtype=np.float32)
        wmask = np.zeros((s.h, s.w), dtype=np.float32)
        # Unit-production mix, as a proportion. Supervised only where a swarm
        # actually stands, and stored normalised: a ratio is scale-free, which
        # is exactly why it should transfer to this agent's small economy when
        # absolute staffing did not.
        ratio = np.zeros((3, s.h, s.w), dtype=np.float32)
        rmask = np.zeros((s.h, s.w), dtype=np.float32)
        if s.label_buildings:
            triples = np.frombuffer(s.label_buildings, dtype=np.int16).reshape(-1, 7)
            inside = (triples[:, 0] >= 0) & (triples[:, 0] < s.w) & \
                     (triples[:, 1] >= 0) & (triples[:, 1] < s.h)
            t3 = triples[inside]
            building[t3[:, 1], t3[:, 0]] = t3[:, 2] + 1
            valid = (t3[:, 2] >= 0) & (t3[:, 2] < 13)
            counts = np.bincount(t3[valid, 2], minlength=13).astype(np.float32)
            workers[t3[:, 1], t3[:, 0]] = np.clip(t3[:, 3], 0, 255)
            wmask[t3[:, 1], t3[:, 0]] = 1.0
            sw = t3[t3[:, 2] == 0]                      # SWARM_BUILDING == 0
            if len(sw):
                r = sw[:, 4:7].astype(np.float32)
                tot = r.sum(axis=1, keepdims=True)
                keep = tot[:, 0] > 0
                if keep.any():
                    r, sw = r[keep] / tot[keep], sw[keep]
                    ratio[:, sw[:, 1], sw[:, 0]] = r.T
                    rmask[sw[:, 1], sw[:, 0]] = 1.0
        a = np.frombuffer(zlib.decompress(s.label_areas), dtype=np.uint8).reshape(s.h, s.w)
        areas = np.stack([((a & 1) != 0), ((a & 2) != 0),
                          ((a & 4) != 0)]).astype(np.float32)

        return (torch.from_numpy(obs), torch.from_numpy(building),
                torch.from_numpy(areas), torch.from_numpy(counts),
                torch.from_numpy(workers), torch.from_numpy(wmask),
                torch.from_numpy(ratio), torch.from_numpy(rmask))


def collate(batch):
    obs = torch.stack([b[0] for b in batch])
    building = torch.stack([b[1] for b in batch])
    areas = torch.stack([b[2] for b in batch])
    counts = torch.stack([b[3] for b in batch])
    workers = torch.stack([b[4] for b in batch])
    wmask = torch.stack([b[5] for b in batch])
    ratio = torch.stack([b[6] for b in batch])
    rmask = torch.stack([b[7] for b in batch])
    return obs, building, areas, counts, workers, wmask, ratio, rmask
