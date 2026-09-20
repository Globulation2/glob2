"""Replay the retained late-game checkpoint and compare every detailed state hash."""
import gzip
import hashlib
import json
import mmap
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test"))
from compare_save_continuation import records

FIXTURES = Path(__file__).resolve().parent / "fixtures/save-continuation"


def check(binary, checkpoint_name, expected_name, stop_tick):
    expected = json.loads((FIXTURES / expected_name).read_text())
    expected = {tick: value for tick, value in expected.items() if int(tick) < stop_tick}
    with tempfile.TemporaryDirectory(prefix="glob2-save-continuation-") as directory:
        output = Path(directory)
        checkpoint = output / "checkpoint.game"
        checkpoint.write_bytes(gzip.decompress((FIXTURES / checkpoint_name).read_bytes()))
        result = subprocess.run(
            [str(Path(binary).resolve()), "--run-game", "--load-game", str(checkpoint),
             "--ticks", str(stop_tick), "--telemetry", "checksums", "--output-dir", str(output / "run")],
            cwd=ROOT, capture_output=True, text=True,
        )
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        with (output / "run/game.replay.checksums").open("rb") as stream:
            with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                actual = {str(t): hashlib.sha256(state).hexdigest() for t, state in records(data)}
        if actual.keys() != expected.keys():
            raise SystemExit("FAIL: missing or extra continuation ticks")
        for tick in expected:
            if actual[tick] != expected[tick]:
                raise SystemExit(f"FAIL: continuation diverges at tick {tick}")
        print(f"PASS: {checkpoint_name} matches all {len(expected)} uninterrupted team/entity state hashes")


def main(binary):
    check(binary, "checkpoint-30000-v115.game.gz", "expected-30000-30512.json", 30512)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_save_continuation_fixture.py BUILD/GLOB2")
    main(sys.argv[1])
