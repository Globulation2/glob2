#!/usr/bin/env python3
"""Inflate a checked-in ".gz" map/save fixture to a temporary raw file.

Repository map and save fixtures are stored gzip-compressed (see the gzip-by-
default map/save format). Tests that specifically exercise loading a raw,
uncompressed legacy file (as opposed to the engine's own transparent gzip
support) materialize one from the checked-in fixture at test time instead of
also keeping an uncompressed copy in the repository.
"""
import gzip
import sys
from pathlib import Path


def inflate_fixture(source, destination):
    """Writes the gzip-decompressed bytes of source to destination, returning it."""
    destination = Path(destination)
    destination.write_bytes(gzip.decompress(Path(source).read_bytes()))
    return destination


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(f'usage: {sys.argv[0]} <source.gz> <destination>')
    inflate_fixture(sys.argv[1], sys.argv[2])
