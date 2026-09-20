#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Immutable, seeded NPS6 policy server. Restart between PPO generations.
One model/version for every connection's entire episode. No hot reload, decode
flags, model-dependent candidate pruning, or silently compatible checkpoints.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import struct
import threading
import traceback
import zlib
import numpy as np
import torch
from neurotica_actions import SCHEMA, parse_context, pack_action
from neurotica_net import NeuroticaNet


def exact(conn, n):
    out = bytearray()
    while len(out) < n:
        chunk = conn.recv(n - len(out))
        if not chunk:
            if not out:
                return None
            raise ConnectionError("truncated message")
        out.extend(chunk)
    return bytes(out)


def atomic_json(path, data):
    tmp = Path(str(path) + ".tmp")
    tmp.write_text(json.dumps(data))
    os.replace(tmp, path)


def serve(args):
    net, ck = NeuroticaNet.load(args.checkpoint, args.device)
    net.eval()
    lock = threading.Lock()
    stop = threading.Event()
    threads = []
    if args.record_dir:
        Path(args.record_dir).mkdir(parents=True, exist_ok=True)
    # Refuse to unlink someone else's live endpoint.
    if os.path.exists(args.socket):
        raise RuntimeError("socket already exists; select a unique run socket")
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(args.socket)
    listener.listen(64)
    listener.settimeout(0.5)

    def shutdown(*_):
        stop.set()

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    def client(conn):
        records = []
        previous = None
        team = None
        status = "complete"
        error = None
        try:
            conn.settimeout(120)
            if exact(conn, 4) != b"NPS6":
                raise ValueError("NPS6 handshake required")
            generator = torch.Generator(device=args.device)
            while True:
                length = exact(conn, 4)
                if length is None:
                    break
                n = struct.unpack("<I", length)[0]
                if n > 32 * 1024 * 1024:
                    raise ValueError("oversized context")
                raw = exact(conn, n)
                if raw is None:
                    raise ConnectionError("missing context")
                c = parse_context(raw)
                if team is None:
                    team = c.team
                    # Per-game seed is sent out-of-band by the runner through a
                    # unique server per batch and the context's static map hash.
                    digest = hashlib.sha256(
                        c.static.tobytes()
                        + struct.pack("<III", args.seed, c.game_id, c.team)
                    ).digest()
                    generator.manual_seed(
                        int.from_bytes(digest[:8], "little") % (2**63 - 1)
                    )
                if c.team != team:
                    raise ValueError("team changed on connection")
                with lock, torch.no_grad():
                    ev = net.score_action(
                        c,
                        previous=previous,
                        deterministic=not args.sample,
                        generator=generator,
                    )
                a = pack_action(ev["action"])
                conn.sendall(struct.pack("<I", len(a)) + a)
                if args.record_dir:
                    records.append(
                        dict(
                            context=zlib.compress(raw, 1),
                            action=a,
                            logp=float(ev["logp"]),
                            value=float(ev["value"]),
                            tick=c.tick,
                            phi=c.phi,
                        )
                    )
                previous = c
        except Exception as exc:
            status = "invalid"
            error = str(exc)
            traceback.print_exc()
        finally:
            conn.close()
            if args.record_dir and records:
                # Map-static hash + team identifies a match in a unique batch;
                # runners forbid duplicate map seeds within a batch.
                key = hashlib.sha256(
                    parse_context(
                        zlib.decompress(records[0]["context"])
                    ).static.tobytes()
                ).hexdigest()[:20]
                base = Path(args.record_dir) / f"g{c.game_id}.t{team}"
                try:
                    if base.with_suffix(base.suffix + ".json").exists():
                        raise ValueError("duplicate episode identity")
                    blobs = []
                    offsets = [0]
                    actions = []
                    aoffs = [0]
                    for r in records:
                        z = r["context"]
                        blobs.append(z)
                        offsets.append(offsets[-1] + len(z))
                        actions.append(r["action"])
                        aoffs.append(aoffs[-1] + len(r["action"]))
                    with open(str(base) + ".npz.tmp", "wb") as f:
                        np.savez(
                            f,
                            contexts=np.frombuffer(b"".join(blobs), np.uint8),
                            offsets=offsets,
                            actions=np.frombuffer(b"".join(actions), np.uint8),
                            action_offsets=aoffs,
                            logps=[r["logp"] for r in records],
                            values=[r["value"] for r in records],
                            ticks=[r["tick"] for r in records],
                            potentials=[r["phi"] for r in records],
                        )
                    os.replace(str(base) + ".npz.tmp", str(base) + ".npz")
                    atomic_json(
                        str(base) + ".json",
                        dict(
                            schema=SCHEMA,
                            policy_id=ck["policy_id"],
                            team=team,
                            map_hash=key,
                            game_id=c.game_id,
                            status=status,
                            error=error,
                            steps=len(records),
                            outcome=None,
                            seed=args.seed,
                            sample=args.sample,
                        ),
                    )
                except Exception:
                    traceback.print_exc()
            print(
                json.dumps(
                    dict(
                        event="disconnect",
                        team=team,
                        status=status,
                        steps=len(records),
                        error=error,
                    )
                ),
                flush=True,
            )

    print(
        json.dumps(dict(event="ready", policy_id=ck["policy_id"], schema=SCHEMA)),
        flush=True,
    )
    try:
        while not stop.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            thread = threading.Thread(target=client, args=(conn,))
            thread.start()
            threads.append(thread)
    finally:
        listener.close()
        for t in threads:
            t.join(timeout=125)
        os.unlink(args.socket)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", required=True)
    ap.add_argument("--socket", required=True)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--sample", action="store_true")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--record-dir")
    args = ap.parse_args()
    torch.set_num_threads(2)
    serve(args)


if __name__ == "__main__":
    main()
