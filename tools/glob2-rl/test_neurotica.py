# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
import numpy as np
import torch
from neurotica_actions import (
    FIELDS,
    PARAMS,
    parse_context,
    pack_action,
    unpack_action,
    validate_action,
)
from neurotica_net import NeuroticaNet
from neurotica_ppo import compute_rewards, gae
from neurotica_bc import imitation_loss, atomic_save
from neurotica_data import OrderCorpus
from neurotica_decode import anchors
from paired_eval import paired_summary


def context(tick=0, gid=1, empty=False):
    w = h = 16
    nd = 59
    e = np.zeros((0 if empty else 4, 29), "<i4")
    if not empty:
        e[:, 0] = [10, 11, 12, 13]
        e[:, 1] = [0, 4, 8, 12]
        e[:, 3] = [0, 9, 12, 10]
        e[:, 6] = [0, 1, 0, 1]
        e[:, 7] = 1
        e[:, 28] = [1, 0, 1, 1]
    raw = b"NAC1" + struct.pack("<8i", w, h, 0, tick, len(e), 500, nd, gid)
    raw += bytes((4 + nd) * w * h) + e.tobytes() + bytes([1]) * (14 * w * h)
    return parse_context(raw)


def action(op, c):
    a = {k: 0 for k in FIELDS}
    a.update(op=op, delay=1, workers=3, future=3, radius=4, r0=2, r2=1, priority=1)
    # Inactive fields are canonical zero, as on the wire.
    active = set(PARAMS[op] + ["op", "delay"])
    active |= {"x", "y"} if "target" in active else set()
    a = {k: (v if k in active else 0) for k, v in a.items()}
    if "source" in active:
        a["source"] = int(c.entities[np.flatnonzero(c.source_mask(op))[0], 0])
    if op == 1:
        a["radius"] = 256
    a["area"] = np.zeros(c.w * c.h if op in (14, 15, 16) else 0, np.uint8)
    if op in (14, 15, 16):
        a["area"][3:6] = 1
    return a


class ContractTests(unittest.TestCase):
    def test_terminal_classification(self):
        from neurotica_run import end_reason

        self.assertEqual(end_reason(-1, 100, 100), "cap")
        self.assertEqual(end_reason(-1, 50, 100), "draw")
        self.assertEqual(end_reason(0, 50, 100), "win")
        self.assertEqual(end_reason(1, 50, 100), "loss")
        with self.assertRaises(ValueError):
            end_reason(3, 50, 100)

    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def setUp(self):
        torch.manual_seed(1)
        self.c = context()
        self.net = NeuroticaNet(65, width=8, hidden=32)

    def test_wire_roundtrip(self):
        for op in range(18):
            a = action(op, self.c)
            validate_action(self.c, a)
            self.assertEqual(pack_action(a), pack_action(unpack_action(pack_action(a))))

    def test_actor_learner_likelihood(self):
        g = torch.Generator().manual_seed(5)
        for _ in range(20):
            ev = self.net.score_action(self.c, generator=g)
            scored = self.net.score_action(self.c, ev["action"])
            torch.testing.assert_close(ev["logp"], scored["logp"])

    def test_all_executed_heads_get_gradients(self):
        for op in range(18):
            self.net.zero_grad(set_to_none=True)
            ev = self.net.score_action(self.c, action(op, self.c))
            (-ev["logp"]).backward()
            for key in PARAMS[op] + ["op", "delay"]:
                module = {
                    "source": self.net.source_query,
                    "target": self.net.target_query,
                    "area": self.net.area_query,
                }.get(key)
                if module is None:
                    module = self.net.heads["f_" + key]
                grad = sum(
                    float(p.grad.abs().sum())
                    for p in module.parameters()
                    if p.grad is not None
                )
                # A one-source operation legitimately has no source choice.
                if op == 1 and key == "radius":
                    continue
                if key == "source" and self.c.source_mask(op).sum() == 1:
                    continue
                self.assertGreater(grad, 0, (op, key))

    def test_type_changes_likelihood(self):
        a = action(1, self.c)
        b = dict(a, type=1)
        self.assertNotEqual(
            float(self.net.score_action(self.c, a)["logp"].detach()),
            float(self.net.score_action(self.c, b)["logp"].detach()),
        )

    def test_hold_remains_when_no_buildings(self):
        c = context(empty=True)
        self.assertTrue(c.op_mask()[0])
        self.assertFalse(c.op_mask()[7])
        ev = self.net.score_action(c, action(0, c))
        self.assertTrue(torch.isfinite(ev["logp"]))

    def test_shaping_telescopes(self):
        for g in (1.0, 0.999):
            discounts = np.array([g, g**2, g**4])
            weights = np.r_[1, np.cumprod(discounts)[:-1]]
            for phi in ([0, 0.2, 0.4], [0, -0.3, 0.1]):
                r = compute_rewards(phi, 1, discounts, 1.0)
                self.assertAlmostEqual(float(weights @ r), weights[-1], places=6)

    def test_cap_no_position_bonus(self):
        for phi in ([0, 0.2, 0.4], [0, -0.3, 0.1]):
            self.assertAlmostEqual(
                float(compute_rewards(phi, 0, np.ones(3)).sum()), 0.0, places=6
            )

    def test_truncation_requires_bootstrap(self):
        with self.assertRaises(ValueError):
            gae([0], [0.7], [1], [1], terminal=False)
        _, ret = gae([0], [0.7], [1], [1], terminal=False, next_value=0.8)
        self.assertAlmostEqual(float(ret[0]), 0.8, places=6)

    def test_wrap_anchor(self):
        mb = np.zeros((13, 16, 16), np.float32)
        for x in (15, 1):
            for dy in (0, 1):
                for dx in (0, 1):
                    mb[1, 4 + dy, (x + dx) % 16] = 1
        _, cnt = anchors(mb, np.zeros((16, 16)))
        self.assertEqual(cnt[1], 2)

    def test_checkpoint_strict(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "p.pt"
            atomic_save(self.net.checkpoint(), p)
            n, ck = NeuroticaNet.load(p)
            self.assertEqual(n.config, self.net.config)
            torch.save({"model": self.net.state_dict()}, p)
            with self.assertRaises(ValueError):
                NeuroticaNet.load(p)

    def test_group_split(self):
        with tempfile.TemporaryDirectory() as d:
            for g in (1, 2, 3):
                p = Path(d) / f"g{g}.p0.nac"
                with p.open("wb") as f:
                    for tick in (0, 25):
                        c = context(tick, g)
                        raw = (
                            struct.pack("<I", len(c.raw))
                            + c.raw
                            + pack_action(action(0, c))
                        )
                        z = zlib.compress(raw)
                        f.write(struct.pack("<I", len(z)) + z)
                (Path(d) / f"g{g}.json").write_text(
                    json.dumps(
                        dict(status="complete", ticks=50, generator="test", map_seed=g)
                    )
                )
            ds = OrderCorpus(d)
            tr, va, groups = ds.split()
            self.assertFalse(
                set(ds.records[i]["path"] for i in tr)
                & set(ds.records[i]["path"] for i in va)
            )

    def test_paired_validation(self):
        r = dict(
            id=1,
            generator="x",
            map_seed=1,
            game_seed=2,
            opponent="numbi",
            max_ticks=40,
            status="complete",
            winner=1,
            map_sha256="x",
        )
        self.assertEqual(paired_summary([r], [dict(r, winner=0)])["gained_wins"], 1)
        with self.assertRaises(ValueError):
            paired_summary([r], [dict(r, status="invalid")])
        with self.assertRaises(ValueError):
            paired_summary([r, r], [r, r])

    def test_tiny_order_overfit(self):
        a = action(7, self.c)
        opt = torch.optim.Adam(self.net.parameters(), lr=0.01)
        before = float(
            imitation_loss(self.net.score_action(self.c, a), self.c).detach()
        )
        for _ in range(40):
            opt.zero_grad()
            loss = imitation_loss(self.net.score_action(self.c, a), self.c)
            loss.backward()
            opt.step()
        self.assertLess(float(loss.detach()), before * 0.2)
        self.assertEqual(
            pack_action(self.net.score_action(self.c, deterministic=True)["action"]),
            pack_action(a),
        )


if __name__ == "__main__":
    unittest.main()
