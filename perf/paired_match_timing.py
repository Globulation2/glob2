#!/usr/bin/env python3
"""Paired headless match timing: baseline binary vs candidate on the same map and seed.

Runs `glob2 --run-game` alternately (randomized order per pair), records user+sys CPU
and wall time per run, and reports medians, min/max and the candidate/baseline ratio.
"""
import argparse
import json
import os
import random
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run(binary, root, args, env, out_dir):
    cmd = [str(binary), "--run-game", *args, "--output-dir", str(out_dir)]
    start = time.perf_counter()
    proc = subprocess.Popen(cmd, cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    _, status, usage = os.wait4(proc.pid, 0)
    wall = time.perf_counter() - start
    err = proc.stderr.read().decode(errors="replace")
    if os.waitstatus_to_exitcode(status) != 0:
        raise SystemExit(f"{binary} failed:\n{err[-2000:]}")
    result = json.loads((out_dir / "result.json").read_text()) if (out_dir / "result.json").exists() else {}
    return {"cpu": usage.ru_utime + usage.ru_stime, "wall": wall, "checksum": result.get("finalChecksum") or result.get("checksum")}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--baseline", required=True, help="path to baseline worktree")
    p.add_argument("--candidate", required=True, help="path to candidate worktree")
    p.add_argument("--map", default="maps/FourSquares1.map.gz")
    p.add_argument("--players", default="maxima,cortex,castor,nicowar")
    p.add_argument("--ticks", type=int, default=6000)
    p.add_argument("--seed", type=int, default=19)
    p.add_argument("--repeats", type=int, default=10)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--output", required=True)
    a = p.parse_args()
    out = Path(a.output)
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    # TREE[:BINARY]; the binary defaults to the tree's release client.
    def split(spec):
        tree, _, binary = spec.partition(":")
        return Path(tree), Path(binary) if binary else Path(tree) / "build/linux/client/release/src/glob2"
    trees = {"baseline": split(a.baseline)[0], "candidate": split(a.candidate)[0]}
    binaries = {"baseline": split(a.baseline)[1], "candidate": split(a.candidate)[1]}
    args = ["--map-file", a.map, "--game-seed", str(a.seed), "--ticks", str(a.ticks), "--telemetry", "checksums"]
    for player in a.players.split(","):
        args += ["--player", player]
    rng = random.Random(7)
    samples = {k: [] for k in trees}
    checksums = {k: set() for k in trees}
    with tempfile.TemporaryDirectory(prefix="paired-") as tmp:
        for i in range(a.warmup + a.repeats):
            order = list(trees)
            rng.shuffle(order)
            for name in order:
                d = Path(tmp) / f"{name}-{i}"
                r = run(binaries[name], trees[name], args, env, d)
                checksums[name].add(json.dumps(r["checksum"], sort_keys=True))
                if i >= a.warmup:
                    samples[name].append(r)
                print(f"{name:9s} rep {i:2d} cpu {r['cpu']:.3f}s wall {r['wall']:.3f}s", flush=True)
    report = {"map": a.map, "ticks": a.ticks, "seed": a.seed, "players": a.players, "repeats": a.repeats,
              "samples": samples, "checksums": {k: sorted(v) for k, v in checksums.items()}}
    for metric in ("cpu", "wall"):
        med = {k: statistics.median(s[metric] for s in v) for k, v in samples.items()}
        report[f"median_{metric}"] = med
        report[f"ratio_{metric}"] = med["candidate"] / med["baseline"]
        print(f"{metric}: baseline median {med['baseline']:.3f}s  candidate median {med['candidate']:.3f}s  "
              f"ratio {report[f'ratio_{metric}']:.4f}  "
              f"(min/max baseline {min(s[metric] for s in samples['baseline']):.3f}/{max(s[metric] for s in samples['baseline']):.3f}, "
              f"candidate {min(s[metric] for s in samples['candidate']):.3f}/{max(s[metric] for s in samples['candidate']):.3f})")
    print("checksums identical across repeats:", {k: len(v) == 1 for k, v in checksums.items()})
    (out / "paired-timing.json").write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
