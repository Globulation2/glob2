#!/usr/bin/env python3
"""Check all nine shapes, supported colony counts, and worker-domain endpoints."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tools.map_generation_study import generate_map

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", required=True)
parser.add_argument("--out", required=True, type=Path)
parser.add_argument("--jobs", type=int, default=4)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
jobs = [(w, h, teams, workers, seed)
        for w in (128, 256, 512) for h in (128, 256, 512)
        for teams in range(2, (4 if min(w, h) == 128 else 12) + 1)
        for workers in (1, 8) for seed in (11, 29, 71)]
jobs += [(64, 256, 4, 4, 1), (256, 256, 1, 4, 1),
         (128, 512, 5, 4, 1), (512, 128, 5, 4, 1)]
(args.out / "metadata.json").write_text(json.dumps({
    "binary": str(Path(args.binary).resolve()),
    "sha256": hashlib.sha256(Path(args.binary).read_bytes()).hexdigest(),
    "jobs": jobs,
}, indent=2) + "\n")

def run(job):
    w, h, teams, workers, seed = job
    result = generate_map(args.binary, 69, seed, w, h, teams, {"workers": workers})
    expected = w >= 128 and h >= 128 and teams >= 2 and (min(w, h) > 128 or teams <= 4)
    status = (result["native"] or {}).get("status")
    return dict(request=job, expected_success=expected,
                passed=result["category"] == "completed" if expected else status == "invalid_request",
                **result)

with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
    with (args.out / "results.jsonl").open("w") as output:
        for row in pool.map(run, jobs):
            output.write(json.dumps(row) + "\n")
            output.flush()
            if not row["passed"]:
                print(row["request"], row["detail"], flush=True)
print(f"Completed {len(jobs)} envelope checks")
