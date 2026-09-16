#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-play rollout driver for Neurotica.

Runs games against a policy server, on a FRESHLY GENERATED map from a randomly
chosen generator every time — never a built-in map and never a fixed pool. The
stock two-team maps are mostly below 128x128, which is not a playable size, and
reusing a small pool teaches the net those maps rather than how to read a map.

Opponents are drawn from a league: the policy itself (self-play) plus the
hand-written AIs as frozen exploiters. Pure self-play in an RTS reliably
collapses into a narrow strategy cycle, and the four strong AIs are a free,
genuinely different-styled opponent pool that keeps it honest.

After each game the driver writes the outcome into the trajectory the server
left behind. The server cannot do this itself: it sees observations, not who
won.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

GAME_END = re.compile(r"winner_team=(-?\d+)")


def generate_map(glob2: str, root: str, generators, game_id: int, seed: int):
    """Fresh map from a random generator. Returns the bare map name or None."""
    name = f"sp_{game_id}_{os.getpid()}"
    path = os.path.join(root, "maps", name + ".map")
    for attempt in range(4):
        gen = random.choice(generators)
        cmd = [glob2, "--generate-map", gen, "--output", path,
               "--width", "128", "--height", "128", "--teams", "2",
               "--seed", str(seed + attempt * 101)]
        try:
            subprocess.run(cmd, cwd=root, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=180, check=False)
        except subprocess.TimeoutExpired:
            continue
        if os.path.exists(path) and os.path.getsize(path) > 0:
            return name, path
        if os.path.exists(path):
            os.unlink(path)
    return None, None


def play_one(args, generators, game_id: int) -> dict:
    seed = game_id * 7919 + 13
    name, path = generate_map(args.glob2, args.root, generators, game_id, seed)
    if name is None:
        return {"game_id": game_id, "error": "mapgen"}

    opponent = random.choice(args.league.split(","))
    env = dict(os.environ)
    env.update(GLOB2_TEST_SEED=str(seed),
               GLOB2_NEUROTICA_POLICY_SOCKET=args.socket,
               GLOB2_NEUROTICA_GAME_ID=str(game_id),
               GLOB2_NEUROTICA_POLICY_PERIOD=str(args.policy_period))
    if args.max_ticks:
        env["GLOB2_TEST_MAX_TICKS"] = str(args.max_ticks)
    cmd = [args.glob2, "-test-games-nox", "1", "--map", name,
           "--matchup", f"neurotica,{opponent}"]
    try:
        proc = subprocess.run(cmd, cwd=args.root, capture_output=True,
                              text=True, timeout=args.timeout, env=env)
        out = proc.stdout
    except subprocess.TimeoutExpired:
        out = ""
    finally:
        if os.path.exists(path):
            os.unlink(path)

    match = GAME_END.search(out)
    winner = int(match.group(1)) if match else -1
    # Neurotica is team 0 in the matchup above.
    outcome = 0.0 if winner < 0 else (1.0 if winner == 0 else -1.0)

    # Stamp the outcome onto whatever the server recorded for this game.
    if args.record_dir:
        for team in (0, 1):
            meta = os.path.join(args.record_dir, f"g{game_id}_t{team}.json")
            if os.path.exists(meta):
                try:
                    with open(meta) as fh:
                        data = json.load(fh)
                    data["outcome"] = outcome if team == 0 else -outcome
                    data["opponent"] = opponent
                    with open(meta, "w") as fh:
                        json.dump(data, fh)
                except Exception:
                    pass
    return {"game_id": game_id, "opponent": opponent, "winner": winner,
            "outcome": outcome}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.expanduser("~/neurotica/glob2"))
    ap.add_argument("--glob2", default=os.path.expanduser("~/neurotica/glob2/build/src/glob2"))
    ap.add_argument("--socket", default="/tmp/neurotica.sock")
    ap.add_argument("--record-dir", default=os.path.expanduser("~/neurotica/rollouts"))
    ap.add_argument("--generators", default=os.path.expanduser("~/neurotica/generators.txt"))
    ap.add_argument("--league", default="nicowar,cabino,cortex,maxima",
                    help="frozen opponents to draw from each game")
    ap.add_argument("--parallel", type=int, default=4,
                    help="concurrent games; the GPU caps useful parallelism, "
                         "so more workers mostly means each game runs slower")
    ap.add_argument("--games", type=int, default=100000)
    ap.add_argument("--start-id", type=int, default=1)
    ap.add_argument("--policy-period", type=int, default=100)
    ap.add_argument("--max-ticks", type=int, default=0)
    ap.add_argument("--timeout", type=int, default=1800)
    args = ap.parse_args()

    with open(args.generators) as fh:
        generators = [line.strip() for line in fh if line.strip()]
    os.makedirs(args.record_dir, exist_ok=True)
    print(f"{len(generators)} generators, league [{args.league}], "
          f"{args.parallel} concurrent", flush=True)

    wins = played = 0
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = {}
        next_id = args.start_id
        end_id = args.start_id + args.games
        while next_id < end_id or futures:
            while len(futures) < args.parallel and next_id < end_id:
                futures[pool.submit(play_one, args, generators, next_id)] = next_id
                next_id += 1
            done = [f for f in futures if f.done()]
            if not done:
                time.sleep(0.5)
                continue
            for f in done:
                futures.pop(f)
                res = f.result()
                if "error" in res:
                    continue
                played += 1
                wins += 1 if res["outcome"] > 0 else 0
                if played % 5 == 0:
                    print(f"{played} games, win rate {wins/played:.3f}, "
                          f"{played/(time.time()-t0)*3600:.0f} games/h", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
