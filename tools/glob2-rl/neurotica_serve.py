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

MAGIC = b"NPS1"


class Client:
    """One connected game team, with its static planes and fixed latent."""

    def __init__(self, conn: socket.socket, latent_std: float, latent_dim: int):
        self.conn = conn
        self.conn.setblocking(True)
        head = _recv_exact(conn, 12)
        if head is None or head[:4] != MAGIC:
            raise ValueError("bad handshake")
        self.w, self.h, self.n_static, self.n_dynamic = struct.unpack_from("<HHBB", head, 4)
        static_raw = _recv_exact(conn, self.n_static * self.w * self.h)
        if static_raw is None:
            raise ValueError("short static planes")
        self.static = (np.frombuffer(static_raw, dtype=np.uint8)
                       .reshape(self.n_static, self.h, self.w).astype(np.float32) / 255.0)
        self.dyn_bytes = self.n_dynamic * self.w * self.h
        self.latent = (np.random.randn(latent_dim).astype(np.float32) * latent_std
                       if latent_std > 0 else np.zeros(latent_dim, dtype=np.float32))
        self.conn.setblocking(False)
        self.buf = bytearray()


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
    ap.add_argument("--score-floor", type=int, default=0,
                    help="drop predicted buildings whose softmax score is below "
                         "this (0..255); the reconciler still ranks by score")
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    in_planes = ckpt.get("in_planes", 60)
    width = ckpt.get("args", {}).get("width", 48)
    net = NeuroticaNet(in_planes, width=width).to(args.device)
    net.load_state_dict(ckpt["model"])
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

    clients = {}
    served = 0
    t_start = time.time()

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
                    payload = _recv_exact(client.conn, client.dyn_bytes)
                    if payload is None:
                        raise ConnectionError
                    client.conn.setblocking(False)
                except (OSError, ConnectionError):
                    sel.unregister(client.conn)
                    clients.pop(client.conn.fileno(), None)
                    client.conn.close()
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

        batch = np.stack([build_input(c, d, t) for c, d, t in pending])
        x = torch.from_numpy(batch).to(args.device).to(memory_format=torch.channels_last)
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.float16):
            out = net(x)
        logits = out["building"].float()
        probs = torch.softmax(logits, dim=1)
        cls = probs.argmax(dim=1).to(torch.uint8)
        score = (probs.amax(dim=1) * 255).clamp(0, 255).to(torch.uint8)
        if args.score_floor > 0:
            cls = torch.where(score >= args.score_floor, cls, torch.zeros_like(cls))
        areas = (torch.sigmoid(out["areas"].float()) > 0.5)
        area_bits = (areas[:, 0].to(torch.uint8) * 1 + areas[:, 1].to(torch.uint8) * 2
                     + areas[:, 2].to(torch.uint8) * 4)
        packed = torch.stack([cls, score, area_bits], dim=-1).cpu().numpy()

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
