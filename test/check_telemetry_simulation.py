"""Run the retained AI and telemetry saves under the default gradient publication
schedule, and check a mid-run save/reload against the uninterrupted run.

Each legacy save runs to its stop tick with a complete checksum sidecar. The v108
checkpoint continuation is saved at tick 1152 and resumed: every remaining tick's
team and entity records must match the uninterrupted run. No committed trace is
compared; tools/sim_fingerprint.py (part `saves`) runs the same saves for sim-diff.

detailed_ticks and detailed_tick_hashes are shared sidecar parsers for other checks.
"""

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
    (ROOT / "content/games/gd-large-4ai.game", 1024),
    (ROOT / "content/games/gd-bigarena-long.game", 2048),
    (ROOT / "test/fixtures/ai-random-streams/numbi-castor-v121.game.gz", 2048),
)
CHECKPOINT = ROOT / "test/fixtures/team-stats/telemetry-expansion-validation/checkpoint-1024-v108.game.gz"
CHECKPOINT_STOP, CHECKPOINT_SAVE = 1280, 1152


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


def run_game(binary, saved, ticks, output, compute, evidence, save_at=None):
    """Run one save headlessly; returns its checksum sidecar bytes, or None on failure."""
    command = [str(Path(binary).resolve()), "game", "run", "--load-game", str(saved),
               "--ticks", str(ticks), "--telemetry", "checksums",
               "--output-dir", str(output), *compute]
    if save_at is not None:
        command += ["--save", f"every:{save_at}"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if evidence is not None:
        output.mkdir(parents=True, exist_ok=True)
        (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
        (output / "run.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        return None
    sidecar = output / "game.replay.checksums"
    if not sidecar.exists():
        print(f"{Path(saved).name}: checksum sidecar missing from {output}", file=sys.stderr)
        sys.stderr.write(result.stdout + result.stderr)
        print("output files:", [p.name for p in output.iterdir()], file=sys.stderr)
        report = output / "result.json"
        if report.exists():
            print(report.read_text(encoding="utf-8")[:4000], file=sys.stderr)
        return None
    return sidecar.read_bytes()


def main(binary: str, parallel_ai: bool = False, evidence: Path | None = None) -> int:
    if evidence is not None:
        evidence = evidence.resolve()
        evidence.mkdir(parents=True, exist_ok=False)
        manifest = {
            "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
            "platform": platform.platform(), "parallelAI": parallel_ai,
            "binarySha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
            "inputHashes": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for original in [*(case[0] for case in SCENARIOS), CHECKPOINT]
                            for p in [original if original.is_file() else Path(str(original) + ".gz")]
                            if p.is_file()},
            "traceHashes": {},
            "checksumCoverage": "Complete checksum sidecars for all legacy fresh loads and the retained v108 "
                                "checkpoint; its midpoint reload compares every team/entity record.",
        }
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    compute = (["--compute-threads", "4"] if parallel_ai
               else ["--compute-threads", "1"])
    for save, ticks in SCENARIOS:
        with run_directory(evidence, save.stem) as directory:
            actual = run_game(binary, save, ticks, Path(directory), compute, evidence)
            if actual is None:
                return 1
            if set(detailed_ticks(actual)) != set(range(ticks)):
                print(f"{save.name}: incomplete scenario trace", file=sys.stderr)
                return 1
            digest = hashlib.sha256(actual).hexdigest()
            if evidence is not None:
                manifest["traceHashes"][save.name] = digest
            print(f"PASS {save.name}: {ticks} ticks, complete checksum sidecar: {digest}")
    with run_directory(evidence, "v108-continuation") as directory:
        checkpoint = Path(directory) / "checkpoint.game"
        with gzip.open(CHECKPOINT, "rb") as stream:
            checkpoint.write_bytes(stream.read())
        output = Path(directory) / "run"
        actual = run_game(binary, checkpoint, CHECKPOINT_STOP, output, compute, evidence, CHECKPOINT_SAVE)
        if actual is None:
            return 1
        uninterrupted = detailed_ticks(actual)
        if set(uninterrupted) != set(range(1024, CHECKPOINT_STOP)):
            print("incomplete checkpoint trace", file=sys.stderr)
            return 1
        saved = sorted(output.glob(f"checkpoint-{CHECKPOINT_SAVE}.game*"))
        if len(saved) != 1:
            print(f"no single save at tick {CHECKPOINT_SAVE}: {[p.name for p in output.iterdir()]}", file=sys.stderr)
            return 1
        resumed_bytes = run_game(binary, saved[0], CHECKPOINT_STOP, Path(directory) / "resumed", compute, evidence)
        if resumed_bytes is None:
            return 1
        resumed = detailed_ticks(resumed_bytes)
        if set(resumed) != set(range(CHECKPOINT_SAVE, CHECKPOINT_STOP)):
            print("incomplete resumed trace", file=sys.stderr)
            return 1
        # Team and entity records exclude the aggregate, which includes the save version.
        diverged = [tick for tick in sorted(resumed) if resumed[tick] != uninterrupted[tick]]
        if diverged:
            print(f"v108 checkpoint: reload at {CHECKPOINT_SAVE} diverges from the uninterrupted run "
                  f"at tick {diverged[0]} ({len(diverged)} ticks differ)", file=sys.stderr)
            return 1
        if evidence is not None:
            manifest["traceHashes"]["v108-continuation"] = hashlib.sha256(actual).hexdigest()
        print(f"PASS v108 checkpoint: {CHECKPOINT_STOP - 1024} ticks; reload at {CHECKPOINT_SAVE} matches "
              f"all {len(resumed)} remaining ticks")
    if evidence is not None:
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--parallel-ai", action="store_true")
    parser.add_argument("--output", type=Path, help="Retain traces, replays, saves, commands and logs")
    args = parser.parse_args()
    sys.exit(main(args.binary, args.parallel_ai, args.output))
