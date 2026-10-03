"""Measure actual gzip saves with isolated profiles; retain JSON and saved outputs.

Run once with each revision's SaveSizeHarness and the same input files. Timings
are medians of independent processes. Peak RSS includes loading and serialization,
and is sampled before section-attribution compression. Section gzip sizes are
independent estimates, not additive contributions to the whole gzip stream.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import hashlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("harness", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = []
    binary_sha256 = hashlib.sha256(args.harness.read_bytes()).hexdigest()
    for index, source in enumerate(args.inputs):
        runs = []
        for repeat in range(args.repeats):
            prefix = (args.output / f"{index:03d}-{source.name}-{repeat}").resolve()
            with tempfile.TemporaryDirectory(prefix="glob2-save-size-") as profile:
                env = dict(os.environ, GLOB2_USER_DATA_DIR=profile)
                with prefix.with_suffix(prefix.suffix + ".log").open("w") as log:
                    subprocess.run([str(args.harness.resolve()), str(source.resolve()), str(prefix)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            runs.append(json.loads(Path(str(prefix) + ".json").read_text()))
        summary = dict(runs[0])
        summary["harness_sha256"] = binary_sha256
        summary["input_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        for field in ("load_ms", "serialize_ms", "capture_ms", "encode_ms", "compress_ms", "write_ms", "total_ms", "process_peak_rss_bytes"):
            if field in summary:
                summary[field] = statistics.median(r[field] for r in runs)
                summary[field + "_max"] = max(r[field] for r in runs)
        assert len({(r["raw_bytes"], r["gzip_bytes"]) for r in runs}) == 1, "Nondeterministic save size"
        summaries.append(summary)
        print(f"{source}: {summary['raw_bytes']:,} raw / {summary['gzip_bytes']:,} gzip bytes")
    (args.output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")


if __name__ == "__main__":
    main()
