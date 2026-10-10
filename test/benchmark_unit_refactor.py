#!/usr/bin/env python3
"""Release unit-foundation benchmark with the agreed CPU/owner/RSS gates.

Use the resource benchmark's frozen-save manifest and input auditing. Put
"repeats": 16 in crowded priority scenarios and "repeats": 8 in other scenarios.
Both binaries must carry the same owner-timing instrumentation; record its patch
along with the baseline revision. Correctness traces and profiles run separately.
"""
import sys
from pathlib import Path
import benchmark_resource_refactor as benchmark
benchmark.RUNNER_INPUTS += (Path(__file__).resolve(),)
main = benchmark.main

if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:] + [
        '--aggregate-cpu-limit', '1.01', '--scenario-cpu-limit', '1.03',
        '--aggregate-rss-limit', '1.02', '--require-owner-timing']))
