"""Continue a retained legacy save, checking complete hashes and midpoint reload."""
import argparse
from contextlib import nullcontext
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test"))
from check_javascript import complete_ticks, save_header
from compare_save_continuation import compare

FIXTURES = Path(__file__).resolve().parent / "fixtures/save-continuation"
EXPECTED = FIXTURES / "expected-resources-30000-30512.json"


def run(binary, saved, output, stop_tick, workers, checkpoint=False):
    output.mkdir(parents=True, exist_ok=False)
    command = [str(binary), "--run-game", "--load-game", str(saved),
               "--ticks", str(stop_tick), "--compute-threads", str(workers),
               "--telemetry", "checksums",
               "--output-dir", str(output)]
    if checkpoint:
        command += ["--save", "every:30256"]
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (output / "run.log").open("w") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    return complete_ticks((output / "game.replay.checksums").read_bytes())


def check(binary, output, workers, update_fixtures):
    checkpoint = FIXTURES / "checkpoint-30000-v115.game.gz"
    actual = run(binary, checkpoint, output / "run", 30512, workers, checkpoint=True)
    assert set(actual) == set(range(30000, 30512)), "missing or extra continuation ticks"
    hashes = {str(t): hashlib.sha256(state).hexdigest() for t, state in actual.items()}
    current = output / "run/checkpoint-30256.game.gz"
    resumed = run(binary, current, output / "resumed", 30512, workers)
    assert set(resumed) == set(range(30256, 30512)), "missing or extra resumed ticks"
    count = compare(output / "run/game.replay.checksums", output / "resumed/game.replay.checksums")
    # Exactly the same header-version contribution used by the JavaScript
    # checker; all entity bytes and the rest of the aggregate remain covered.
    before, after = save_header(checkpoint), save_header(current)
    assert before[0] == after[0] and before[2:] == after[2:]
    assert after[1] >= before[1]
    delta = before[0] ^ before[1] ^ after[0] ^ after[1]
    rotations = (before[2] + before[3] + 5) % 32
    if rotations:
        delta = ((delta >> rotations) | (delta << (32 - rotations))) & 0xffffffff
    for tick, record in resumed.items():
        expected = actual[tick]
        assert record[:4] == expected[:4] and record[8:] == expected[8:], f"continuation diverges at {tick}"
        assert struct.unpack_from('<I', record, 4)[0] == struct.unpack_from('<I', expected, 4)[0] ^ delta, \
            f"continuation aggregate diverges at {tick}"
    # Never accept a regenerated baseline until the independent reload passes.
    if update_fixtures:
        EXPECTED.write_text(json.dumps(hashes, indent=2) + "\n")
    expected = json.loads(EXPECTED.read_text())
    assert hashes.keys() == expected.keys(), "missing or extra expected ticks"
    for tick in expected:
        assert hashes[tick] == expected[tick], f"continuation baseline diverges at {tick}"
    manifest = {
        "revision": subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        "binarySha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "checkpointSha256": hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
        "traceSha256": hashlib.sha256(EXPECTED.read_bytes()).hexdigest(),
        "workers": workers, "updatedFixtures": update_fixtures,
        "continuationChecksumAdjustment": "MapHeader format-version contribution only",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"PASS: v115 checkpoint matches all {len(hashes)} complete resource state hashes")
    print(f"PASS: save/reload at tick 30256 preserves all {count} complete continuation records")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, help="Retain traces, commands, logs and manifest")
    parser.add_argument("--parallel-ai", action="store_true")
    parser.add_argument("--update-fixtures", action="store_true", help="Regenerate only the resource baseline")
    args = parser.parse_args()
    if args.parallel_ai and args.update_fixtures:
        parser.error("generate fixtures serially, then verify --parallel-ai without --update-fixtures")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
    context = nullcontext(str(args.output.resolve())) if args.output else tempfile.TemporaryDirectory(prefix="glob2-save-continuation-")
    with context as directory:
        check(args.binary.resolve(), Path(directory), 4 if args.parallel_ai else 1, args.update_fixtures)


if __name__ == "__main__":
    main()
