"""Compare every team/entity record in a resumed run with uninterrupted execution.

The aggregate checksum includes the save format/header, so compare its detailed
simulation records instead. Stream through memory maps to handle long games
without copying gigabytes of records into Python dictionaries.
"""
import argparse
import mmap
from pathlib import Path
import struct


def records(data):
    if len(data) < 20 or data[:4] != b"GCS1":
        raise ValueError("invalid checksum sidecar header")
    teams, _, count, _ = struct.unpack_from("<4I", data, 4)
    if not 1 <= teams <= 32:
        raise ValueError("invalid team count")
    position = 20
    previous = None
    for _ in range(count):
        tick = struct.unpack_from("<I", data, position)[0]
        if previous is not None and tick != previous + 1:
            raise ValueError("nonconsecutive checksum ticks")
        previous = tick
        position += 8
        start = position
        for _ in range(teams):
            position += 4
            for _ in range(2):
                entities = struct.unpack_from("<I", data, position)[0]
                position += 4
                for _ in range(entities):
                    fields = struct.unpack_from("<I", data, position + 6)[0]
                    position += 10 + 4 * fields
                    if position > len(data):
                        raise ValueError("truncated entity record")
        yield tick, data[start:position]
    if position != len(data):
        raise ValueError("extra or truncated checksum records")


def compare(uninterrupted, resumed):
    with Path(uninterrupted).open("rb") as af, Path(resumed).open("rb") as bf:
        with mmap.mmap(af.fileno(), 0, access=mmap.ACCESS_READ) as a, mmap.mmap(
            bf.fileno(), 0, access=mmap.ACCESS_READ
        ) as b:
            if a[4:12] != b[4:12]:
                raise ValueError("team/player counts differ")
            baseline = iter(records(a))
            current = next(baseline, None)
            count = 0
            for tick, actual in records(b):
                while current is not None and current[0] < tick:
                    current = next(baseline, None)
                if current is None or current[0] != tick:
                    raise ValueError(f"baseline has no record for tick {tick}")
                if current[1] != actual:
                    raise ValueError(f"team/entity state diverges at tick {tick}")
                count += 1
            if count == 0:
                raise ValueError("resumed trace has no ticks")
            # Also validate the rest of the baseline file.
            for _ in baseline:
                pass
            return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uninterrupted", type=Path)
    parser.add_argument("resumed", type=Path, nargs="+")
    args = parser.parse_args()
    for resumed in args.resumed:
        try:
            count = compare(args.uninterrupted, resumed)
        except (ValueError, struct.error) as error:
            raise SystemExit(f"FAIL {resumed}: {error}") from error
        print(f"PASS {resumed}: {count} consecutive team/entity records match")
