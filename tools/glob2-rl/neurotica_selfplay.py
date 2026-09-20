#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synchronized rollout generations, or pooled teacher-order collection.
No PFSP weighting hides a changing opponent mix. No old rollouts are deleted.
"""

import argparse
from pathlib import Path
import random
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from neurotica_run import HERE, PolicyServer, play_game, write_json, digest
from paired_eval import OPPONENTS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--generators", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--init")
    ap.add_argument("--collect", action="store_true")
    ap.add_argument("--roundtrip", action="store_true")
    ap.add_argument("--teachers", default=",".join(OPPONENTS))
    ap.add_argument("--opponents", default=",".join(OPPONENTS))
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--games", type=int, default=14)
    ap.add_argument("--generations", type=int, default=1)
    ap.add_argument("--parallel", type=int, default=4)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--max-ticks", type=int, default=40000)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--bc-corpus")
    args = ap.parse_args()
    if not args.collect and not args.init:
        raise ValueError("--init required for PPO")
    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError(
            "use a fresh run directory; resume explicitly from its retained checkpoint"
        )
    out.mkdir(parents=True, exist_ok=True)
    gens = [
        s.strip() for s in Path(args.generators).read_text().splitlines() if s.strip()
    ]
    if not gens:
        raise ValueError("empty generators")
    write_json(
        out / "config.json",
        dict(args=vars(args), generators=gens, binary_sha256=digest(args.binary)),
    )
    rng = random.Random(args.seed)
    current = str(Path(args.init).resolve()) if args.init else None
    for generation in range(args.generations):
        run = out / f"generation-{generation:05d}"
        run.mkdir()
        opps = args.opponents.split(",")
        teachers = args.teachers.split(",")
        games = []
        for i in range(args.games):
            games.append(
                dict(
                    id=generation * args.games + i + 1,
                    opponent=opps[i % len(opps)],
                    generator=rng.choice(gens),
                    map_seed=rng.randrange(1, 2**31),
                    game_seed=rng.randrange(1, 2**31),
                    teacher=teachers[(i + generation + 1) % len(teachers)],
                )
            )
        write_json(run / "manifest.json", dict(generators=gens, games=games))
        server = (
            PolicyServer(current, run / "policy", args.device, True, args.seed)
            if current
            else None
        )
        try:
            with ThreadPoolExecutor(args.parallel) as pool:
                results = list(
                    pool.map(
                        lambda g: play_game(
                            args.root,
                            args.binary,
                            g,
                            run / "games",
                            args.max_ticks,
                            args.timeout,
                            server,
                            teacher=g["teacher"] if args.collect else None,
                            record=args.collect,
                            roundtrip=args.roundtrip,
                        ),
                        games,
                    )
                )
        finally:
            if server:
                server.close()
        write_json(run / "results.json", results)
        if any(r["status"] != "complete" for r in results):
            raise RuntimeError("invalid generation; fix failures before training")
        if current:
            cmd = [
                sys.executable,
                str(HERE / "neurotica_ppo.py"),
                "--init",
                current,
                "--reference",
                str(Path(args.init).resolve()),
                "--rollouts",
                str(run / "policy" / "rollouts"),
                "--out",
                str(run / "update"),
                "--device",
                args.device,
                "--shaping",
                "1.0",
                "--gamma",
                "1.0",
                "--seed",
                str(args.seed + generation),
            ]
            if args.bc_corpus:
                cmd += ["--bc-corpus", args.bc_corpus]
            subprocess.run(cmd, check=True)
            current = str(run / "update" / "policy.pt")


if __name__ == "__main__":
    main()
