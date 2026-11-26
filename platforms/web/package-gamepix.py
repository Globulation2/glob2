#!/usr/bin/env python3
"""Repackage a verified browser release package as a GamePix HTML5 upload.

Kept for existing commands: this is `package-portal.py --portal gamepix`, which
documents the packaging.
"""

import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("package_portal", Path(__file__).resolve().with_name("package-portal.py"))
_portal = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_portal)

PROFILE = _portal.portal("gamepix")
ROOT = _portal.ROOT
SDK_URL = PROFILE.sdk_url
ADAPTER = "gamepix.js"
ROOT_REFERENCE = _portal.ROOT_REFERENCE
EXTERNAL_REFERENCE = _portal.EXTERNAL_REFERENCE
write_zip = _portal.write_zip


def rewrite_index(html):
    return _portal.rewrite_index(html, PROFILE)


def collect(source, design_system):
    return _portal.collect(source, design_system, PROFILE)


def verify(path):
    _portal.verify(path, PROFILE)


def main(argv=None):
    _portal.main(argv, default_portal="gamepix")


if __name__ == "__main__":
    main()
