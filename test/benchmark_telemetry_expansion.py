"""Repeatable paired process-CPU comparison for gameplay telemetry changes.

The before executable must be built from the parent commit with the same build
settings. Alternating or mirrored ABBA/BAAB order reduces thermal and
background-load bias.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import resource
import statistics
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(binary: Path, save: Path, ticks: int) -> dict:
    with tempfile.TemporaryDirectory(prefix="glob2-telemetry-benchmark-") as output:
        command = [str(binary), "--run-game", "--load-game", str(save),
                   "--ticks", str(ticks), "--output-dir", output]
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        wall = time.perf_counter() - started
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        if result.returncode:
            raise RuntimeError(f"{command}:\n{result.stdout}\n{result.stderr}")
        return {"wall_s": wall, "user_s": after.ru_utime-before.ru_utime,
                "system_s": after.ru_stime-before.ru_stime}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("save", type=Path)
    parser.add_argument("--ticks", type=int, default=2048)
    parser.add_argument("--pairs", type=int, default=10)
    parser.add_argument("--order", choices=("alternating","abba"), default="alternating")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before, after, save = (p.resolve() for p in (args.before,args.after,args.save))
    measurements = []
    for pair in range(args.pairs):
        if args.order == "abba":
            order = ("before","after","after","before") if pair % 2 == 0 else \
                    ("after","before","before","after")
        else:
            order = ("before","after") if pair % 2 == 0 else ("after","before")
        for slot, variant in enumerate(order):
            row = {"pair": pair, "slot": slot, "variant": variant,
                   **run(before if variant == "before" else after, save, args.ticks)}
            measurements.append(row)
            print(json.dumps(row), flush=True)
    medians = {variant: statistics.median(r["user_s"] for r in measurements
                                            if r["variant"] == variant)
               for variant in ("before","after")}
    result = {"platform": platform.platform(), "save": str(save),
              "save_sha256": digest(save), "ticks": args.ticks, "pairs": args.pairs,
              "order": args.order,
              "commands": {variant: [str(binary),"--run-game","--load-game",str(save),
                                     "--ticks",str(args.ticks),"--output-dir","<temp>"]
                           for variant,binary in (("before",before),("after",after))},
              "binary_sha256": {"before":digest(before),"after":digest(after)},
              "measurements":measurements,"median_user_s":medians,
              "change_percent": 100*(medians["after"]/medians["before"]-1)}
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(f"median user CPU: {medians}, change {result['change_percent']:+.3f}%", flush=True)


if __name__ == "__main__":
    main()
