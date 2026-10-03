"""Attest source and compiler configuration before same-run program reuse."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['write','verify'])
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    path = Path('artifacts/ci-native/provenance.json')
    identity = {'schema': 1, 'sha': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                'compiler': args.compiler, 'role': 'client', 'release': 1, 'opengl': 1}
    if args.mode == 'write':
        path.write_text(json.dumps(identity, sort_keys=True) + '\n')
    elif json.loads(path.read_text()) != identity:
        raise ValueError('Built programs do not match the source/compiler/configuration being verified')


if __name__ == '__main__':
    main()
