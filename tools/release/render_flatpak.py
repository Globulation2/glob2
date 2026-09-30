#!/usr/bin/env python3
"""Pin the Flatpak recipe to one upstream commit for build and Flathub review."""

import argparse
import re
from pathlib import Path


def render(template, destination, commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("commit must be a full lowercase Git SHA-1")
    source = template.read_text()
    if source.count("RELEASE_COMMIT") != 1:
        raise ValueError("Flatpak template must contain one RELEASE_COMMIT marker")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(source.replace("RELEASE_COMMIT", commit))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commit")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(Path("flatpak/org.globulation2.Globulation2.yml.in"), args.output, args.commit)
