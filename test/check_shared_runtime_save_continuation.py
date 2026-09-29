#!/usr/bin/env python3
"""Verify Runtime scheduling across two save/reload boundaries in real games."""
import argparse
import gzip
from pathlib import Path
import subprocess
import tempfile

from compare_save_continuation import compare

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="glob2-runtime-continuation-") as directory:
        root = Path(directory)
        map_file = root / "arena.map"
        map_file.write_bytes(gzip.decompress(
            (ROOT / "test/fixtures/shared-runtime-continuation/arena.map.gz").read_bytes()))
        for first in ("maxima", "nicowar"):
            case = root / first
            baseline = case / "full"
            command = [str(binary), "--run-game", "--map-file", str(map_file),
                       "--game-seed", "6101", "--player", first, "--player", "nicowar",
                       "--ticks", "8192", "--save", "every:2048", "--telemetry", "checksums",
                       "--output-dir", str(baseline)]
            run(command)
            parent = baseline
            for tick in (4096, 6144):
                child = case / f"resumed-{tick}"
                run([str(binary), "--run-game", "--load-game", str(parent / f"checkpoint-{tick}.game"),
                     "--ticks", "8192", "--save", "every:2048", "--telemetry", "checksums",
                     "--output-dir", str(child)])
                count = compare(baseline / "game.replay.checksums", child / "game.replay.checksums")
                print(f"PASS: {first}/nicowar reload at {tick}: {count} matching ticks")
                parent = child


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(f"{command!r}\n{result.stdout}\n{result.stderr}")


if __name__ == "__main__":
    main()
