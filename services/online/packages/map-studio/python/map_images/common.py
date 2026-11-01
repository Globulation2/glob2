# SPDX-License-Identifier: GPL-3.0-or-later
"""File and dependency helpers shared by the optional map-image tools."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]


# Generator descriptions and assets belong to Glob2, not to this service.
# Explicit paths avoid silently selecting an unrelated sibling checkout.
def glob2_source():
    value = os.environ.get("GLOB2_SOURCE_DIR")
    if not value:
        raise ValueError("Set GLOB2_SOURCE_DIR to the matching Glob2 source checkout")
    root = Path(value).expanduser().resolve()
    if not (root / "src/map/generator/generators").is_dir():
        raise ValueError("GLOB2_SOURCE_DIR is not a Glob2 source checkout")
    return root


def write_json(path, value):
    Path(path).write_text(
        json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def nearest_resampling():
    """Load Pillow lazily; support distro versions predating Image.Resampling."""
    from PIL import Image

    return getattr(Image, "Resampling", Image).NEAREST
