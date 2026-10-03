#!/usr/bin/env python3
"""Record runtime package owners from real ELF dependencies; validate before use."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def packages_for(binary, run=subprocess.check_output):
    linked = run(['ldd', str(binary)], text=True, stderr=subprocess.STDOUT)
    if 'not found' in linked:
        raise ValueError(f'unresolved runtime library in {binary}: {linked}')
    libraries = re.findall(r'(?:=>\s+)?(/[^\s]+)', linked)
    owners = set()
    for library in libraries:
        # usrmerge symlinks may be indexed under /lib instead of /usr/lib.
        candidates = [library, str(Path(library).resolve())]
        for item in tuple(candidates):
            if item.startswith('/usr/lib/'):
                candidates.append(item[4:])
        for candidate in candidates:
            try:
                output = run(['dpkg-query', '-S', candidate], text=True, stderr=subprocess.STDOUT)
                # Ubuntu usrmerge can report only diversion metadata for the
                # loader alias. Resolve its real path instead of treating that
                # informational row as a package (or dropping the dependency).
                rows = [line for line in output.splitlines() if line and
                        not line.startswith(('diversion by ', 'local diversion '))]
                if not rows:
                    continue
                found = {line.split(': ', 1)[0] for line in rows}
                if len(found) != 1 or any(not re.fullmatch(r'[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?', owner) for owner in found):
                    raise ValueError(f'ambiguous package owner: {output}')
                owners.update(found)
                break
            except subprocess.CalledProcessError:
                continue
        else:
            # Project libraries travel in the archive and must be present.
            if not Path(library).resolve().is_relative_to(Path('build').resolve()):
                raise ValueError(f'no runtime package owner for {library}')
    return owners


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    packages = set()
    binaries = []
    for path in sorted(args.root.rglob('*')):
        if path.is_file():
            with path.open('rb') as source:
                elf = source.read(4) == b'\x7fELF'
            if elf and (path.stat().st_mode & 0o111 or '.so' in path.name):
                if args.verify:
                    linked = subprocess.check_output(['ldd', str(path)], text=True, stderr=subprocess.STDOUT)
                    if 'not found' in linked:
                        raise ValueError(f'unresolved runtime dependency in {path}: {linked}')
                else:
                    packages |= packages_for(path)
                binaries.append(str(path))
    if not binaries:
        raise ValueError('no built ELF executables found')
    if args.verify:
        return
    if args.output is None:
        parser.error('--output is required when recording dependencies')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(sorted(packages)) + '\n')
    args.output.with_suffix('.json').write_text(json.dumps({'binaries': binaries, 'packages': sorted(packages)}, indent=2) + '\n')

if __name__ == '__main__':
    main()
