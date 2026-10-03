#!/usr/bin/env python3
"""Turns on "Sign in with Google" for a single-host deployment, in one step:

    printf %s "$GOOGLE_CLIENT_SECRET" | \\
      python3 deploy/configure-signin.py <env-file> google --client-id <id> [--restart]

* adds (or replaces) the `google` OIDC provider in the instance config named by
  GLOB2_INSTANCE_CONFIG in the env file, keeping the rest of the file and its
  comments as they are;
* stores the client secret, read from standard input so it never appears on a
  command line, as GOOGLE_CLIENT_SECRET in the env file (kept mode 600);
* with --restart, recreates platform-api and platform-worker so they load it.

`--remove` takes the provider out again (the secret line stays harmless).
The OAuth client itself is made in the Google Cloud console; see
docs/hosting/README.md, "Sign-in providers".
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SECRET_ENV = 'GOOGLE_CLIENT_SECRET'
CLIENT_ID = re.compile(r'^[0-9]+-[a-z0-9]+\.apps\.googleusercontent\.com$')


def setting(env_text: str, name: str) -> str | None:
    value = None
    for line in env_text.splitlines():
        if line.startswith(f'{name}='):
            value = line[len(name) + 1:]
    return value


def google_provider(client_id: str) -> dict:
    return {'id': 'google', 'kind': 'oidc', 'preset': 'google', 'displayName': 'Google',
            'clientId': client_id, 'clientSecretEnv': SECRET_ENV}


def replace_providers(text: str, providers: list[dict]) -> str:
    """Rewrites only the value of `auth.providers`, as one flow mapping per line."""
    lines = text.splitlines(keepends=True)
    auth = next((i for i, l in enumerate(lines) if re.match(r'^auth:\s*(#.*)?$', l)), None)
    if auth is None:
        raise ValueError('the instance config has no top-level auth: section')
    start = indent = None
    for i in range(auth + 1, len(lines)):
        line = lines[i]
        if line.strip() and not line.startswith((' ', '#')):
            break  # next top-level key
        match = re.match(r'^(\s+)providers:', line)
        if match:
            start, indent = i, len(match.group(1))
            break
    if start is None:
        raise ValueError('auth: has no providers: key')
    end = start + 1
    while end < len(lines):
        line = lines[end]
        stripped = line.strip()
        lead = len(line) - len(line.lstrip(' '))
        if stripped and not stripped.startswith('#') and lead <= indent and \
                not (lead == indent and stripped.startswith('- ')):
            break
        if stripped.startswith('#') and lead <= indent:
            break  # a comment that belongs to the next key
        end += 1
    pad = ' ' * indent
    if providers:
        block = f'{pad}providers:\n' + ''.join(
            f'{pad}  - {json.dumps(p, separators=(", ", ": "))}\n' for p in providers)
    else:
        block = f'{pad}providers: []\n'
    return ''.join(lines[:start]) + block + ''.join(lines[end:])


def update_instance(path: Path, client_id: str | None) -> None:
    import yaml  # PyYAML: checks the result before anything is written

    text = path.read_text()
    config = yaml.safe_load(text) or {}
    providers = [p for p in (config.get('auth') or {}).get('providers') or []
                 if p.get('id') != 'google']
    if client_id:
        providers.append(google_provider(client_id))
    updated = replace_providers(text, providers)
    expected = dict(config)
    expected['auth'] = {**(config.get('auth') or {}), 'providers': providers}
    if yaml.safe_load(updated) != expected:
        raise ValueError(f'could not edit {path} safely; add the provider by hand')
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(updated)
    os.chmod(tmp, path.stat().st_mode & 0o777)
    tmp.replace(path)


def update_env(path: Path, secret: str) -> None:
    lines = [l for l in path.read_text().splitlines() if not l.startswith(f'{SECRET_ENV}=')]
    lines.append(f'{SECRET_ENV}={secret}')
    tmp = path.with_name(path.name + '.tmp')
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as out:
        out.write('\n'.join(lines) + '\n')
    tmp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('env_file', type=Path)
    parser.add_argument('provider', choices=['google'])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--client-id', help='<number>-<id>.apps.googleusercontent.com')
    action.add_argument('--remove', action='store_true')
    parser.add_argument('--restart', action='store_true',
                        help='recreate platform-api and platform-worker afterwards')
    args = parser.parse_args()

    env_file = args.env_file.resolve()
    env_text = env_file.read_text()
    instance = setting(env_text, 'GLOB2_INSTANCE_CONFIG') or './instance.yaml'
    instance_path = Path(instance)
    if not instance_path.is_absolute():
        instance_path = ROOT / 'deploy' / instance_path  # compose resolves it from deploy/

    if args.remove:
        update_instance(instance_path, None)
        print(f'configure-signin: removed the google provider from {instance_path}')
    else:
        if not CLIENT_ID.match(args.client_id):
            parser.error('--client-id must look like 1234-abc.apps.googleusercontent.com')
        secret = sys.stdin.read().strip()
        if not secret or any(c.isspace() for c in secret):
            parser.error('pipe the client secret (one word) on standard input')
        update_env(env_file, secret)
        update_instance(instance_path, args.client_id)
        print(f'configure-signin: google provider in {instance_path}; '
              f'{SECRET_ENV} in {env_file}')

    if args.restart:
        subprocess.run(['docker', 'compose', '-f', 'deploy/compose.yaml', '--env-file',
                        str(env_file), 'up', '-d', '--no-deps', '--no-build', '--force-recreate',
                        '--wait', 'platform-api', 'platform-worker'], cwd=ROOT, check=True)
        print('configure-signin: platform-api and platform-worker restarted')
    return 0


if __name__ == '__main__':
    sys.exit(main())
