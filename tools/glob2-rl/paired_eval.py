#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed-manifest evaluation with paired wins, no silent missing/duplicate games."""

import argparse
import json
import math
from pathlib import Path
import random
from concurrent.futures import ThreadPoolExecutor
from neurotica_run import PolicyServer, play_game, write_json, digest

OPPONENTS = ["numbi", "warrush", "castor", "nicowar", "cortex", "maxima", "cabino"]


def paired_summary(baseline, candidate):
    def keyed(rows):
        out = {}
        for r in rows:
            key = (
                r["id"],
                r["generator"],
                r["map_seed"],
                r["game_seed"],
                r["opponent"],
                r["max_ticks"],
            )
            if key in out:
                raise ValueError("duplicate paired game")
            if r["status"] != "complete":
                raise ValueError("invalid game in paired evaluation")
            out[key] = r
        return out

    b, c = keyed(baseline), keyed(candidate)
    if b.keys() != c.keys():
        raise ValueError("arms do not contain exactly the same completed games")
    plus = minus = 0
    tab = {}
    for key in b:
        if b[key].get("map_sha256") != c[key].get("map_sha256"):
            raise ValueError("paired map mismatch")
        x, y = b[key]["winner"], c[key]["winner"]
        tab[f"{x}->{y}"] = tab.get(f"{x}->{y}", 0) + 1
        plus += x != 0 and y == 0
        minus += x == 0 and y != 0
    n = plus + minus
    p = (
        min(1.0, 2 * sum(math.comb(n, k) for k in range(min(plus, minus) + 1)) / 2**n)
        if n
        else 1.0
    )
    return dict(
        games=len(b),
        baseline_wins=sum(r["winner"] == 0 for r in b.values()),
        candidate_wins=sum(r["winner"] == 0 for r in c.values()),
        gained_wins=plus,
        lost_wins=minus,
        mcnemar_exact_p=p,
        cross_tab=tab,
    )


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("manifest")
    m.add_argument("--generators", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--per-opponent", type=int, default=25)
    m.add_argument("--seed", type=int, default=20260920)
    m.add_argument("--opponents", default=",".join(OPPONENTS))
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--arm", required=True)
    r.add_argument("--checkpoint")
    r.add_argument("--root", required=True)
    r.add_argument("--binary", required=True)
    r.add_argument("--device", default="cpu")
    r.add_argument("--sample", action="store_true")
    r.add_argument("--seed", type=int, default=0)
    r.add_argument("--max-ticks", type=int, default=40000)
    r.add_argument("--timeout", type=int, default=1800)
    r.add_argument("--parallel", type=int, default=4)
    r.add_argument("--replay", action="store_true")
    s = sub.add_parser("summary")
    s.add_argument("--baseline", required=True)
    s.add_argument("--candidate", required=True)
    args = ap.parse_args()
    if args.cmd == "manifest":
        gens = [
            x.strip()
            for x in Path(args.generators).read_text().splitlines()
            if x.strip()
        ]
        if len(gens) != len(set(gens)) or not gens:
            raise ValueError("generator list must be nonempty and unique")
        rng = random.Random(args.seed)
        games = []
        for opp in args.opponents.split(","):
            for _ in range(args.per_opponent):
                games.append(
                    dict(
                        id=len(games) + 1,
                        opponent=opp,
                        generator=rng.choice(gens),
                        map_seed=rng.randrange(1, 2**31),
                        game_seed=rng.randrange(1, 2**31),
                    )
                )
        write_json(args.out, dict(seed=args.seed, generators=gens, games=games))
        return
    if args.cmd == "summary":
        load = lambda p: json.loads((Path(p) / "results.json").read_text())
        print(
            json.dumps(
                paired_summary(load(args.baseline), load(args.candidate)), indent=2
            )
        )
        return
    if (args.arm == "inert") != (args.checkpoint is None):
        raise ValueError("only inert may omit a checkpoint")
    out = Path(args.out)
    if out.exists() and any(out.iterdir()):
        raise ValueError("use a new output directory for each arm/run")
    out.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(Path(args.manifest).read_text())
    games = manifest["games"]
    if len({g["id"] for g in games}) != len(games):
        raise ValueError("duplicate game IDs")
    write_json(out / "manifest.json", manifest)
    write_json(
        out / "config.json",
        dict(
            args=vars(args),
            binary_sha256=digest(args.binary),
            checkpoint_sha256=digest(args.checkpoint) if args.checkpoint else None,
        ),
    )
    server = (
        PolicyServer(
            args.checkpoint, out / "policy", args.device, args.sample, args.seed
        )
        if args.checkpoint
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
                        out / "games",
                        args.max_ticks,
                        args.timeout,
                        server,
                        replay=args.replay,
                    ),
                    games,
                )
            )
    finally:
        if server:
            server.close()
    write_json(out / "results.json", results)
    if any(r["status"] != "complete" for r in results):
        raise RuntimeError("invalid games; this arm cannot be compared")
    print(
        json.dumps(
            dict(
                wins=sum(r["winner"] == 0 for r in results),
                games=len(results),
                caps=sum(r["winner"] == -1 for r in results),
            )
        )
    )


if __name__ == "__main__":
    main()
