#!/usr/bin/env python3
"""Select a narrowly scoped Epic startup patch on an existing public release."""
import argparse
import re
import subprocess

PUBLIC_REPOSITORY = 'https://github.com/Globulation2/glob2.git'
ALLOWED_PATHS = {
    'src/app/cli/CommandLine.cpp',
    'src/app/cli/CommandLineTest.cpp',
    'docs/releases/store-setup.md',
}


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def select_source(tag, source_ref, repository=PUBLIC_REPOSITORY):
    base = git('rev-parse', f'{tag}^{{commit}}')
    if git('rev-parse', 'HEAD') != base:
        raise ValueError('Preflight must start at the checked public release tag')
    if not source_ref:
        return base
    if not re.fullmatch(r'[0-9a-f]{40}', source_ref):
        raise ValueError('source_ref must be an immutable lowercase public commit SHA')
    subprocess.run(['git', 'fetch', '--no-tags', repository, source_ref], check=True)
    if git('rev-parse', 'FETCH_HEAD') != source_ref:
        raise ValueError('Fetched source does not match requested commit')
    subprocess.run(['git', 'merge-base', '--is-ancestor', base, source_ref], check=True)
    paths = subprocess.check_output(['git', 'diff', '--no-renames', '--name-only', '-z', base, source_ref]).decode().split('\0')
    changed = {p for p in paths if p}
    if not changed or not changed <= ALLOWED_PATHS:
        raise ValueError('Patched source must change only the approved CLI files/store guide')
    for path in changed:
        mode = git('ls-tree', source_ref, '--', path).split(' ', 1)[0]
        if mode != '100644':
            raise ValueError('Patched files must remain regular non-executable files')
    # The allowlist also keeps the release-version file and all engine/assets identical.
    subprocess.run(['git', 'checkout', '--detach', source_ref], check=True)
    return source_ref


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--source-ref', default='')
    args = parser.parse_args()
    try:
        print(select_source(args.tag, args.source_ref))
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Epic source qualification failed: {error}\n')


if __name__ == '__main__':
    main()
