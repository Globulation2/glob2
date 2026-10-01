#!/usr/bin/env python3
"""Reconstruct the retained scripting sources from pinned archives and local patches.

Downloads occur only in a temporary directory. --archives allows offline verification
from a directory containing quickjs-ng.tar.gz and openlibm.tar.gz. Never modifies
vendored sources or regenerates patches: unexplained changes fail the command.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request


def verify(archives=None):
    root = Path(__file__).resolve().parents[2]
    third_party = root / 'third_party'
    lock = json.loads((third_party / 'javascript-vendor.json').read_text())
    with tempfile.TemporaryDirectory(prefix='glob2-js-vendor-') as temporary:
        temporary = Path(temporary)
        for dependency in lock['dependencies']:
            name = dependency['name']
            archive = temporary / (name + '.tar.gz')
            if archives:
                shutil.copyfile(archives / archive.name, archive)
            else:
                urllib.request.urlretrieve(dependency['url'], archive)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            if digest != dependency['sha256']:
                raise RuntimeError(f'{name}: archive hash differs: {digest}')
            reconstructed = temporary / name
            reconstructed.mkdir()
            with tarfile.open(archive) as tar:
                for relative in dependency['files']:
                    try:
                        member = tar.getmember(dependency['archive_root'] + '/' + relative)
                    except KeyError:
                        continue  # locally introduced files are added by the patch series
                    if not member.isfile():
                        raise RuntimeError(f'{name}/{relative}: expected regular upstream file')
                    destination = reconstructed / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(tar.extractfile(member).read())
            for patch in dependency['patches']:
                subprocess.run(['patch', '--batch', '-p1', '-i', str(third_party / patch)],
                               cwd=reconstructed, check=True, stdout=subprocess.DEVNULL)
            expected = set(dependency['files'])
            actual = {p.relative_to(third_party / name).as_posix()
                      for p in (third_party / name).rglob('*') if p.is_file()}
            if actual != expected:
                raise RuntimeError(f'{name}: retained file list differs: {actual ^ expected}')
            for relative in sorted(expected):
                if (reconstructed / relative).read_bytes() != (third_party / name / relative).read_bytes():
                    raise RuntimeError(f'{name}/{relative}: unexplained vendor difference')
            print(f'{name}: {len(expected)} files reproduced at {dependency["revision"]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives', type=Path)
    verify(parser.parse_args().archives)
