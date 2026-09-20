# SPDX-License-Identifier: GPL-3.0-or-later
"""NAC1/NPS6 semantic action contract; shared by recording, BC, serving and PPO."""

from dataclasses import dataclass
import hashlib
import json
import struct
import numpy as np

SCHEMA = "neurotica-orders-v1"
FIELDS = "op source type x y workers future radius r0 r1 r2 priority minlevel drop clear receive send mode delay".split()
OPS = "hold create delete cancel_delete construct cancel_construction staff mix radius minlevel priority move clearing exchange guard_area clear_area forbidden_area sharing".split()
PARAMS = {
    0: [],
    1: ["type", "target", "workers", "future", "radius"],
    2: ["source"],
    3: ["source"],
    4: ["source", "workers", "future"],
    5: ["source", "workers"],
    6: ["source", "workers"],
    7: ["source", "r0", "r1", "r2"],
    8: ["source", "radius"],
    9: ["source", "minlevel"],
    10: ["source", "priority"],
    11: ["source", "target", "drop"],
    12: ["source", "clear"],
    13: ["source", "receive", "send"],
    14: ["mode", "area"],
    15: ["mode", "area"],
    16: ["mode", "area"],
    17: ["r0", "r1", "r2"],
}
SIZES = dict(
    op=18,
    type=13,
    workers=256,
    future=256,
    radius=257,
    r0=256,
    r1=256,
    r2=256,
    priority=3,
    minlevel=4,
    drop=2,
    clear=32,
    receive=256,
    send=256,
    mode=2,
    delay=25,
)


@dataclass
class Context:
    raw: bytes
    w: int
    h: int
    team: int
    tick: int
    game_id: int
    phi: float
    static: np.ndarray
    dynamic: np.ndarray
    entities: np.ndarray
    legal: np.ndarray
    discovered: np.ndarray

    def planes(self):
        t = min(self.tick, 60000) / 60000
        return np.concatenate(
            (
                self.static / 255.0,
                self.dynamic / 255.0,
                np.full((1, self.h, self.w), t),
                np.full((1, self.h, self.w), np.sqrt(t)),
            ),
            axis=0,
        ).astype(np.float32)

    def source_mask(self, op):
        e = self.entities
        mask = np.ones(len(e), dtype=bool)
        if op == 7:
            mask = e[:, 3] == 0
        if op in (8, 9, 11):
            mask = e[:, 6] != 0
        if op == 9:
            mask = np.isin(e[:, 3], [8, 9])
        if op == 12:
            mask = e[:, 3] == 10
        if op == 13:
            mask = e[:, 3] == 12
        if op == 4:
            mask = e[:, 28] != 0
        if op == 5:
            mask = e[:, 6] == 0
        return mask

    def op_mask(self):
        mask = np.ones(18, dtype=bool)
        mask[1] = self.legal.any()
        for op in range(2, 14):
            mask[op] = self.source_mask(op).any()
        mask[11] &= self.discovered.any()
        mask[14:17] = self.w <= 512 and self.h <= 512
        return mask


def parse_context(raw):
    if raw[:4] != b"NAC1":
        raise ValueError("incompatible observation schema; regenerate corpus")
    w, h, team, tick, n, phi, nd, game_id = struct.unpack_from("<8i", raw, 4)
    if not (
        8 <= w <= 512
        and 8 <= h <= 512
        and w % 8 == 0
        and h % 8 == 0
        and 0 <= n <= 1024
        and 0 <= phi <= 1000
        and nd >= 54
    ):
        raise ValueError("invalid context dimensions or probability")
    hw = w * h
    at = 36
    expected = at + (4 + nd) * hw + n * 29 * 4 + 14 * hw
    if len(raw) != expected:
        raise ValueError(f"context length {len(raw)} != {expected}")
    static = np.frombuffer(raw, np.uint8, count=4 * hw, offset=at).reshape(4, h, w)
    at += 4 * hw
    dyn = np.frombuffer(raw, np.uint8, count=nd * hw, offset=at).reshape(nd, h, w)
    at += nd * hw
    entities = np.frombuffer(raw, "<i4", count=n * 29, offset=at).reshape(n, 29)
    at += n * 29 * 4
    legal = (
        np.frombuffer(raw, np.uint8, count=13 * hw, offset=at)
        .reshape(13, hw)
        .astype(bool)
    )
    at += 13 * hw
    discovered = np.frombuffer(raw, np.uint8, offset=at).astype(bool)
    return Context(
        raw,
        w,
        h,
        team,
        tick,
        game_id,
        (phi - 500) / 1000.0,
        static,
        dyn,
        entities,
        legal,
        discovered,
    )


def unpack_action(raw):
    if len(raw) < 76:
        raise ValueError("short action")
    a = dict(zip(FIELDS, struct.unpack_from("<19i", raw)))
    a["area"] = np.frombuffer(raw, np.uint8, offset=76).copy()
    return a


def pack_action(a):
    return (
        struct.pack("<19i", *(int(a.get(k, 1 if k == "delay" else 0)) for k in FIELDS))
        + np.asarray(a.get("area", []), np.uint8).tobytes()
    )


def validate_action(c, a):
    op = int(a["op"])
    if not 0 <= op < 18 or not c.op_mask()[op]:
        raise ValueError("illegal operation")
    for key in PARAMS[op] + ["delay"]:
        if key == "source":
            rows = np.flatnonzero(c.entities[:, 0] == a[key])
            if len(rows) != 1 or not c.source_mask(op)[rows[0]]:
                raise ValueError("illegal source")
        elif key == "target":
            x, y = a["x"], a["y"]
            if not (0 <= x < c.w and 0 <= y < c.h):
                raise ValueError("target outside map")
            mask = c.legal[a["type"]] if op == 1 else c.discovered
            if not mask[y * c.w + x]:
                raise ValueError("illegal location")
        elif key == "area":
            if len(a[key]) != c.w * c.h or not np.isin(a[key], [0, 1]).all():
                raise ValueError("invalid area mask")
        else:
            value = int(a[key]) - (1 if key == "delay" else 0)
            if not 0 <= value < SIZES[key]:
                raise ValueError(f"{key} outside supported range: {value}")
    if op == 6 and a["workers"] > 20:
        raise ValueError("staffing exceeds engine limit")
    if op == 8 and a["radius"] == 256:
        raise ValueError("radius sentinel only valid on create")
    if op == 17 and any(a[k] > 3 for k in ("r0", "r1", "r2")):
        raise ValueError("two-team sharing mask required")
    if op not in (14, 15, 16) and len(a.get("area", [])):
        raise ValueError("unexpected area payload")


def checkpoint_id(model_state, config):
    h = hashlib.sha256(json.dumps(config, sort_keys=True).encode())
    for key, t in sorted(model_state.items()):
        h.update(key.encode())
        h.update(t.detach().cpu().contiguous().numpy().tobytes())
    return h.hexdigest()
