"""Import the common development-store resolver from standalone mobile commands."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))
from dev_store import (
    android_sdk,
    dependency_prefix,
    gradle_home,
    isolated,
    mobile_tools,
)

__all__ = [
    "android_sdk",
    "dependency_prefix",
    "gradle_home",
    "isolated",
    "mobile_tools",
]
