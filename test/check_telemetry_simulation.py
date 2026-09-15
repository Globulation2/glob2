"""Cross-platform per-tick checksum comparison for diagnostic gameplay telemetry."""

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
     ROOT / "test/fixtures/team-stats/telemetry-expansion-gd-large-4ai-1024.checksums.gz"),
    (ROOT / "games/gd-bigarena-long.game", 2048,
     ROOT / "test/fixtures/team-stats/telemetry-expansion-gd-bigarena-2048.checksums.gz"),
)
CHECKPOINT = ROOT / "test/fixtures/team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz"
PARENT_RELOAD = ROOT / "test/fixtures/team-stats/telemetry-expansion-gd-large-4ai-checkpoint-parent-reload-256.checksums.gz"


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


def main(binary: str) -> int:
    for save, ticks, fixture in SCENARIOS:
        with tempfile.TemporaryDirectory(prefix="glob2-telemetry-check-") as directory:
            output = Path(directory)
            command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(save),
                       "--ticks", str(ticks), "--telemetry", "checksums",
                       "--output-dir", str(output)]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                sys.stderr.write(result.stdout + result.stderr)
                return result.returncode
            actual = (output / "game.replay.checksums").read_bytes()
            with gzip.open(fixture, "rb") as stream:
                expected = stream.read()
            if actual != expected:
                print(f"{save.name}: per-tick checksums differ from the pre-telemetry baseline",
                      file=sys.stderr)
                print(f"expected SHA-256 {hashlib.sha256(expected).hexdigest()}", file=sys.stderr)
                print(f"actual   SHA-256 {hashlib.sha256(actual).hexdigest()}", file=sys.stderr)
                return 1
            print(f"PASS {save.name}: {ticks} ticks, identical checksum sidecar: "
                  f"{hashlib.sha256(actual).hexdigest()}")
    with tempfile.TemporaryDirectory(prefix="glob2-telemetry-save-check-") as directory:
        checkpoint = Path(directory) / "checkpoint.game"
        with gzip.open(CHECKPOINT, "rb") as stream:
            checkpoint.write_bytes(stream.read())
        output = Path(directory) / "run"
        command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(checkpoint),
                   "--ticks", "1280", "--telemetry", "checksums", "--output-dir", str(output)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            sys.stderr.write(result.stdout + result.stderr)
            return result.returncode
        with gzip.open(PARENT_RELOAD, "rb") as stream:
            expected = detailed_ticks(stream.read())
        actual = detailed_ticks((output / "game.replay.checksums").read_bytes())
        if actual != expected:
            print("new-format checkpoint changes parent team/entity execution", file=sys.stderr)
            return 1
        print(f"PASS v108 checkpoint: {len(actual)} reloaded ticks, identical parent "
              "team/entity checksum records")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: check_telemetry_simulation.py BUILD/GLOB2")
    sys.exit(main(sys.argv[1]))
