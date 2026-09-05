#!/usr/bin/env python3
"""cortex-knob-search.py — successive-halving knob search for the Cortex AI.

Searches the CortexTuning knob vector (src/ai/cortex/CortexTuning.h) against
Nicowar using tools/ai-benchmark.sh as the game runner. Designed around the
anti-overfit rules from the rank-gate tuning handoff:

  * fitness = MIN across the training maps' win rates (no map trading);
  * paired seeds: every config faces the exact same seed blocks, both
    orientations (--swap-sides), so A/B differences are per-seed flips,
    not resampling noise;
  * successive halving: all configs get a cheap seed block first, only
    survivors buy more seeds (later blocks EXTEND the seed set, nothing is
    re-run), so the budget concentrates on candidates that survive contact;
  * the DEFAULT config always rides along as the control: it is never
    eliminated, and every survivor's per-seed flips vs it are reported;
  * holdout maps (--holdout) are never part of the fitness — evaluate the
    winner on them ONCE at the end, as a generalization check.

Usage:
  # overnight search (report design defaults):
  tools/cortex-knob-search.py --run-dir .tmp/knob-search/run1

  # tiny smoke run to validate plumbing end-to-end:
  tools/cortex-knob-search.py --run-dir .tmp/knob-search/smoke \
      --configs 2 --blocks 3 --keeps "" --maps Muka

  # holdout check of one surviving config:
  tools/cortex-knob-search.py --run-dir .tmp/knob-search/run1 \
      --holdout configs/cfg-07.tuning

Every path handed to the engine is absolute (glob2 chdir()s at startup) and
game runs write outside any sandbox — run this with sandboxing disabled.
State is checkpointed to <run-dir>/state.json after every benchmark; rerunning
the same command resumes where it left off.
"""

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TOOLS_DIR)
BENCH = os.path.join(TOOLS_DIR, "ai-benchmark.sh")

# Knob space (name -> inclusive integer range). Ranges from the Muka rev
# diagnosis (.tmp/rankgate-diag/FINDINGS.md); defaults from CortexTuning.h.
DEFAULTS = {
    "expandCornLo": 5,
    "swarmWorkerCap": 7,
    "swarmCornRemHi": 15,
    "wheatStarvedTiles": 5,
    "expandWheatVeto": 0,
    "expandDebounceCycles": 1,
    "scoreSecondSwarmBase": 6100,
    "scoreSecondSwarmStep": 100,
    "expandSeverityFloor": 1,
    "tierMidDiv": 2,
    "workerRatioTier2": 8,
}
PRIMARY_SPACE = {
    "expandCornLo": (1, 8),
    "swarmWorkerCap": (5, 12),
    "swarmCornRemHi": (8, 18),
    "wheatStarvedTiles": (3, 15),
    # 0 disables the veto; sampled as 0 with probability ~1/3, else 8..64.
    "expandWheatVeto": (0, 64),
    "expandDebounceCycles": (1, 20),
    "scoreSecondSwarmBase": (5400, 6600),
    "scoreSecondSwarmStep": (0, 300),
    "expandSeverityFloor": (1, 5),
}
SECONDARY_SPACE = {
    "tierMidDiv": (1, 4),
    "workerRatioTier2": (2, 12),
}

# Hypothesis configs from the Muka rev diagnosis (.tmp/rankgate-diag/FINDINGS.md):
# single-lever probes at the suspected fix family, screened alongside the random
# population so the search doesn't depend on random draws landing near them.
HYPOTHESES = {
    # field-depleted-only trigger (kills the capped-draining face outright)
    "cfg-h1-floor5": {"expandSeverityFloor": 5},
    # keep the capped-draining face but veto it while wheat is plentiful
    "cfg-h2-veto24": {"expandWheatVeto": 24},
    # keep both faces but require the desire to persist ~6 cycles (150 ticks)
    "cfg-h3-debounce6": {"expandDebounceCycles": 6},
    # gentler ranking: low-severity expansion loses to the tech band
    "cfg-h4-base5800": {"scoreSecondSwarmBase": 5800, "scoreSecondSwarmStep": 200},
}


def sample_config(rng, include_secondary):
    knobs = dict(DEFAULTS)
    for name, (lo, hi) in PRIMARY_SPACE.items():
        if name == "expandWheatVeto":
            knobs[name] = 0 if rng.random() < 1 / 3 else rng.randint(8, hi)
        elif name in ("scoreSecondSwarmBase", "scoreSecondSwarmStep"):
            knobs[name] = rng.randrange(lo, hi + 1, 50)
        else:
            knobs[name] = rng.randint(lo, hi)
    # A severity floor above the severity scale's top (expandCornLo) would
    # disable expansion outright — clamp so every config CAN expand.
    knobs["expandSeverityFloor"] = min(knobs["expandSeverityFloor"], knobs["expandCornLo"])
    if include_secondary:
        for name, (lo, hi) in SECONDARY_SPACE.items():
            knobs[name] = rng.randint(lo, hi)
    return knobs


def write_tuning_file(path, knobs):
    with open(path, "w") as f:
        for name in DEFAULTS:
            f.write(f"{name} {knobs[name]}\n")


GAME_END_RE = re.compile(r"^GLOB2_GAME_END .*winner_team=(-?\d+)", re.M)


def parse_game(log_path, cortex_team):
    """Outcome for cortex in one game log: W/L/D, or E if the game errored."""
    try:
        with open(log_path, errors="replace") as f:
            m = GAME_END_RE.search(f.read())
    except OSError:
        return "E"
    if not m:
        return "E"
    winner = int(m.group(1))
    if winner == -1:
        return "D"
    return "W" if winner == cortex_team else "L"


def run_bench(bin_path, cfg_path, game_map, games, seed_base, out_dir, jobs):
    """One ai-benchmark.sh invocation; returns {seed: {'fwd': o, 'rev': o}}."""
    os.makedirs(out_dir, exist_ok=True)
    env = dict(os.environ)
    env["GLOB2_CORTEX_TUNING"] = os.path.abspath(cfg_path)
    cmd = [BENCH, "--map", game_map, "--matchup", "cortex,nicowar",
           "--games", str(games), "--swap-sides",
           "--seed-base", str(seed_base),
           "--bin", os.path.abspath(bin_path),
           "--out", out_dir, "--keep"]
    if jobs:
        cmd += ["--jobs", str(jobs)]
    with open(os.path.join(out_dir, "bench.out"), "w") as summary:
        subprocess.run(cmd, env=env, stdout=summary,
                       stderr=subprocess.STDOUT, check=False)
    results = {}
    for i in range(games):
        tag = f"{i:04d}"
        results[str(seed_base + i)] = {
            # matchup cortex,nicowar: fwd cortex=team0; rev cortex=team1.
            "fwd": parse_game(os.path.join(out_dir, f"game-{tag}-fwd.log"), 0),
            "rev": parse_game(os.path.join(out_dir, f"game-{tag}-rev.log"), 1),
        }
    return results


def win_rate(seed_results):
    counts = {"W": 0, "L": 0, "D": 0, "E": 0}
    for r in seed_results.values():
        for o in ("fwd", "rev"):
            counts[r[o]] += 1
    played = counts["W"] + counts["L"] + counts["D"]
    return (counts["W"] / played if played else 0.0), counts


def flips_vs(control, candidate):
    """Per-seed W<->L flips between two {seed: {fwd,rev}} grids (shared seeds)."""
    gained = lost = 0
    for seed, ours in candidate.items():
        theirs = control.get(seed)
        if not theirs:
            continue
        for o in ("fwd", "rev"):
            if theirs[o] == "W" and ours[o] == "L":
                lost += 1
            elif theirs[o] == "L" and ours[o] == "W":
                gained += 1
    return gained, lost


class State:
    """Checkpointed search state: configs and their per-seed grids per map."""

    def __init__(self, path):
        self.path = path
        if os.path.exists(path):
            with open(path) as f:
                self.data = json.load(f)
        else:
            self.data = {"configs": {}}

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.data, f, indent=1, sort_keys=True)
        os.replace(tmp, self.path)

    def ensure_config(self, cid, knobs):
        self.data["configs"].setdefault(cid, {"knobs": knobs, "results": {}})

    def grid(self, cid, game_map):
        return self.data["configs"][cid]["results"].setdefault(game_map, {})

    def has_seeds(self, cid, game_map, seed_base, games):
        grid = self.data["configs"].get(cid, {}).get("results", {}).get(game_map, {})
        return all(str(seed_base + i) in grid for i in range(games))


def fitness(state, cid, maps):
    per_map = {}
    for m in maps:
        rate, counts = win_rate(state.grid(cid, m))
        per_map[m] = (rate, counts)
    fit = min(rate for rate, _ in per_map.values()) if per_map else 0.0
    mean = sum(rate for rate, _ in per_map.values()) / len(per_map) if per_map else 0.0
    return fit, mean, per_map


def eval_block(args, state, cid, cfg_path, maps, block_games, block_base):
    """Run one seed block for one config on every map (skipping cached work)."""
    for m in maps:
        if state.has_seeds(cid, m, block_base, block_games):
            continue
        out_dir = os.path.join(args.run_dir, "bench", cid, f"{m}-s{block_base}")
        print(f"  {cid} {m} seeds {block_base}..{block_base + block_games - 1} ...",
              flush=True)
        results = run_bench(args.bin, cfg_path, m, block_games, block_base,
                            out_dir, args.jobs)
        state.grid(cid, m).update(results)
        state.save()
        errs = sum(1 for r in results.values() for o in ("fwd", "rev") if r[o] == "E")
        if errs:
            print(f"    WARNING: {errs} errored game(s) — see {out_dir}", flush=True)
        if not args.keep_logs:
            for name in os.listdir(out_dir):
                if name.startswith("game-"):
                    os.remove(os.path.join(out_dir, name))


def cmd_search(args):
    maps = args.maps.split(",")
    blocks = [int(b) for b in args.blocks.split(",") if b]
    keeps = [int(k) for k in args.keeps.split(",") if k]
    if len(keeps) != len(blocks) - 1:
        sys.exit(f"error: --keeps needs {len(blocks) - 1} entries for {len(blocks)} blocks")

    os.makedirs(os.path.join(args.run_dir, "configs"), exist_ok=True)
    state = State(os.path.join(args.run_dir, "state.json"))

    # Population: the control (defaults) + N sampled configs. Sampling is
    # deterministic in --sample-seed, so a resumed run regenerates the same
    # population and the state cache lines up.
    rng = random.Random(args.sample_seed)
    population = [("cfg-00-control", dict(DEFAULTS))]
    if not args.no_hypotheses:
        for cid, overrides in HYPOTHESES.items():
            population.append((cid, {**DEFAULTS, **overrides}))
    for i in range(1, args.configs + 1):
        population.append((f"cfg-{i:02d}", sample_config(rng, args.include_secondary)))

    cfg_paths = {}
    for cid, knobs in population:
        state.ensure_config(cid, knobs)
        path = os.path.join(args.run_dir, "configs", f"{cid}.tuning")
        write_tuning_file(path, knobs)
        cfg_paths[cid] = path
    state.save()

    total_games = 0
    alive_count = len(population)
    for bi, block in enumerate(blocks):
        total_games += alive_count * len(maps) * block * 2
        if bi < len(keeps):
            alive_count = keeps[bi] + 1  # +control
    print(f"plan: {len(population) - 1} configs + control, maps {maps}, "
          f"blocks {blocks} (cumulative {sum(blocks)} seeds), keeps {keeps}; "
          f"~{total_games} games total")
    if args.dry_run:
        for cid, knobs in population:
            diffs = {k: v for k, v in knobs.items() if v != DEFAULTS[k]}
            print(f"  {cid}: {diffs if diffs else '(defaults)'}")
        return

    alive = [cid for cid, _ in population]
    block_base = args.seed_base
    for bi, block in enumerate(blocks):
        print(f"\n=== rung {bi}: seeds {block_base}..{block_base + block - 1}, "
              f"{len(alive)} config(s) ===", flush=True)
        for cid in alive:
            eval_block(args, state, cid, cfg_paths[cid], maps, block, block_base)
        # rank on ALL seeds seen so far; control is reported but never eliminated
        ranked = sorted((c for c in alive if c != "cfg-00-control"),
                        key=lambda c: (fitness(state, c, maps)[0],
                                       fitness(state, c, maps)[1]),
                        reverse=True)
        print(f"\nrung {bi} standings (fitness = min across maps):")
        for cid in ["cfg-00-control"] + ranked:
            fit, mean, per_map = fitness(state, cid, maps)
            per = "  ".join(f"{m} {rate * 100:5.1f}%" for m, (rate, _) in per_map.items())
            print(f"  {cid:<16} min {fit * 100:5.1f}%  mean {mean * 100:5.1f}%   {per}")
        if bi < len(keeps):
            alive = ["cfg-00-control"] + ranked[:keeps[bi]]
        block_base += block

    # final report: survivors + per-seed flips vs control
    report = [f"# Knob search results — {args.run_dir}", ""]
    report.append(f"blocks {blocks} seeds from {args.seed_base}, maps {maps}, "
                  f"{args.configs} sampled configs, sample-seed {args.sample_seed}")
    report.append("")
    for cid in ["cfg-00-control"] + ranked:
        fit, mean, per_map = fitness(state, cid, maps)
        knobs = state.data["configs"][cid]["knobs"]
        diffs = {k: v for k, v in knobs.items() if v != DEFAULTS[k]}
        report.append(f"## {cid}  min {fit * 100:.1f}%  mean {mean * 100:.1f}%")
        report.append(f"knobs: {json.dumps(diffs) if diffs else '(defaults)'}")
        for m, (rate, counts) in per_map.items():
            line = f"- {m}: {rate * 100:.1f}% ({counts['W']}W/{counts['L']}L/{counts['D']}D)"
            if cid != "cfg-00-control":
                gained, lost = flips_vs(state.grid("cfg-00-control", m), state.grid(cid, m))
                line += f", flips vs control +{gained}/-{lost}"
            report.append(line)
        report.append("")
    out = os.path.join(args.run_dir, "RESULTS.md")
    with open(out, "w") as f:
        f.write("\n".join(report) + "\n")
    print(f"\nwrote {out}")


def cmd_holdout(args):
    maps = args.holdout_maps.split(",")
    state = State(os.path.join(args.run_dir, "state.json"))
    cfg_path = os.path.abspath(os.path.join(args.run_dir, args.holdout))
    cid = "holdout-" + os.path.splitext(os.path.basename(args.holdout))[0]
    knobs = {}
    with open(cfg_path) as f:
        for line in f:
            parts = line.split("#")[0].split()
            if len(parts) == 2:
                knobs[parts[0]] = int(parts[1])
    control_path = os.path.join(args.run_dir, "configs", "holdout-control.tuning")
    os.makedirs(os.path.dirname(control_path), exist_ok=True)
    write_tuning_file(control_path, DEFAULTS)
    state.ensure_config(cid, knobs)
    state.ensure_config("holdout-control", dict(DEFAULTS))
    print(f"holdout: {cid} + control on {maps}, "
          f"{args.holdout_games} seeds from {args.holdout_seed_base}")
    for c, path in ((cid, cfg_path), ("holdout-control", control_path)):
        eval_block(args, state, c, path, maps, args.holdout_games,
                   args.holdout_seed_base)
    for c in (cid, "holdout-control"):
        fit, mean, per_map = fitness(state, c, maps)
        per = "  ".join(f"{m} {rate * 100:5.1f}%" for m, (rate, _) in per_map.items())
        print(f"  {c:<24} min {fit * 100:5.1f}%  mean {mean * 100:5.1f}%   {per}")
    for m in maps:
        gained, lost = flips_vs(state.grid("holdout-control", m), state.grid(cid, m))
        print(f"  {m}: flips vs control +{gained}/-{lost}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--run-dir", required=True,
                   help="state/output directory (checkpointed; rerun to resume)")
    p.add_argument("--configs", type=int, default=16,
                   help="sampled configs, control not included (default 16)")
    p.add_argument("--blocks", default="20,30,50",
                   help="comma-separated seed-block sizes per rung (default 20,30,50)")
    p.add_argument("--keeps", default="6,3",
                   help="survivors kept after each rung but the last (default 6,3)")
    p.add_argument("--maps", default="Muka,SmallForTwo,Mazury",
                   help="training maps (fitness = min across them)")
    p.add_argument("--seed-base", type=int, default=1)
    p.add_argument("--sample-seed", type=int, default=42,
                   help="RNG seed for config sampling (reproducible population)")
    p.add_argument("--jobs", type=int, default=0,
                   help="parallel games per benchmark (default: ai-benchmark.sh's)")
    p.add_argument("--bin", default=os.path.join(REPO_DIR, "build", "src", "glob2"))
    p.add_argument("--include-secondary", action="store_true",
                   help="also search tierMidDiv/workerRatioTier2")
    p.add_argument("--no-hypotheses", action="store_true",
                   help="skip the hand-designed hypothesis configs")
    p.add_argument("--keep-logs", action="store_true",
                   help="keep per-game logs/replays (large!)")
    p.add_argument("--dry-run", action="store_true",
                   help="print the sampled population and game budget, run nothing")
    p.add_argument("--holdout", metavar="TUNING_FILE",
                   help="holdout-evaluate one tuning file (relative to --run-dir) + control")
    p.add_argument("--holdout-maps", default="Dejans,balanced_for_2,strange2")
    p.add_argument("--holdout-games", type=int, default=40)
    p.add_argument("--holdout-seed-base", type=int, default=1001,
                   help="fresh seeds, disjoint from the search's (default 1001)")
    args = p.parse_args()

    args.run_dir = os.path.abspath(args.run_dir)
    if not os.path.exists(args.bin):
        sys.exit(f"error: binary not found: {args.bin} (scons release=1 -j16)")
    if args.holdout:
        cmd_holdout(args)
    else:
        cmd_search(args)


if __name__ == "__main__":
    main()
