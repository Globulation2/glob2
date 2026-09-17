#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Policy server: runs the Neurotica network for one or more live games.

Game clients connect over a Unix socket, hand over observation planes, and get
back a desired-state field. The protocol is specified in
src/ai/neurotica/NeuroticaPolicySocket.h, which is the authority.

Requests from all connected clients are batched before each forward pass. That
matters more than it might look: a headless game runs far faster than real
time, so a single 2-team match asks for roughly 360 inferences per wall second,
and the GPU is the binding constraint long before the socket is. Batching
across clients is what turns many small requests into one efficient one.

Exploration. The policy is a deterministic function of (observation, latent);
`--latent-std` draws a per-connection z that is held for the whole game and fed
through FiLM. Sampling per cell instead would explore by flipping individual
tiles, which is not a strategically meaningful direction and makes the PPO
importance ratio hopeless over ~16k dimensions.
"""

from __future__ import annotations

import argparse
import os
import selectors
import socket
import struct
import sys
import time

import numpy as np
import torch

from neurotica_net import NeuroticaNet, MIX_PRESETS, NUM_BUILDING_CLASSES
from neurotica_decode import anchors

MAGIC = b"NPS4"

# Plane ranges inside the DYNAMIC stack (NeuroticaObservation.h order):
# 8 resources, 13 own buildings, level/site/workers, 13 enemy buildings,
# ally, then units.
MY_BUILDING_SLICE = slice(8, 21)

# Matches Neurotica::DONT_CARE in NeuroticaDesiredState.h: leave this cell
# alone, neither building on it nor demolishing what stands there.
DONT_CARE = 255


ENEMY_BUILDING_SLICE = slice(24, 37)
MY_UNIT_SLICE = slice(38, 41)
ENEMY_UNIT_SLICE = slice(41, 44)


def flush_trajectory(client, record_dir: str) -> None:
    """Write one episode. The outcome is filled in later by the driver, which
    is the only party that knows who won."""
    if not client.logps:
        return
    import json
    base = os.path.join(record_dir, f"g{client.game_id}_t{client.team_seen}")
    np.save(base + ".obs.npy", np.stack(client.obs_u8))
    # Static planes and ticks travel with the episode: PPO has to rebuild the
    # exact network input, and storing only the dynamic planes would leave it
    # six channels short.
    np.savez(base + ".npz",
             placements=np.stack(client.latents).astype(np.int64),
             logps=np.array(client.logps, dtype=np.float32),
             values=np.array(client.values, dtype=np.float32),
             potentials=np.array(client.potentials, dtype=np.float32),
             mixes=np.array(client.mixes, dtype=np.int64),
             mix_decided=np.array(client.decided, dtype=bool),
             allowed=(np.stack(client.allowed) if client.allowed
                      else np.zeros((0, 0), dtype=np.uint8)),
             ticks=np.array(client.ticks, dtype=np.int64),
             static=client.static_u8)
    with open(base + ".json", "w") as fh:
        json.dump({"game_id": int(client.game_id), "team": int(client.team_seen),
                   "steps": len(client.logps), "outcome": None}, fh)


class Client:
    """One connected game team, with its static planes and fixed latent."""

    def __init__(self, conn: socket.socket, latent_std: float, latent_dim: int):
        self.conn = conn
        self.conn.setblocking(True)
        head = _recv_exact(conn, 16)
        if head is None or head[:4] != MAGIC:
            raise ValueError("bad handshake")
        self.w, self.h, self.n_static, self.n_dynamic = struct.unpack_from("<HHBB", head, 4)
        (self.game_id,) = struct.unpack_from("<I", head, 12)
        static_raw = _recv_exact(conn, self.n_static * self.w * self.h)
        if static_raw is None:
            raise ValueError("short static planes")
        self.static_u8 = (np.frombuffer(static_raw, dtype=np.uint8)
                          .reshape(self.n_static, self.h, self.w).copy())
        self.static = self.static_u8.astype(np.float32) / 255.0
        self.dyn_bytes = self.n_dynamic * self.w * self.h
        self.latent = (np.random.randn(latent_dim).astype(np.float32) * latent_std
                       if latent_std > 0 else np.zeros(latent_dim, dtype=np.float32))
        self.conn.setblocking(False)
        self.buf = bytearray()
        # Trajectory buffers, only populated when recording.
        self.obs_u8: list = []
        self.latents: list = []
        self.logps: list = []
        self.values: list = []
        self.potentials: list = []
        self.mixes: list = []
        self.decided: list = []
        self.allowed: list = []
        self.held_mix = None
        self.mix_step = 0
        self.ticks: list = []


def _recv_exact(conn: socket.socket, n: int):
    out = bytearray()
    while len(out) < n:
        chunk = conn.recv(n - len(out))
        if not chunk:
            return None
        out.extend(chunk)
    return bytes(out)


def _send_all(conn: socket.socket, data: bytes) -> bool:
    try:
        conn.setblocking(True)
        conn.sendall(data)
        conn.setblocking(False)
        return True
    except OSError:
        return False


def build_input(client: Client, dynamic: np.ndarray, tick: int) -> np.ndarray:
    t = np.float32(min(tick, 60000) / 60000.0)
    tick_planes = np.stack([
        np.full((client.h, client.w), t, dtype=np.float32),
        np.full((client.h, client.w), np.float32(np.sqrt(t)), dtype=np.float32)])
    return np.concatenate([client.static, dynamic.astype(np.float32) / 255.0,
                           tick_planes], axis=0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--socket", default="/tmp/neurotica.sock")
    ap.add_argument("--checkpoint", required=True)
    ap.add_argument("--device", default="cuda:0")
    ap.add_argument("--max-batch", type=int, default=16)
    ap.add_argument("--batch-wait-ms", type=float, default=2.0)
    ap.add_argument("--latent-std", type=float, default=0.0)
    ap.add_argument("--latent-dim", type=int, default=32)
    ap.add_argument("--reload-from", default=None,
                    help="watch this checkpoint and hot-reload when it changes, "
                         "so a PPO update takes effect without restarting the "
                         "server and dropping every in-flight game")
    ap.add_argument("--reload-every", type=float, default=30.0,
                    help="seconds between checkpoint mtime checks")
    ap.add_argument("--record-dir", default=None,
                    help="write one trajectory per connection here, for PPO")
    ap.add_argument("--sample", action="store_true",
                    help="sample placements from the policy instead of taking "
                         "the field's argmax (required for on-policy RL)")
    ap.add_argument("--placements", type=int, default=8,
                    help="new placements per policy step (sampled under "
                         "--sample, highest-probability under --top-k)")
    ap.add_argument("--ratio", action="store_true",
                    help="send the swarm unit mix (NPS4). Off leaves it at "
                         "the engine default, which is workers-only forever.")
    ap.add_argument("--staffing", action="store_true",
                    help="send the staffing head's maxUnitWorking (NPS3). "
                         "Off sends DONT_CARE everywhere, which is the "
                         "pre-NPS3 behaviour and the A/B control.")
    ap.add_argument("--use-count", action="store_true",
                    help="bound each type by the count head's prediction "
                         "(requires a checkpoint trained with one)")
    ap.add_argument("--min-base", type=int, default=4,
                    help="the deadlock breaker only fires while fewer than "
                         "this many buildings are held")
    ap.add_argument("--mix-hold", type=int, default=10,
                    help="policy steps to hold a chosen production mix before "
                         "re-choosing; at the default period of 25 ticks, 10 "
                         "is 250 ticks")
    ap.add_argument("--top-k", action="store_true",
                    help="deterministic deployment: take the --placements "
                         "most probable cells. Without this or --sample the "
                         "field is a per-cell argmax, which is empty in "
                         "practice -- see the note in the serve loop.")
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    in_planes = ckpt.get("in_planes", 60)
    width = ckpt.get("args", {}).get("width", 48)
    net = NeuroticaNet(in_planes, width=width).to(args.device)
    # Checkpoints from before the count head exist; load what matches.
    net.load_state_dict(ckpt["model"], strict=False)
    net.eval().to(memory_format=torch.channels_last)
    print(f"loaded {args.checkpoint}: {in_planes} planes, width {width}, "
          f"metrics {ckpt.get('metrics')}", flush=True)

    if os.path.exists(args.socket):
        os.unlink(args.socket)
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(args.socket)
    listener.listen(64)
    listener.setblocking(False)
    sel = selectors.DefaultSelector()
    sel.register(listener, selectors.EVENT_READ, data=None)
    print(f"listening on {args.socket}", flush=True)

    if args.record_dir:
        os.makedirs(args.record_dir, exist_ok=True)
    clients = {}
    served = 0
    t_start = time.time()
    last_reload_check = time.time()
    reload_mtime = (os.path.getmtime(args.reload_from)
                    if args.reload_from and os.path.exists(args.reload_from) else 0.0)

    while True:
        pending = []          # (client, dynamic planes, tick)
        deadline = None
        while True:
            timeout = 0.05 if not pending else max(
                0.0, (deadline - time.time()) if deadline else 0.0)
            for key, _mask in sel.select(timeout=timeout):
                if key.data is None:
                    conn, _ = listener.accept()
                    try:
                        client = Client(conn, args.latent_std, args.latent_dim)
                    except Exception as exc:
                        print("rejected client:", exc, flush=True)
                        conn.close()
                        continue
                    sel.register(conn, selectors.EVENT_READ, data=client)
                    clients[conn.fileno()] = client
                    print(f"client connected: {client.w}x{client.h}, "
                          f"{client.n_dynamic} planes", flush=True)
                    continue
                client = key.data
                try:
                    client.conn.setblocking(True)
                    head = _recv_exact(client.conn, 8)
                    if head is None:
                        raise ConnectionError
                    tick, team = struct.unpack_from("<IB", head, 0)
                    client.team_seen = team
                    payload = _recv_exact(client.conn, client.dyn_bytes)
                    if payload is None:
                        raise ConnectionError
                    client.conn.setblocking(False)
                except (OSError, ConnectionError):
                    sel.unregister(client.conn)
                    clients.pop(client.conn.fileno(), None)
                    client.conn.close()
                    if args.record_dir:
                        try:
                            flush_trajectory(client, args.record_dir)
                        except Exception as exc:
                            print("trajectory flush failed:", exc, flush=True)
                    print("client disconnected", flush=True)
                    continue
                dynamic = np.frombuffer(payload, dtype=np.uint8).reshape(
                    client.n_dynamic, client.h, client.w)
                pending.append((client, dynamic, tick))
                if deadline is None:
                    deadline = time.time() + args.batch_wait_ms / 1000.0
            if pending and (len(pending) >= args.max_batch or
                            (deadline and time.time() >= deadline)):
                break
            if not pending and not clients:
                time.sleep(0.01)

        # Hot-reload between batches, never mid-batch: a policy that changed
        # halfway through a forward pass would make the recorded log-probs
        # disagree with the weights that produced them, which silently
        # corrupts the PPO ratio.
        if args.reload_from and time.time() - last_reload_check > args.reload_every:
            last_reload_check = time.time()
            try:
                mtime = os.path.getmtime(args.reload_from)
                if mtime > reload_mtime:
                    fresh = torch.load(args.reload_from, map_location="cpu",
                                       weights_only=False)
                    net.load_state_dict(fresh["model"])
                    net.eval().to(args.device).to(memory_format=torch.channels_last)
                    reload_mtime = mtime
                    print(f"reloaded policy from {args.reload_from}", flush=True)
            except Exception as exc:
                print("reload failed (keeping current policy):", exc, flush=True)

        batch = np.stack([build_input(c, d, t) for c, d, t in pending])
        x = torch.from_numpy(batch).to(args.device).to(memory_format=torch.channels_last)
        # Which cells this team already occupies. MY_BUILDING_SLICE indexes the
        # dynamic planes; in the assembled input they sit after the 4 static
        # planes.
        my_buildings = x[:, 4 + MY_BUILDING_SLICE.start:4 + MY_BUILDING_SLICE.stop]
        existing = my_buildings.amax(dim=1) > 0.5

        # Anchors and per-type counts, level-aware, from the raw planes. The
        # DP_MY_BUILDING_LEVEL plane immediately follows the 13 type planes.
        anc, cnt = [], []
        for (_c, dyn, _t) in pending:
            a_, c_ = anchors(dyn[MY_BUILDING_SLICE].astype(np.float32) / 255.0,
                             dyn[MY_BUILDING_SLICE.stop].astype(np.float32) / 255.0)
            anc.append(a_); cnt.append(c_)
        anchor_ = torch.from_numpy(np.stack(anc)).to(x.device)          # (B,13,H,W)
        have = torch.from_numpy(np.stack(cnt)).to(x.device)             # (B,13)

        with torch.no_grad(), torch.autocast("cuda", dtype=torch.float16):
            out = net(x)
        probs = torch.softmax(out["building"].float(), dim=1)
        score = (probs.amax(dim=1) * 255).clamp(0, 255).to(torch.uint8)
        best_type = probs[:, 1:].argmax(dim=1) + 1                      # (B,H,W) 1..13
        bt = best_type.flatten(1)
        occ = (1.0 - probs[:, 0]).masked_fill(existing, 0.0).flatten(1)

        # ONE decode for eval and self-play. The review found four ways the two
        # paths differed (budget semantics, deadlock handling, k, period), which
        # meant PPO was optimising an agent nobody measured. Everything below
        # is shared; only the final draw differs -- sampled or top-k.
        #
        # Per-type budget: the count head says how many of each type to hold,
        # anchors say how many are held. Never let it forbid everything: a
        # stunted base is out of distribution and draws a low prediction, which
        # forbids building, which keeps it stunted (measured: 2 buildings in
        # 20000 ticks). The best-scoring type always keeps one slot.
        if args.use_count and "count" in out:
            caps = torch.expm1(out["count"].float().clamp(max=6.0)).round()
            caps = caps.clamp(min=0, max=64).to(torch.long)
        else:
            caps = torch.full_like(have, 64)
        allow = (caps - have).clamp(min=0)                              # (B,13)
        # Only for a genuinely small base. Once the base matches the predicted
        # composition every type is at cap, and an ungated breaker then hands
        # the top-scoring type one slot EVERY step: measured hospital=8 against
        # a cap of 4 in a 6500-tick smoke game. Past the floor, trust the head;
        # its caps grow with the tick planes as the game goes on.
        stalled = (allow.sum(dim=1) == 0) & (have.sum(dim=1) < args.min_base)
        if bool(stalled.any()):
            top_type = bt.gather(1, occ.argmax(dim=1, keepdim=True))
            unblock = torch.zeros_like(allow)
            unblock.scatter_(1, (top_type.long() - 1).clamp(min=0), 1)
            allow = torch.where(stalled.unsqueeze(1), unblock, allow)
        allowed = (~existing).flatten(1) & (allow.gather(1, (bt.long() - 1)) > 0)
        for t in range(allow.shape[1]):
            of_type = allowed & (bt == (t + 1))
            over = of_type.sum(dim=1) > allow[:, t]
            if not bool(over.any()):
                continue
            ranked = occ.masked_fill(~of_type, -1.0).argsort(dim=1, descending=True)
            rank_pos = torch.empty_like(ranked)
            rank_pos.scatter_(1, ranked, torch.arange(ranked.shape[1], device=x.device)
                              .expand_as(ranked))
            within = rank_pos < allow[:, t].unsqueeze(1)
            allowed = torch.where(over.unsqueeze(1) & of_type, of_type & within, allowed)

        # The production mix is held for --mix-hold policy steps: re-drawing a
        # 5-way choice every step made swarms thrash between mixes, and the
        # episode log showed the policy getting worse while doing it.
        decide = torch.tensor([(c.mix_step % args.mix_hold == 0) or c.held_mix is None
                               for (c, _d, _t) in pending], device=x.device)
        if args.sample:
            with torch.no_grad(), torch.autocast("cuda", dtype=torch.float16):
                act = net.act_placements(x, existing, k=args.placements,
                                         allowed=allowed, decide_mix=decide, out=out)
            idx = act["placements"]
            proposed_mix = act["mix_choice"]
            logp = act["logp"]
        else:
            k = min(args.placements, occ.shape[1])
            idx = (occ * allowed.float()).topk(k, dim=1).indices
            proposed_mix = out["mix"].argmax(dim=1)
            logp = None
        mix_used = []
        for n, (client, _d, _t) in enumerate(pending):
            if bool(decide[n]):
                client.held_mix = int(proposed_mix[n])
            client.mix_step += 1
            mix_used.append(client.held_mix)

        # The field: anchors re-assert what stands, covered non-anchor cells
        # are DONT_CARE (not ours to decide), placements go where drawn. Under
        # top-k a draw can land on a disallowed cell when fewer than k are
        # allowed, so it is masked; under sampling every draw is an action the
        # policy is credited for and must reach the field.
        anchor_any = anchor_.any(dim=1)
        anchor_type = (anchor_.to(torch.uint8).argmax(dim=1).to(torch.uint8) + 1)
        base = torch.where(anchor_any, anchor_type,
                           torch.where(existing, torch.full((), DONT_CARE, dtype=torch.uint8,
                                                            device=x.device).expand_as(anchor_type),
                                       torch.zeros_like(anchor_type)))
        flat = base.flatten(1)
        want = bt.gather(1, idx).to(torch.uint8)
        if not args.sample:
            want = torch.where(allowed.gather(1, idx), want, flat.gather(1, idx))
        flat.scatter_(1, idx, want)
        cls = flat.view_as(anchor_type)

        areas = (torch.sigmoid(out["areas"].float()) > 0.5)
        area_bits = (areas[:, 0].to(torch.uint8) * 1 + areas[:, 1].to(torch.uint8) * 2
                     + areas[:, 2].to(torch.uint8) * 4)
        if args.staffing and "workers" in out:
            want_workers = out["workers"].float().clamp(min=0).round().clamp(0, 200).to(torch.uint8)
            staffing = torch.where((cls > 0) & (cls != DONT_CARE), want_workers,
                                   torch.full_like(cls, DONT_CARE))
        else:
            staffing = torch.full_like(cls, DONT_CARE)
        if args.ratio:
            presets = torch.tensor(MIX_PRESETS, device=x.device, dtype=torch.uint8)
            chosen = presets[torch.tensor(mix_used, device=x.device)]         # (B,3)
            is_swarm = (cls == 1)                                             # SWARM + 1
            r = [torch.where(is_swarm, chosen[:, j].view(-1, 1, 1).expand_as(cls),
                             torch.full_like(cls, DONT_CARE)) for j in range(3)]
        else:
            r = [torch.full_like(cls, DONT_CARE) for _ in range(3)]
        packed = torch.stack([cls, score, area_bits, staffing, r[0], r[1], r[2]],
                             dim=-1).cpu().numpy()

        if args.record_dir:
            allowed_np = allowed.cpu().numpy()
            for n, (client, dyn, _tick) in enumerate(pending):
                client.obs_u8.append(dyn.copy())
                client.ticks.append(int(_tick))
                if args.sample:
                    client.latents.append(idx[n].cpu().numpy().astype(np.int64))
                    client.logps.append(float(logp[n]))
                    client.mixes.append(mix_used[n])
                    client.decided.append(bool(decide[n]))
                    # Stored so PPO scores the distribution that acted, not one
                    # recomputed from a trunk it has since moved.
                    client.allowed.append(np.packbits(allowed_np[n]))
                if "value" in out:
                    client.values.append(float(out["value"][n]))
                mine = float((dyn[MY_BUILDING_SLICE] > 0).sum() +
                             (dyn[MY_UNIT_SLICE] > 0).sum())
                theirs = float((dyn[ENEMY_BUILDING_SLICE] > 0).sum() +
                               (dyn[ENEMY_UNIT_SLICE] > 0).sum())
                client.potentials.append((mine - theirs) / 500.0)

        for (client, _d, _t), reply in zip(pending, packed):
            if not _send_all(client.conn, reply.tobytes()):
                sel.unregister(client.conn)
                clients.pop(client.conn.fileno(), None)
                client.conn.close()
        served += len(pending)
        if served % 200 < len(pending):
            print(f"served {served} ({served/(time.time()-t_start):.0f}/s, "
                  f"batch {len(pending)})", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
