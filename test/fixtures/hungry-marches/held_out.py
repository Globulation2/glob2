#!/usr/bin/env python3
"""Replay a control study's settings with fresh seeds and a frozen binary."""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--input", required=True, type=Path)
parser.add_argument("--binary", required=True)
parser.add_argument("--out", required=True, type=Path)
parser.add_argument("--count", type=int, default=1000)
parser.add_argument("--offset", type=int, default=500000)
parser.add_argument("--jobs", type=int, default=4)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "control_study", root / ".agents/skills/glob2-map-design/scripts/control_study.py")
study = importlib.util.module_from_spec(spec)
spec.loader.exec_module(study)
rows = [json.loads(line) for line in args.input.read_text().splitlines()][:args.count]
if len(rows) != args.count:
    raise SystemExit("The input study has fewer completed requests than requested")
jobs = [dict(study="held-out", control="", value=0, seed=r["seed"] + args.offset,
             w=r["w"], h=r["h"], teams=r["teams"], set=r["set"],
             binary=args.binary, method=69, generator="hungry-marches") for r in rows]
args.out.mkdir(parents=True, exist_ok=True)
(args.out / "metadata.json").write_text(json.dumps({
    "binary_sha256": hashlib.sha256(Path(args.binary).read_bytes()).hexdigest(),
    "requests": jobs,
}, indent=2) + "\n")
with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
    with (args.out / "results.jsonl").open("w") as output:
        for result in pool.map(study.run, jobs):
            output.write(json.dumps(result) + "\n")
            output.flush()
            if not result["ok"] and result.get("native_status") != "invalid_request":
                print(result["seed"], result["detail"], flush=True)
print(f"Completed {len(jobs)} held-out requests")
