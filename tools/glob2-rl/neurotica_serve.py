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

from neurotica_net import NeuroticaNet, NUM_BUILDING_CLASSES

MAGIC = b"NPS3"

# Plane ranges inside the DYNAMIC stack (NeuroticaObservation.h order):
# 8 resources, 13 own buildings, level/site/workers, 13 enemy buildings,
# ally, then units.
MY_BUILDING_SLICE = slice(8, 21)

# Matches Neurotica::DONT_CARE in NeuroticaDesiredState.h: leave this cell
# alone, neither building on it nor demolishing what stands there.
DONT_CARE = 255

# IntBuildingType order: swarm, inn, hospital, racetrack, swimmingpool,
# barracks, school, defencetower, explorationflag, warflag, clearingflag,
# stonewall, market. Caps are roughly the most a strong teacher holds at once
# (measured from Nicowar and Cortex telemetry), not a strategy -- just a bound
# that stops the marginal decode running away.
TYPE_CAPS = [6, 8, 4, 2, 2, 3, 3, 4, 2, 3, 3, 24, 2]
# Approximate footprint cells per building, to turn marked cells back into a
# building count. Flags and walls are 1x1; the rest are 2x2.
TYPE_CELLS = [4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 1.0, 1.0, 1.0, 1.0, 4.0]
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
    ap.add_argument("--threshold", type=float, default=0.0,
                    help="with --top-k, place on every empty cell whose "
                         "occupancy probability exceeds this, instead of a "
                         "fixed count. 0 disables (fixed --placements).")
    ap.add_argument("--max-placements", type=int, default=64,
                    help="upper bound on placements per step under --threshold")
    ap.add_argument("--count-scale", type=float, default=1.0,
                    help="multiply the predicted per-type counts before using "
                         "them as a budget; >1 lets the agent build toward the "
                         "predicted composition faster")
    ap.add_argument("--build-per-unit", type=float, default=0.0,
                    help="require this many units per building held before "
                         "allowing another; 0 disables")
    ap.add_argument("--staffing", action="store_true",
                    help="send the staffing head's maxUnitWorking (NPS3). "
                         "Off sends DONT_CARE everywhere, which is the "
                         "pre-NPS3 behaviour and the A/B control.")
    ap.add_argument("--use-count", action="store_true",
                    help="bound each type by the count head's prediction "
                         "(requires a checkpoint trained with one)")
    ap.add_argument("--type-caps", action="store_true",
                    help="bound how many of each building type may be wanted "
                         "at once (see TYPE_CAPS)")
    ap.add_argument("--top-k", action="store_true",
                    help="deterministic deployment: take the --placements "
                         "most probable cells. Without this or --sample the "
                         "field is a per-cell argmax, which is empty in "
                         "practice -- see the note in the serve loop.")
    ap.add_argument("--score-floor", type=int, default=0,
                    help="drop predicted buildings whose softmax score is below "
                         "this (0..255); the reconciler still ranks by score")
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

        with torch.no_grad(), torch.autocast("cuda", dtype=torch.float16):
            if args.sample:
                out = net.act_placements(x, existing, k=args.placements)
            else:
                out = net(x)
        logits = out["building"].float()
        probs = torch.softmax(logits, dim=1)
        cls = probs.argmax(dim=1).to(torch.uint8)
        score = (probs.amax(dim=1) * 255).clamp(0, 255).to(torch.uint8)

        # The desired field is: keep what we already have, plus exactly k new
        # placements. Everything else is explicitly empty, so under --sample the
        # action the policy is credited for is the action the reconciler
        # actually carries out.
        #
        # Selecting those k by per-cell argmax does NOT work, and quietly
        # produced an AI that did nothing at all. Buildings occupy ~0.1% of
        # cells, so class 0 ("no building") wins the argmax essentially
        # everywhere; the desired field came out empty, the reconciler found no
        # diff to close, and Neurotica sat on its starting base for entire
        # games while BC metrics looked healthy -- those are top-k ranking
        # metrics, which is exactly what argmax is not. Rank cells by occupancy
        # probability 1 - p(empty) instead, matching the candidate set that
        # NeuroticaNet.placement_distribution samples from.
        if args.sample or args.top_k:
            best_type = probs[:, 1:].argmax(dim=1).to(torch.uint8) + 1
            keep = None
            if args.sample:
                idx = out["placements"]
            else:
                occ = (1.0 - probs[:, 0]).masked_fill(existing, 0.0).flatten(1)
                if args.threshold > 0:
                    # Let the model decide how many buildings it wants. top-k
                    # imposes exactly k new placements every policy step
                    # whatever the model believes, which over-builds badly:
                    # k=12 every 25 ticks produced 94 buildings in 8000 ticks
                    # against a teacher's 20-40. A probability threshold lets
                    # the count be zero when nothing is wanted, which is what
                    # the BC labels actually encode.
                    #
                    # The selection has to be the mask itself. Ranking the
                    # masked scores with topk(n) still returns n cells --
                    # below-threshold ones included, since masking them to zero
                    # does not remove them -- so a "threshold" built that way
                    # silently stays a fixed count, and a higher threshold then
                    # produces MORE buildings rather than fewer, because fewer
                    # simultaneous desires churn through more distinct cells.
                    keep = occ > args.threshold
                    over = keep.sum(dim=1) > args.max_placements
                    if bool(over.any()):
                        cut = occ.masked_fill(~keep, 0.0).topk(
                            args.max_placements, dim=1).indices
                        capped = torch.zeros_like(keep)
                        capped.scatter_(1, cut, True)
                        keep = torch.where(over.unsqueeze(1), capped, keep)
                    idx = None
                else:
                    idx = occ.topk(min(args.placements, occ.shape[1]), dim=1).indices
            # Cells we already occupy re-assert the building that is observed
            # there, never the model's argmax. The building plane has no
            # DONT_CARE (0 means "none", 1..13 a type), so an occupied cell the
            # model is unsure about reads as "empty wanted here" and the
            # reconciler demolishes it after demolishPersistSteps. With the
            # argmax field that fired constantly: 874 created against 683
            # demolished in 8000 ticks, the AI tearing down its own base as
            # fast as it built it. Re-asserting the observed type makes the
            # field idempotent on everything already standing, so only the k
            # new placements can ever be a change.
            # Per-type budget. Telemetry says composition, not placement, is
            # what loses games: on one seed Neurotica finished with swarm=48
            # inn=57 school=19 and nothing else, against Nicowar's swarm=4
            # inn=7 hospital=3 racetrack=2 pool=2 barracks=2 school=2 -- and
            # 4 workers to Nicowar's 77. The model has learned the *marginal*
            # distribution over building types, so an uncertain cell decodes to
            # whichever type is commonest in the corpus, and thresholding turns
            # "inn-ness everywhere" into 57 inns. A per-cell marginal field has
            # no way to say "one more inn"; until a count head exists, bound it.
            # Anchors, computed once: they define both the re-assertion below
            # and the count of what we already hold.
            marked_ = my_buildings > 0.5
            anchor_ = (marked_ & ~torch.roll(marked_, 1, dims=3)
                                & ~torch.roll(marked_, 1, dims=2))

            # Not under --sample: PPO credits the policy for exactly the
            # placements it sampled, so trimming them here would train against
            # an action that was never taken.
            if (args.type_caps or args.use_count) and not args.sample:
                if keep is None:
                    # top-k hands back indices; the budget works on a mask.
                    keep = torch.zeros_like(occ, dtype=torch.bool)
                    keep.scatter_(1, idx, True)
                    idx = None
                if args.use_count and "count" in out:
                    # The model's own answer to "how many of each should I
                    # hold", which is what the per-cell marginal cannot say.
                    # --count-scale exists because the budget is a follower.
                    # allow = predicted - held, and the prediction is made from
                    # the CURRENT state, so an agent already behind the teacher
                    # distribution is only ever permitted to creep. Measured at
                    # scale 1.0: 2 buildings until tick 9728, units 5 -> 16 over
                    # 20000 ticks, against a teacher's 77 workers. Scaling the
                    # target keeps the composition the head predicts while
                    # letting the agent build toward it faster.
                    caps = torch.expm1(out["count"].float().clamp(max=6.0))
                    caps = (caps * args.count_scale).round()
                    caps = caps.clamp(min=0, max=64).to(torch.long)
                else:
                    caps = torch.tensor(TYPE_CAPS, device=cls.device).unsqueeze(0)
                # Count anchors. Dividing covered cells by a guessed footprint
                # size read 4 swarms when the team held 1, so `allow` was ~0
                # and the budget blocked everything: 2 buildings in 20000
                # ticks. The count labels are per-building, so the comparison
                # has to be per-building too.
                have = anchor_.flatten(2).sum(dim=2).to(torch.long)         # (B,13)
                allow = (caps - have).clamp(min=0)                         # (B,13)
                if os.environ.get("NEUROTICA_CAP_DEBUG"):
                    print("CAP caps=", caps[0].tolist(), "have=", have[0].tolist(),
                          "allow=", allow[0].tolist(),
                          "keepN=", int(keep[0].sum()) if keep is not None else -1,
                          flush=True)
                # Deadlock breaker. The count head predicts the count a team in
                # THIS state should hold, so a stunted base -- which is out of
                # distribution, teachers being much larger by the same tick --
                # gets a low prediction, which forbids building, which keeps it
                # stunted. Observed directly: holding 1 swarm and 1 inn, the
                # head asked for 1 swarm and 0 inns, so allow was zero on every
                # type for the rest of the game and it built nothing for 20000
                # ticks. Never let the budget forbid everything: the
                # highest-scoring type always keeps one slot, so the state can
                # walk back toward the distribution the head was trained on.
                # Population gate. Teachers build in proportion to the
                # population that has to staff the buildings; measured at tick
                # 5632 on one seed: nicowar 30 units / 6 buildings, numbi 21/1,
                # cortex 15/6, Neurotica 9/4. Numbi wins games holding ONE
                # building, because early workers belong on wheat, not on
                # construction -- a swarm only produces when wheat reaches it
                # (Building TypeSteps.cpp). Neurotica ran 0.44 buildings per
                # unit against nicowar's 0.20 and starved its own swarms.
                if args.build_per_unit > 0:
                    units = (x[:, 4 + MY_UNIT_SLICE.start:4 + MY_UNIT_SLICE.stop]
                             > 0.5).flatten(1).sum(dim=1).float()
                    budget_total = (units / args.build_per_unit).floor()
                    saturated = have.sum(dim=1).float() >= budget_total
                    allow = torch.where(saturated.unsqueeze(1),
                                        torch.zeros_like(allow), allow)

                stalled = allow.sum(dim=1) == 0
                if args.build_per_unit > 0:
                    # A saturated base is a deliberate stop, not a deadlock.
                    stalled = stalled & ~saturated
                if bool(stalled.any()):
                    top_cell = occ.masked_fill(~keep, -1.0).argmax(dim=1)
                    top_type = best_type.flatten(1).gather(1, top_cell.unsqueeze(1))
                    unblock = torch.zeros_like(allow)
                    unblock.scatter_(1, (top_type.long() - 1).clamp(min=0), 1)
                    allow = torch.where(stalled.unsqueeze(1), unblock, allow)

                bt = best_type.flatten(1)                                  # (B,HW) 1..13
                # NB: not `sel` -- that name is the selectors object driving
                # the server loop, and shadowing it crashes on the next poll.
                for t in range(len(TYPE_CAPS)):
                    of_type = keep & (bt == (t + 1))
                    over = of_type.sum(dim=1) > allow[:, t]
                    if not bool(over.any()):
                        continue
                    ranked = occ.masked_fill(~of_type, -1.0).argsort(dim=1, descending=True)
                    rank_pos = torch.empty_like(ranked)
                    rank_pos.scatter_(1, ranked,
                                      torch.arange(ranked.shape[1], device=cls.device)
                                      .expand_as(ranked))
                    within = rank_pos < allow[:, t].unsqueeze(1)
                    keep = torch.where(over.unsqueeze(1) & of_type,
                                       of_type & within, keep)

            # Re-assert existing buildings at their ANCHOR cell only.
            #
            # The desired field is anchored top-left, but the observation marks
            # every cell a building covers, and the reconciler keys observed
            # buildings by anchor alone. So repeating the type across a
            # footprint asks for a NEW building at each non-anchor cell, which
            # then covers cells of its own: swarm count ran 4 -> 28 -> 48 -> 80
            # in 1500 ticks with an empty placement set, and no per-type budget
            # could stop it, because the budget bounds placements and these
            # were never placements.
            #
            # A cell is an anchor if it is marked and the cells above and to
            # its left are not. torch.roll is exactly right here: glob2 maps
            # are toroidal, so the wrap is the real neighbour.
            anchor_any = anchor_.any(dim=1)
            anchor_type = (anchor_.float().argmax(dim=1).to(torch.uint8) + 1)
            # Covered-but-not-anchor is DONT_CARE: not ours to decide, so
            # neither built on nor demolished.
            field = torch.where(anchor_any, anchor_type,
                                torch.where(existing,
                                            torch.full_like(cls, DONT_CARE),
                                            torch.zeros_like(cls)))
            flat = field.flatten(1)
            if idx is None:
                flat = torch.where(keep, best_type.flatten(1), flat)
            else:
                flat.scatter_(1, idx, best_type.flatten(1).gather(1, idx))
            cls = flat.view_as(cls)
        elif args.score_floor > 0:
            cls = torch.where(score >= args.score_floor, cls, torch.zeros_like(cls))
        areas = (torch.sigmoid(out["areas"].float()) > 0.5)
        area_bits = (areas[:, 0].to(torch.uint8) * 1 + areas[:, 1].to(torch.uint8) * 2
                     + areas[:, 2].to(torch.uint8) * 4)
        # Staffing byte. DONT_CARE everywhere the head has nothing to say --
        # which is every cell without a building wanted on it -- so the
        # reconciler leaves those alone rather than reading a predicted 0 as
        # "unstaff this".
        if args.staffing and "workers" in out:
            want_workers = out["workers"].float().round().clamp(0, 200).to(torch.uint8)
            staffing = torch.where((cls > 0) & (cls != DONT_CARE), want_workers,
                                   torch.full_like(cls, DONT_CARE))
        else:
            # All DONT_CARE reproduces every build before NPS3, so the staffing
            # head can be A/B'd against it with one checkpoint and one binary.
            staffing = torch.full_like(cls, DONT_CARE)
        packed = torch.stack([cls, score, area_bits, staffing], dim=-1).cpu().numpy()

        if args.record_dir:
            lat = out.get("placements")
            lp = out.get("logp")
            val = out.get("value")
            for n, (client, dyn, _tick) in enumerate(pending):
                client.obs_u8.append(dyn.copy())
                client.ticks.append(int(_tick))
                if lat is not None:
                    client.latents.append(lat[n].cpu().numpy().astype(np.int64))
                    client.logps.append(float(lp[n]))
                if val is not None:
                    client.values.append(float(val[n]))
                # Potential for shaping, read straight off the planes the
                # server already holds: own buildings and units minus the
                # enemy's. Potential-based shaping is policy-invariant, so this
                # cannot introduce a strategy that wins the shaping instead of
                # the game.
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
