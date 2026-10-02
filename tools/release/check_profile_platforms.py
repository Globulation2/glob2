#!/usr/bin/env python3
"""Require matching per-tick execution across Linux/Windows size experiments."""

import argparse
import json
from pathlib import Path

PROFILES = ("gc", "lto", "size")


def verify(roots):
    reference = None
    platforms = {}
    for root in roots:
        root = Path(root)
        for profile in PROFILES:
            report = json.loads((root / profile / "measurements.json").read_text())
            if report["profile"] != profile:
                raise ValueError("Profile evidence is mislabelled: " + str(root))
            traces = {
                scenario: dict(
                    fixture=value["fixture_sha256"],
                    trace=value["checksums"]["baseline"]["game.replay.checksums"],
                )
                for scenario, value in report["scenarios"].items()
            }
            if not traces:
                raise ValueError("Profile evidence contains no scenarios")
            for scenario, value in report["scenarios"].items():
                if (
                    value["checksums"]["baseline"]["game.replay.checksums"]
                    != value["checksums"]["candidate"]["game.replay.checksums"]
                ):
                    raise ValueError(
                        "Candidate changes per-tick execution: " + scenario
                    )
            identity = dict(revision=report["revision"], traces=traces)
            if reference is None:
                reference = identity
            elif identity != reference:
                raise ValueError(
                    "Cross-platform profile checksums or sources differ: "
                    + str(root / profile)
                )
            platforms[report["platform"]] = report["architecture"]
    if set(platforms) != {"Linux", "Windows"}:
        raise ValueError("Both Linux and Windows execution evidence are required")
    return dict(**reference, platforms=platforms, profiles=PROFILES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.roots)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
