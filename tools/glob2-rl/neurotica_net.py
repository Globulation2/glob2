# SPDX-License-Identifier: GPL-3.0-or-later
"""Circular U-Net plus an autoregressive distribution over executable orders.
BC and PPO call the same score_action function. All executed choices have a
likelihood, including building type, entity identity, parameters and delay.
"""

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from neurotica_actions import PARAMS, SIZES, SCHEMA, validate_action, checkpoint_id


def circ_pad(x: torch.Tensor, p: int) -> torch.Tensor:
    return F.pad(x, (p, p, p, p), mode="circular")


class SepConv(nn.Module):
    """Depthwise 3x3 (circular) + pointwise 1x1, with norm and activation."""

    def __init__(self, cin: int, cout: int):
        super().__init__()
        self.dw = nn.Conv2d(cin, cin, 3, padding=0, groups=cin, bias=False)
        self.pw = nn.Conv2d(cin, cout, 1, bias=False)
        self.norm = nn.GroupNorm(8, cout)

    def forward(self, x):
        x = self.dw(circ_pad(x, 1))
        x = self.pw(x)
        return F.silu(self.norm(x))


class DenseConv(nn.Module):
    def __init__(self, cin: int, cout: int):
        super().__init__()
        self.conv = nn.Conv2d(cin, cout, 3, padding=0, bias=False)
        self.norm = nn.GroupNorm(8, cout)

    def forward(self, x):
        return F.silu(self.norm(self.conv(circ_pad(x, 1))))


class Block(nn.Module):
    """Residual pair, separable at high resolution and dense at low."""

    def __init__(self, ch: int, separable: bool):
        super().__init__()
        Conv = SepConv if separable else DenseConv
        self.a = Conv(ch, ch)
        self.b = Conv(ch, ch)

    def forward(self, x):
        return x + self.b(self.a(x))


class NeuroticaNet(nn.Module):
    def __init__(self, in_planes=65, width=32, hidden=128):
        super().__init__()
        self.config = dict(
            in_planes=in_planes, width=width, hidden=hidden, schema=SCHEMA
        )
        self.stem = nn.Conv2d(in_planes * 2, width, 1)
        self.enc = nn.ModuleList(
            [Block(width, True), Block(width * 2, True), Block(width * 4, False)]
        )
        self.down = nn.ModuleList(
            [nn.Conv2d(width * 2**i, width * 2 ** (i + 1), 2, 2) for i in range(3)]
        )
        self.mid = Block(width * 8, False)
        self.up = nn.ModuleList(
            [
                nn.ConvTranspose2d(width * 2 ** (i + 1), width * 2**i, 2, 2)
                for i in (2, 1, 0)
            ]
        )
        self.dec = nn.ModuleList(
            [Block(width * 4, False), Block(width * 2, True), Block(width, True)]
        )
        self.global_fc = nn.Linear(width * 8, hidden)
        self.entity = nn.Sequential(
            nn.Linear(29, hidden), nn.SiLU(), nn.Linear(hidden, hidden)
        )
        self.global_extra = nn.Linear(hidden + 3, hidden)
        self.position = nn.Linear(4, hidden, bias=False)
        self.source_query = nn.Linear(hidden, hidden)
        self.spatial = nn.Conv2d(width, hidden, 1)
        self.target_query = nn.Linear(hidden, hidden)
        self.area_query = nn.Linear(hidden, hidden)
        self.heads = nn.ModuleDict(
            {"f_" + k: nn.Linear(hidden, n) for k, n in SIZES.items()}
        )
        self.embeddings = nn.ModuleDict(
            {"f_" + k: nn.Embedding(n, hidden) for k, n in SIZES.items()}
        )
        self.update = nn.GRUCell(hidden, hidden)
        self.value = nn.Linear(hidden, 1)

    def encode_context(self, c, previous=None):
        device = next(self.parameters()).device
        planes = c.planes()
        prev = previous.planes() if previous is not None else planes
        if prev.shape != planes.shape:
            raise ValueError("history shape mismatch")
        x = torch.from_numpy(np.concatenate([planes, prev])).unsqueeze(0).to(device)
        x = self.stem(x)
        skips = []
        for enc, down in zip(self.enc, self.down):
            x = enc(x)
            skips.append(x)
            x = down(x)
        x = self.mid(x)
        state = torch.tanh(self.global_fc(x.mean((2, 3))))
        for up, dec, skip in zip(self.up, self.dec, reversed(skips)):
            x = dec(up(x) + skip)
        spatial = self.spatial(x).flatten(2).squeeze(0).T
        yy, xx = torch.meshgrid(
            torch.arange(c.h, device=device),
            torch.arange(c.w, device=device),
            indexing="ij",
        )
        coords = torch.stack(
            [
                (xx * 2 * torch.pi / c.w).sin(),
                (xx * 2 * torch.pi / c.w).cos(),
                (yy * 2 * torch.pi / c.h).sin(),
                (yy * 2 * torch.pi / c.h).cos(),
            ],
            -1,
        ).reshape(-1, 4)
        spatial = spatial + self.position(coords)
        e = c.entities.astype(np.float32).copy()
        if len(e):
            e[:, 0] = 0  # identity is a pointer, not a numerical feature
            e[:, 1] /= c.w
            e[:, 2] /= c.h
            # Signed log preserves inventories, health and exact staffing ranges.
            e[:, 3:] = np.sign(e[:, 3:]) * np.log1p(np.abs(e[:, 3:]))
        entities = self.entity(torch.from_numpy(e).to(device))
        pooled = entities.mean(0, keepdim=True) if len(e) else torch.zeros_like(state)
        # Entity controls and elapsed time must reach the opcode/value heads,
        # not only the source pointer after an operation is already selected.
        time = torch.tensor(
            [
                [
                    c.tick / 60000,
                    np.log1p(c.tick) / 10,
                    (c.tick - previous.tick) / 25 if previous else 0,
                ]
            ],
            device=device,
            dtype=state.dtype,
        )
        state = torch.tanh(state + self.global_extra(torch.cat([pooled, time], 1)))
        return state, spatial, entities

    def score_action(
        self, c, action=None, previous=None, deterministic=False, generator=None
    ):
        """Teacher-forcing and sampling share every distribution and mask.
        Entropy is summed over active factors; report separately for diagnostics.
        A single order is emitted, so the next context includes its consequences.
        """
        if action is not None:
            validate_action(c, action)
        state, spatial, entities = self.encode_context(c, previous)
        value = self.value(state).squeeze()
        device = state.device
        chosen = {} if action is None else dict(action)
        terms = {}
        entropies = {}

        def categorical(key, logits, mask=None, label=None):
            nonlocal state
            logits = logits.flatten().float()
            if mask is not None:
                m = torch.as_tensor(mask, dtype=torch.bool, device=device)
                if not bool(m.any()):
                    raise ValueError(f"empty {key} support")
                logits = logits.masked_fill(~m, -torch.inf)
            dist = torch.distributions.Categorical(logits=logits)
            if label is None:
                index = (
                    logits.argmax()
                    if deterministic
                    else torch.multinomial(dist.probs, 1, generator=generator).squeeze(
                        0
                    )
                )
            else:
                index = torch.tensor(int(label), device=device)
            terms[key] = dist.log_prob(index)
            entropies[key] = dist.entropy()
            return int(index)

        op = categorical(
            "op",
            self.heads["f_op"](state),
            c.op_mask(),
            None if action is None else action["op"],
        )
        chosen["op"] = op
        state = self.update(
            self.embeddings["f_op"](torch.tensor([op], device=device)), state
        )
        for key in PARAMS[op] + ["delay"]:
            if key == "source":
                label = (
                    None
                    if action is None
                    else int(np.flatnonzero(c.entities[:, 0] == action["source"])[0])
                )
                i = categorical(
                    key,
                    entities @ self.source_query(state).squeeze(0),
                    c.source_mask(op),
                    label,
                )
                chosen[key] = int(c.entities[i, 0])
                embedding = entities[i : i + 1]
            elif key == "target":
                mask = c.legal[chosen["type"]] if op == 1 else c.discovered
                label = None if action is None else action["y"] * c.w + action["x"]
                i = categorical(
                    key, spatial @ self.target_query(state).squeeze(0), mask, label
                )
                chosen["x"] = i % c.w
                chosen["y"] = i // c.w
                embedding = spatial[i : i + 1]
            elif key == "area":
                logits = (spatial @ self.area_query(state).squeeze(0)).float()
                dist = torch.distributions.Bernoulli(logits=logits)
                if action is None:
                    bits = (
                        (logits > 0).float()
                        if deterministic
                        else (
                            torch.rand(logits.shape, device=device, generator=generator)
                            < dist.probs
                        ).float()
                    )
                else:
                    bits = torch.as_tensor(
                        action["area"], device=device, dtype=torch.float32
                    )
                terms[key] = dist.log_prob(bits).sum()
                entropies[key] = dist.entropy().sum()
                chosen[key] = bits.detach().cpu().numpy().astype(np.uint8)
                embedding = (spatial * bits[:, None]).sum(
                    0, keepdim=True
                ) / bits.sum().clamp(min=1)
            else:
                mask = None
                if key == "workers" and op == 6:
                    mask = np.arange(256) <= 20
                if key == "type":
                    mask = c.legal.any(axis=1)
                if key == "radius" and op == 1 and chosen["type"] not in (8, 9, 10):
                    mask = np.arange(257) == 256
                if key == "radius" and op == 8:
                    mask = np.arange(257) < 256
                if op == 17 and key in ("r0", "r1", "r2"):
                    mask = np.arange(256) < 4
                label = (
                    None
                    if action is None
                    else action[key] - (1 if key == "delay" else 0)
                )
                i = categorical(key, self.heads["f_" + key](state), mask, label)
                chosen[key] = i + (1 if key == "delay" else 0)
                embedding = self.embeddings["f_" + key](
                    torch.tensor([i], device=device)
                )
            state = self.update(embedding, state)
        if op not in (14, 15, 16):
            chosen["area"] = np.zeros(0, np.uint8)
        validate_action(c, chosen)
        return dict(
            action=chosen,
            logp=torch.stack(list(terms.values())).sum(),
            entropy=torch.stack(list(entropies.values())).sum(),
            terms=terms,
            entropies=entropies,
            value=value,
        )

    def checkpoint(self, **extra):
        state = self.state_dict()
        return dict(
            model=state,
            config=self.config,
            policy_id=checkpoint_id(state, self.config),
            **extra,
        )

    @classmethod
    def load(cls, path, device="cpu"):
        ck = torch.load(path, map_location=device, weights_only=False)
        config = dict(ck.get("config", {}))
        if config.pop("schema", None) != SCHEMA:
            raise ValueError("legacy field checkpoint: retrain with the order corpus")
        net = cls(**config).to(device)
        net.load_state_dict(ck["model"], strict=True)
        if checkpoint_id(ck["model"], ck["config"]) != ck["policy_id"]:
            raise ValueError("checkpoint hash mismatch")
        return net, ck
