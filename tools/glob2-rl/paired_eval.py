#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Paired evaluation of Neurotica configurations.

Every arm plays the SAME games: a fixed manifest of (opponent, generator, map
seed, game seed) tuples, so differences between arms are differences in play,
not in maps. The previous eval drew fresh maps and seeds per game, capped by
wall-clock on a GPU shared with self-play, and never recorded which server
flags a run used -- which left every cross-configuration number in the night
log indistinguishable from noise (5 vs 3 wins of 22: p=0.70).

Results append to one CSV, one row per game, with the arm's exact server flags
in the row. Summaries report Wilson intervals, because a win count without one
has misled this project repeatedly.

Usage:
  paired_eval.py manifest --out eval/manifest.json --per-opponent 25
  paired_eval.py run --manifest eval/manifest.json --arm inert
  paired_eval.py run --manifest eval/manifest.json --arm staff_topk \\
      --checkpoint ~/neurotica/ckpt_staff/best.pt \\
      --serve-extra "--top-k --placements 48 --use-count"
  paired_eval.py summary --results eval/results.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import random
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.expanduser("~/neurotica")
GLOB2 = f"{ROOT}/glob2/build/src/glob2"
OPPONENTS = ["numbi", "castor", "warrush", "nicowar"]
GAME_END = re.compile(r"GLOB2_GAME_END ticks=(\d+) winner_team=(-?\d+)")


def wilson(k: int, n: int, z: float = 1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (p, max(0.0, c - h), min(1.0, c + h))


def cmd_manifest(args):
    gens = [g.strip() for g in open(f"{ROOT}/generators.txt") if g.strip()]
    rng = random.Random(args.seed)
    games = []
    for opp in OPPONENTS:
        for i in range(args.per_opponent):
            games.append(dict(opponent=opp, generator=rng.choice(gens),
                              map_seed=rng.randrange(1, 2**31 - 1),
                              game_seed=rng.randrange(1, 2**31 - 1)))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    json.dump(dict(seed=args.seed, games=games), open(args.out, "w"), indent=1)
    print(f"{len(games)} games -> {args.out}")


def start_server(args, sock):
    if args.arm == "inert" or not args.checkpoint:
        return None
    if os.path.exists(sock):
        os.unlink(sock)
    cmd = [f"{ROOT}/.venv/bin/python", f"{ROOT}/rl/neurotica_serve.py",
           "--checkpoint", os.path.expanduser(args.checkpoint),
           "--socket", sock, "--device", "cuda", "--max-batch", "8"]
    cmd += args.serve_extra.split()
    env = dict(os.environ, CUDA_VISIBLE_DEVICES=str(args.gpu))
    log = open(f"/tmp/paired_serve_{args.arm}.log", "w")
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env,
                            start_new_session=True)
    for _ in range(60):
        if os.path.exists(sock):
            return proc
        if proc.poll() is not None:
            break
        time.sleep(0.5)
    raise SystemExit(f"policy server failed to start; see {log.name}")


def play(args, sock, g, i):
    name = f"pe_{args.arm}_{i}"
    path = f"{ROOT}/glob2/maps/{name}.map"
    gen = subprocess.run([GLOB2, "--generate-map", g["generator"], "--output", path,
                          "--width", "128", "--height", "128", "--teams", "2",
                          "--seed", str(g["map_seed"])],
                         capture_output=True, timeout=300)
    if gen.returncode != 0 or not os.path.exists(path):
        return dict(g, winner=None, ticks=None, note="mapgen failed")
    env = dict(os.environ, GLOB2_TEST_SEED=str(g["game_seed"]),
               GLOB2_TEST_MAX_TICKS=str(args.max_ticks))
    if sock:
        env["GLOB2_NEUROTICA_POLICY_SOCKET"] = sock
    if args.policy_period:
        env["GLOB2_NEUROTICA_POLICY_PERIOD"] = str(args.policy_period)
    try:
        r = subprocess.run([GLOB2, "-test-games-nox", "1", "--map", name,
                            "--matchup", f"neurotica,{g['opponent']}"],
                           capture_output=True, text=True, env=env,
                           timeout=args.timeout)
        m = GAME_END.search(r.stdout + r.stderr)
    except subprocess.TimeoutExpired:
        m = None
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    if not m:
        return dict(g, winner=None, ticks=None, note="no result")
    return dict(g, winner=int(m.group(2)), ticks=int(m.group(1)), note="")


def cmd_run(args):
    games = json.load(open(args.manifest))["games"]
    sock = f"/tmp/neurotica_pe_{args.arm}.sock"
    server = start_server(args, sock)
    os.makedirs(os.path.dirname(os.path.abspath(args.results)), exist_ok=True)
    new = not os.path.exists(args.results)
    t0 = time.time()
    try:
        with open(args.results, "a", newline="") as fh, \
                ThreadPoolExecutor(max_workers=args.parallel) as pool:
            w = csv.writer(fh)
            if new:
                w.writerow(["arm", "checkpoint", "serve_flags", "policy_period",
                            "max_ticks", "opponent", "generator", "map_seed",
                            "game_seed", "winner", "ticks", "note"])
            futs = {pool.submit(play, args, sock if server else None, g, i): g
                    for i, g in enumerate(games)}
            done = 0
            for f in as_completed(futs):
                r = f.result()
                w.writerow([args.arm, args.checkpoint or "", args.serve_extra,
                            args.policy_period or "", args.max_ticks,
                            r["opponent"], r["generator"], r["map_seed"],
                            r["game_seed"], r["winner"] if r["winner"] is not None else "",
                            r["ticks"] if r["ticks"] is not None else "", r["note"]])
                fh.flush()
                done += 1
                if done % 10 == 0:
                    print(f"{args.arm}: {done}/{len(games)} "
                          f"({done / (time.time() - t0) * 3600:.0f} games/h)", flush=True)
    finally:
        if server:
            server.terminate()
    print(f"{args.arm}: done")
    summarize(args.results, only=args.arm)


def summarize(path, only=None):
    rows = list(csv.DictReader(open(path)))
    arms = sorted({r["arm"] for r in rows})
    for arm in arms:
        if only and arm != only:
            continue
        ar = [r for r in rows if r["arm"] == arm]
        flags = next((r["serve_flags"] for r in ar if r["serve_flags"]), "")
        print(f"\n== {arm}   [{flags}]")
        tw = tn = 0
        for opp in OPPONENTS:
            g = [r for r in ar if r["opponent"] == opp and r["winner"] != ""]
            wins = sum(1 for r in g if r["winner"] == "0")
            losses = sum(1 for r in g if r["winner"] == "1")
            caps = sum(1 for r in g if r["winner"] == "-1")
            dec = wins + losses
            p, lo, hi = wilson(wins, dec)
            tw += wins; tn += dec
            print(f"  {opp:8} {wins:3}W {losses:3}L {caps:3}cap   "
                  f"win {p:5.1%}  [{lo:4.0%}, {hi:4.0%}]")
        p, lo, hi = wilson(tw, tn)
        print(f"  {'TOTAL':8} {tw:3}W of {tn:3} decided   win {p:5.1%}  [{lo:4.0%}, {hi:4.0%}]")


def cmd_summary(args):
    summarize(args.results)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("manifest")
    m.add_argument("--out", required=True)
    m.add_argument("--per-opponent", type=int, default=25)
    m.add_argument("--seed", type=int, default=20260917)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--arm", required=True)
    r.add_argument("--checkpoint", default="")
    r.add_argument("--serve-extra", default="")
    r.add_argument("--policy-period", type=int, default=0)
    r.add_argument("--max-ticks", type=int, default=40000)
    r.add_argument("--timeout", type=int, default=1800)
    r.add_argument("--parallel", type=int, default=6)
    r.add_argument("--gpu", type=int, default=1)
    r.add_argument("--results", default=f"{ROOT}/eval/results.csv")
    s = sub.add_parser("summary")
    s.add_argument("--results", default=f"{ROOT}/eval/results.csv")
    args = ap.parse_args()
    {"manifest": cmd_manifest, "run": cmd_run, "summary": cmd_summary}[args.cmd](args)


if __name__ == "__main__":
    main()
