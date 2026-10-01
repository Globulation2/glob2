"""Cross-platform per-tick checksums for the default gradient publication schedule."""

import argparse
from contextlib import contextmanager
import json
import platform
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
     ROOT / "test/fixtures/scoped-gradients/gd-large-4ai-1024.checksums.gz"),
    (ROOT / "games/gd-bigarena-long.game", 2048,
     ROOT / "test/fixtures/scoped-gradients/gd-bigarena-2048.checksums.gz"),
    (ROOT / "test/fixtures/ai-random-streams/numbi-castor-v121.game.gz", 2048,
     ROOT / "test/fixtures/scoped-gradients/numbi-castor-2048.checksums.gz"),
)
CHECKPOINT = ROOT / "test/fixtures/team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz"
PARENT_RELOAD = ROOT / "test/fixtures/scoped-gradients/v108-reload-256.checksums.gz"


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


@contextmanager
def run_directory(evidence, name):
    if evidence is None:
        with tempfile.TemporaryDirectory(prefix="glob2-released-check-", dir=ROOT) as directory:
            yield directory
    else:
        directory = evidence / name
        directory.mkdir()
        yield str(directory)


def main(binary: str, parallel_ai: bool = False, evidence: Path | None = None) -> int:
    if evidence is not None:
        evidence = evidence.resolve()
        evidence.mkdir(parents=True, exist_ok=False)
        manifest = {
            "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
            "platform": platform.platform(), "parallelAI": parallel_ai,
            "binarySha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
            "fixtureHashes": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for original in [*(p for case in SCENARIOS for p in (case[0], case[2])), CHECKPOINT, PARENT_RELOAD]
                              for p in [original if original.is_file() else Path(str(original) + ".gz")]},
            "checkpointAggregateExclusion": "Version-dependent aggregate changes after save/load; compare every stored team/entity record against the released reference. Fresh-load traces compare complete bytes.",
        }
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    compute = (["--compute-threads", "4", "--compute-experiments", "ai"] if parallel_ai
               else ["--compute-threads", "1", "--compute-experiments", "ai"])
    for save, ticks, fixture in SCENARIOS:
        with run_directory(evidence, save.stem) as directory:
            output = Path(directory)
            command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(save),
                       "--ticks", str(ticks), "--telemetry", "checksums",
                       "--output-dir", str(output), *compute]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if evidence is not None:
                (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
                (output / "run.log").write_text(result.stdout + result.stderr)
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
                print(f"{save.name}: per-tick checksums differ from the version-123 serial reference",
                      file=sys.stderr)
                print(f"expected SHA-256 {hashlib.sha256(expected).hexdigest()}", file=sys.stderr)
                print(f"actual   SHA-256 {hashlib.sha256(actual).hexdigest()}", file=sys.stderr)
                return 1
            print(f"PASS {save.name}: {ticks} ticks, identical checksum sidecar: "
                  f"{hashlib.sha256(actual).hexdigest()}")
    with run_directory(evidence, "v108-continuation") as directory:
        checkpoint = Path(directory) / "checkpoint.game"
        with gzip.open(CHECKPOINT, "rb") as stream:
            checkpoint.write_bytes(stream.read())
        output = Path(directory) / "run"
        command = [str(Path(binary).resolve()), "--run-game", "--load-game", str(checkpoint),
                   "--ticks", "1280", "--telemetry", "checksums", "--output-dir", str(output),
                   *compute]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if evidence is not None:
            output.mkdir(parents=True, exist_ok=True)
            (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
            (output / "run.log").write_text(result.stdout + result.stderr)
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
            print("legacy checkpoint differs from version-123 team/entity reference", file=sys.stderr)
            return 1
        print(f"PASS v108 checkpoint: {len(actual)} reloaded ticks, identical eight-tick "
              "team/entity checksum records")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--parallel-ai", action="store_true")
    parser.add_argument("--output", type=Path, help="Retain traces, replays, saves, commands and logs")
    args = parser.parse_args()
    sys.exit(main(args.binary, args.parallel_ai, args.output))
