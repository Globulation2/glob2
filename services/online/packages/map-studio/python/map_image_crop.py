#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Crop an edited 2048px 2x2 map mosaic to 1024px; requires Pillow."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from map_images.crop import COLORS, crop_image, decode


def main():
    parser = argparse.ArgumentParser(
        description="Choose a four-colony crop from an edited 2x2 map image; no API access."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument(
        "--normalize",
        action="store_true",
        help="Scale the entire square input to 2048px using nearest neighbor",
    )
    parser.add_argument(
        "--require-repeated-map",
        action="store_true",
        help="Require 16 colony marker components in the full mosaic",
    )
    parser.add_argument(
        "--overlay",
        type=Path,
        help="Save the normalized full mosaic with the chosen crop outlined",
    )
    parser.add_argument(
        "--inputs",
        type=Path,
        help="Verify prepared image-inputs.json prompt/sheet hashes and record provenance",
    )
    args = parser.parse_args()
    try:
        report = crop_image(
            args.input,
            args.output,
            args.report,
            normalize=args.normalize,
            require_repeated_map=args.require_repeated_map,
            overlay=args.overlay,
            inputs=args.inputs,
        )
    except (ValueError, OSError, ImportError) as error:
        parser.exit(1, f"Map crop: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
