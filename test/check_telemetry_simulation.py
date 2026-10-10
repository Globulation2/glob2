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
     ROOT / "test/fixtures/scoped-gradients/gd-large-4ai-1024.units.checksums.gz"),
    (ROOT / "games/gd-bigarena-long.game", 2048,
     ROOT / "test/fixtures/scoped-gradients/gd-bigarena-2048.units.checksums.gz"),
    (ROOT / "test/fixtures/ai-random-streams/numbi-castor-v121.game.gz", 2048,
     ROOT / "test/fixtures/scoped-gradients/numbi-castor-2048.units.checksums.gz"),
)
CHECKPOINT = ROOT / "test/fixtures/team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz"
PARENT_RELOAD = ROOT / "test/fixtures/scoped-gradients/v108-reload-256.units.checksums.gz"


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


def detailed_tick_hashes(path: Path) -> dict[int, bytes]:
    """Hash every team/entity byte per tick without retaining large sidecars.

    Like detailed_ticks, exclude only the sidecar metadata and each tick's
    aggregate checksum, which includes the save version. Read payloads in
    bounded chunks so correctness checks do not inflate inherited runner RSS.
    """
    with path.open("rb") as stream:
        def read_exact(size):
            data = stream.read(size)
            if len(data) != size:
                raise ValueError("truncated checksum sidecar")
            return data

        header = read_exact(20)
        if header[:4] != b"GCS1":
            raise ValueError("invalid checksum sidecar signature")
        teams, _, count, _ = struct.unpack_from("<4I", header, 4)
        records = {}
        for _ in range(count):
            tick = struct.unpack_from("<I", read_exact(8))[0]
            if tick in records:
                raise ValueError("duplicate checksum sidecar tick")
            record = hashlib.sha256()
            for _ in range(teams):
                record.update(read_exact(4))  # team checksum
                for _ in range(2):  # units, buildings
                    entities = read_exact(4)
                    record.update(entities)
                    for _ in range(struct.unpack("<I", entities)[0]):
                        entity = read_exact(10)
                        record.update(entity)
                        remaining = 4 * struct.unpack_from("<I", entity, 6)[0]
                        while remaining:
                            chunk = read_exact(min(remaining, 65536))
                            record.update(chunk)
                            remaining -= len(chunk)
            records[tick] = record.digest()
        if stream.read(1):
            raise ValueError("checksum sidecar has extra data")
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


def main(binary: str, parallel_ai: bool = False, evidence: Path | None = None, update_fixtures: bool = False) -> int:
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
                              for p in [original if original.is_file() else Path(str(original) + ".gz")]
                              if p.is_file()},
            "updatedFixtures": update_fixtures,
            "checksumCoverage": "Complete checksum sidecars, including aggregate, for all legacy fresh loads and the retained v108 checkpoint.",
        }
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    compute = (["--compute-threads", "4"] if parallel_ai
               else ["--compute-threads", "1"])
    for save, ticks, fixture in SCENARIOS:
        with run_directory(evidence, save.stem) as directory:
            output = Path(directory)
            command = [str(Path(binary).resolve()), "game", "run", "--load-game", str(save),
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
            assert set(detailed_ticks(actual)) == set(range(ticks)), "incomplete scenario trace"
            if update_fixtures:
                fixture.write_bytes(gzip.compress(actual, mtime=0))
            with gzip.open(fixture, "rb") as stream:
                expected = stream.read()
            if actual != expected:
                print(f"{save.name}: per-tick checksums differ from the current simulation serial reference",
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
        command = [str(Path(binary).resolve()), "game", "run", "--load-game", str(checkpoint),
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
        actual = (output / "game.replay.checksums").read_bytes()
        assert set(detailed_ticks(actual)) == set(range(1024, 1280)), "incomplete checkpoint trace"
        if update_fixtures:
            PARENT_RELOAD.write_bytes(gzip.compress(actual, mtime=0))
        with gzip.open(PARENT_RELOAD, "rb") as stream:
            expected = stream.read()
        if actual != expected:
            print("legacy checkpoint differs from complete current simulation reference", file=sys.stderr)
            return 1
        print("PASS v108 checkpoint: 256 reloaded ticks, identical complete checksum sidecar")
    if evidence is not None:
        # Include freshly generated traces in regeneration evidence too.
        for fixture in [case[2] for case in SCENARIOS] + [PARENT_RELOAD]:
            manifest["fixtureHashes"][str(fixture.relative_to(ROOT))] = hashlib.sha256(fixture.read_bytes()).hexdigest()
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--parallel-ai", action="store_true")
    parser.add_argument("--update-fixtures", action="store_true",
                        help="Regenerate current simulation traces from retained legacy saves")
    parser.add_argument("--output", type=Path, help="Retain traces, replays, saves, commands and logs")
    args = parser.parse_args()
    if args.update_fixtures and args.parallel_ai:
        parser.error("generate fixtures serially, then verify --parallel-ai without --update-fixtures")
    sys.exit(main(args.binary, args.parallel_ai, args.output, args.update_fixtures))
