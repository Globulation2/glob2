#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Select concept-matched generators, then render exact 2x2 image references.

The optional selector uses OpenAI text Structured Outputs. Image generation stays
separate: prepare writes its prompt and ordered reference paths for the image tool.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

# Keep this executable entry point stable; implementation lives in map_images.
from map_images.catalog import build_catalog
from map_images.common import digest, write_json
from map_images.references import image_prompt, prepare, reference_sheets
from map_images.selection import (
    DEFAULT_EXAMPLE_COUNT,
    MAX_EXAMPLE_COUNT,
    response_selection,
    select,
    selection_schema,
    selector_input,
    validate_selection,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="command", required=True)
    catalog = modes.add_parser(
        "catalog", help="Write description/tag catalog without an API call"
    )
    catalog.add_argument("--binary", type=Path, required=True)
    catalog.add_argument("--output", type=Path, required=True)
    selector = modes.add_parser(
        "select", help="Analyze a concept using a structured text LLM call"
    )
    selector.add_argument("--binary", type=Path, required=True)
    selector.add_argument("--concept", type=Path, required=True)
    selector.add_argument("--output", type=Path, required=True)
    selector.add_argument("--model", default="gpt-5-mini")
    selector.add_argument(
        "--count",
        type=int,
        choices=range(1, MAX_EXAMPLE_COUNT + 1),
        default=DEFAULT_EXAMPLE_COUNT,
    )
    renderer = modes.add_parser(
        "prepare", help="Generate selected examples and write the final image prompt"
    )
    renderer.add_argument("--binary", type=Path, required=True)
    renderer.add_argument("--selection-dir", type=Path, required=True)
    renderer.add_argument("--output", type=Path, required=True)
    renderer.add_argument("--seed", type=int, default=101)
    args = parser.parse_args()
    try:
        if args.command == "catalog":
            args.output.parent.mkdir(parents=True, exist_ok=True)
            write_json(args.output, build_catalog(args.binary.resolve()))
        elif args.command == "select":
            select(args)
        else:
            if not 0 <= args.seed <= 4294967295:
                raise ValueError("Seed must fit an unsigned32-bit integer")
            prepare(args)
    except (
        ValueError,
        OSError,
        subprocess.SubprocessError,
        KeyError,
        ImportError,
    ) as error:
        parser.exit(1, f"Map examples: {error}\n")


if __name__ == "__main__":
    main()
