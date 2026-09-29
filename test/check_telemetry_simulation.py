"""Cross-platform per-tick checksums for the default gradient publication schedule."""

import argparse
import gzip
import hashlib
from pathlib import Path
import subprocess
import struct
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = (
    (ROOT / "games/gd-large-4ai.game", 1024,
     ROOT / "test/fixtures/gradient-pipeline/gd-large-4ai-1024.checksums.gz"),
    (ROOT / "games/gd-bigarena-long.game", 2048,
     ROOT / "test/fixtures/gradient-pipeline/gd-bigarena-2048.checksums.gz"),
)
CHECKPOINT = ROOT / "test/fixtures/team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz"
PARENT_RELOAD = ROOT / "test/fixtures/gradient-pipeline/v108-reload-256.checksums.gz"


def detailed_ticks(data: bytes) -> dict[int, bytes]:
    """Return team/entity records; aggregate checksum includes save version."""
    if data[:4] != b"GCS1":
        raise ValueError("invalid checksum sidecar signature")
    teams, _, count, _ = struct.unpack_from("<4I", data, 4)
    position, records = 20, {}
    for _ in range(count):
        tick = struct.unpack_from("<I", data, position)[0]
        position += 8  # tick and aggregate checksum
        start = position
        for _ in range(teams):
            position += 4  # team checksum
            for _ in range(2):  # units, buildings
                entities = struct.unpack_from("<I", data, position)[0]
                position += 4
                for _ in range(entities):
                    fields = struct.unpack_from("<I", data, position + 6)[0]
                    position += 10 + 4 * fields
        records[tick] = data[start:position]
    if position != len(data):
        raise ValueError("checksum sidecar has extra or truncated data")
    return records


def main(binary: str, parallel_ai: bool = False) -> int:
    compute = ["--compute-threads", "4", "--compute-experiments", "ai"] if parallel_ai else []
    for save, ticks, fixture in SCENARIOS:
        with tempfile.TemporaryDirectory(prefix="glob2-telemetry-check-", dir=ROOT) as directory:
            output = Path(directory)
            command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(save),
                       "--ticks", str(ticks), "--telemetry", "checksums",
                       "--output-dir", str(output), *compute]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                sys.stderr.write(result.stdout + result.stderr)
                return result.returncode
            sidecar = output / "game.replay.checksums"
            if not sidecar.exists():
                print(f"{save.name}: checksum sidecar missing from {output}", file=sys.stderr)
                sys.stderr.write(result.stdout + result.stderr)
                print("output files:", [p.name for p in output.iterdir()], file=sys.stderr)
                report = output / "result.json"
                if report.exists():
                    print(report.read_text(encoding="utf-8")[:4000], file=sys.stderr)
                return 1
            actual = sidecar.read_bytes()
            with gzip.open(fixture, "rb") as stream:
                expected = stream.read()
            if actual != expected:
                print(f"{save.name}: per-tick checksums differ from the eight-tick serial reference",
                      file=sys.stderr)
                print(f"expected SHA-256 {hashlib.sha256(expected).hexdigest()}", file=sys.stderr)
                print(f"actual   SHA-256 {hashlib.sha256(actual).hexdigest()}", file=sys.stderr)
                return 1
            print(f"PASS {save.name}: {ticks} ticks, identical checksum sidecar: "
                  f"{hashlib.sha256(actual).hexdigest()}")
    with tempfile.TemporaryDirectory(prefix="glob2-telemetry-save-check-", dir=ROOT) as directory:
        checkpoint = Path(directory) / "checkpoint.game"
        with gzip.open(CHECKPOINT, "rb") as stream:
            checkpoint.write_bytes(stream.read())
        output = Path(directory) / "run"
        command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(checkpoint),
                   "--ticks", "1280", "--telemetry", "checksums", "--output-dir", str(output),
                   *compute]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            sys.stderr.write(result.stdout + result.stderr)
            return result.returncode
        if not (output / "game.replay.checksums").exists():
            print("checkpoint checksum sidecar missing", file=sys.stderr)
            sys.stderr.write(result.stdout + result.stderr)
            print("output files:", [p.name for p in output.iterdir()], file=sys.stderr)
            return 1
        with gzip.open(PARENT_RELOAD, "rb") as stream:
            expected = detailed_ticks(stream.read())
        actual = detailed_ticks((output / "game.replay.checksums").read_bytes())
        if actual != expected:
            print("legacy checkpoint differs from eight-tick team/entity reference", file=sys.stderr)
            return 1
        print(f"PASS v108 checkpoint: {len(actual)} reloaded ticks, identical eight-tick "
              "team/entity checksum records")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--parallel-ai", action="store_true")
    args = parser.parse_args()
    sys.exit(main(args.binary, args.parallel_ai))
