#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in real-engine regression. Retains maps, logs and per-tick checksums."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
from types import SimpleNamespace
from neurotica_run import play_game, write_json
from neurotica_data import OrderCorpus
from paired_eval import OPPONENTS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ticks", type=int, default=1500)
    args = ap.parse_args()
    out = Path(args.out).resolve()
    if out.exists():
        raise ValueError("use a fresh artifact directory")
    out.mkdir(parents=True)
    os.environ["GLOB2_CHECKSUM_SIDECAR"] = "1"
    games = [
        dict(
            id=i + 1,
            opponent=opp,
            teacher=OPPONENTS[(i + 1) % len(OPPONENTS)],
            generator="symmetric-arena",
            map_seed=20260 + i,
            game_seed=3100 + i,
        )
        for i, opp in enumerate(OPPONENTS)
    ]

    def compare(g):
        common = dict(
            root=args.root,
            binary=args.binary,
            g=g,
            max_ticks=args.ticks,
            teacher=g["teacher"],
        )
        base = play_game(out=out / "base", **common)
        converted = play_game(
            out=out / "converted", record=True, roundtrip=True, **common
        )
        name = f"g{g['id']}.replay.checksums"
        p = out / "base" / name
        q = out / "converted" / name
        same = p.exists() and q.exists() and p.read_bytes() == q.read_bytes()
        return dict(game=g, base=base, converted=converted, checksums_equal=same)

    with ThreadPoolExecutor(2) as pool:
        rows = list(pool.map(compare, games))
    # A configured but absent server must invalidate the match, not look inert.
    bad = SimpleNamespace(
        socket=str(out / "missing.sock"), rollouts=out / "no-rollouts"
    )
    failure = play_game(
        args.root, args.binary, games[0], out / "failure", max_ticks=100, server=bad
    )
    summary = dict(games=rows, missing_server_rejected=failure["status"] == "invalid")
    write_json(out / "verification.json", summary)
    assert all(
        r["checksums_equal"]
        and r["base"]["status"] == r["converted"]["status"] == "complete"
        and r["base"]["map_sha256"] == r["converted"]["map_sha256"]
        for r in rows
    ), summary
    ds = OrderCorpus(out / "converted")
    summary["corpus_records"] = len(ds.records)
    write_json(out / "verification.json", summary)
    assert summary["missing_server_rejected"]
    print(
        f"{len(rows)} checksum comparisons passed; {len(ds.records)} validated teacher records; missing server rejected"
    )


if __name__ == "__main__":
    main()
