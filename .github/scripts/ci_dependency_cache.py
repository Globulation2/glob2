"""Fingerprint build inputs and verify content of restored SDL dependency prefixes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compiler_identity(command):
    binary = shutil.which(command)
    if not binary:
        raise ValueError(f'Compiler unavailable: {command}')
    return {'binary': digest(Path(binary).resolve()),
            'version': subprocess.check_output([command, '--version'], text=True).splitlines()[0]}


def key(cc, cxx):
    files = [Path('scons/sdl3_dependencies.py'), Path('scons/sdl3-versions.json'),
             Path('scons/sdl3-vendored.json'), Path('.github/scripts/ci_dependency_cache.py')]
    files += sorted(Path('scons/vcpkg-ports/sdl3').glob('*.patch'))
    identity = {'schema': 1, 'platform': platform.platform(), 'machine': platform.machine(),
                'image': [os.environ.get('ImageOS'), os.environ.get('ImageVersion')],
                'cc': compiler_identity(cc), 'cxx': compiler_identity(cxx),
                'cmake': subprocess.check_output(['cmake', '--version'], text=True).splitlines()[0],
                'flags': {name: os.environ.get(name, '') for name in ('CFLAGS','CXXFLAGS','LDFLAGS')},
                'files': {str(path): digest(path) for path in files}}
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()


def contents(prefix):
    return {str(path.relative_to(prefix)): digest(path)
            for path in sorted(prefix.rglob('*')) if path.is_file() and path.name != '.ci-cache.json'}


def verify(prefix, identity):
    try:
        manifest = json.loads((prefix / '.ci-cache.json').read_text())
        return manifest['key'] == identity and bool(manifest['files']) and manifest['files'] == contents(prefix)
    except (OSError, ValueError, KeyError):
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['key','verify','seal'])
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--prefix', type=Path, default=Path('build/sdl3-ci/prefix'))
    parser.add_argument('--key')
    args = parser.parse_args()
    if args.mode == 'key':
        value = key(args.cc, args.cxx)
        print(value)
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a') as target:
                target.write('key=' + value + '\n')
    elif args.mode == 'verify':
        if not verify(args.prefix, args.key) and args.prefix.exists():
            print('Restored dependency prefix is invalid; rebuilding')
            shutil.rmtree(args.prefix)
    else:
        files = contents(args.prefix)
        if not files:
            raise ValueError('Cannot seal an incomplete dependency prefix')
        (args.prefix / '.ci-cache.json').write_text(json.dumps({'key': args.key, 'files': files}, sort_keys=True))


if __name__ == '__main__':
    main()
