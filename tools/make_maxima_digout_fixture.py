#!/usr/bin/env python3
"""Build the deterministic Maxima dig-out scenario map.

The fixture is derived from balanced_for_2.map.  Team 1's starting settlement is
surrounded by a two-cell-thick papyrus wall.  Papyrus blocks ground pathing,
does not spread, and can be removed by a clearing flag, which makes the map a
focused test of the transition from an unreachable enemy to a normal siege.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


SOURCE_MAP = Path("maps/balanced_for_2.map")
OUTPUT_MAP = Path("maps/Maxima_Dig_Out.map")
SOURCE_NAME = b"balanced for 2"
FIXTURE_NAME = b"Maxima Dig Out"

# Inclusive rectangle.  The two-cell wall encloses all of team 1's starting
# units and three buildings while leaving a 21 x 24 usable interior.  The
# balanced source gives both AIs a developed, resource-rich starting position,
# so the scenario reaches military readiness quickly and symmetrically.
LEFT, RIGHT = 3, 27
TOP, BOTTOM = 17, 44
WALL_THICKNESS = 2

MAP_SIGNATURE = b"MapB"
CASE_STRIDE = 33
NO_GID = 0xFFFF
PAPYRUS = 2
PAPYRUS_VARIETY = 0
PAPYRUS_AMOUNT = 4
PAPYRUS_ANIMATION = 0


def read_u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def read_i32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from(">i", data, offset)[0]


def read_u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def wall_cells() -> list[tuple[int, int]]:
    return [
        (x, y)
        for y in range(TOP, BOTTOM + 1)
        for x in range(LEFT, RIGHT + 1)
        if (
            x - LEFT < WALL_THICKNESS
            or RIGHT - x < WALL_THICKNESS
            or y - TOP < WALL_THICKNESS
            or BOTTOM - y < WALL_THICKNESS
        )
    ]


def build_fixture(source: Path, output: Path) -> int:
    data = bytearray(source.read_bytes())
    name_size = read_u32(data, 0)
    if data[4 : 4 + name_size] != SOURCE_NAME:
        raise ValueError("source map has an unexpected internal name")
    if len(FIXTURE_NAME) != name_size:
        raise ValueError("fixture name must preserve the binary header length")
    data[4 : 4 + name_size] = FIXTURE_NAME
    header = 4 + name_size
    major = read_i32(data, header)
    minor = read_i32(data, header + 4)
    team_count = read_i32(data, header + 8)
    map_offset = read_i32(data, header + 12)

    if (major, minor) != (0, 81):
        raise ValueError(
            f"expected the known balanced_for_2 0.81 layout, got {major}.{minor}"
        )
    if team_count != 2:
        raise ValueError(f"expected a two-team source map, got {team_count}")
    if data[map_offset : map_offset + 4] != MAP_SIGNATURE:
        raise ValueError("MapB section is missing at the recorded map offset")

    width_dec = read_i32(data, map_offset + 4)
    height_dec = read_i32(data, map_offset + 8)
    width, height = 1 << width_dec, 1 << height_dec
    if (width, height) != (64, 64):
        raise ValueError(f"expected a 64 x 64 source map, got {width} x {height}")

    # MapB header, then one byte of undermap data per tile, then Case records.
    cases = map_offset + 12 + width * height
    cells = wall_cells()
    for x, y in cells:
        case = cases + (y * width + x) * CASE_STRIDE
        terrain = read_u16(data, case + 4)
        building = read_u16(data, case + 6)
        ground_unit = read_u16(data, case + 12)
        air_unit = read_u16(data, case + 14)
        if terrain >= 16:
            raise ValueError(f"wall tile ({x}, {y}) is not ground terrain: {terrain}")
        if building != NO_GID or ground_unit != NO_GID or air_unit != NO_GID:
            raise ValueError(
                f"wall tile ({x}, {y}) is occupied "
                f"(building={building}, ground={ground_unit}, air={air_unit})"
            )
        data[case + 8 : case + 12] = bytes(
            (PAPYRUS, PAPYRUS_VARIETY, PAPYRUS_AMOUNT, PAPYRUS_ANIMATION)
        )

    # Glob2 hashes the initially written header, where mapOffset and SHA1 are
    # still zero, then seeks back to fill both fields.  Preserve that convention
    # so this fixture has its own network/replay identity instead of retaining
    # the source map's checksum.
    sha1_offset = header + 17
    struct.pack_into(">I", data, header + 12, 0)
    data[sha1_offset : sha1_offset + 20] = bytes(20)
    digest = hashlib.sha1(data).digest()
    struct.pack_into(">I", data, header + 12, map_offset)
    data[sha1_offset : sha1_offset + 20] = digest

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    return len(cells)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / SOURCE_MAP)
    parser.add_argument("--output", type=Path, default=root / OUTPUT_MAP)
    args = parser.parse_args()
    count = build_fixture(args.source, args.output)
    print(
        f"wrote {args.output} with {count} papyrus wall tiles "
        f"around team 1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
