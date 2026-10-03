#!/usr/bin/env python3
"""Runner side of .github/workflows/deploy-online.yml: SSH to the host through IAP.

Uses only instance-scoped permissions: it adds a short-lived SSH key for one user
to the instance's own metadata (compute.instances.get + setMetadata on that
instance), connects through an IAP TCP tunnel (iap.tunnelInstances.accessViaIAP on
that instance) and removes the key again. `gcloud compute ssh` would also need
project-wide reads and try project-wide metadata first, so it is not used.

    online_remote.py add-key    --key FILE.pub
    online_remote.py remove-key
    online_remote.py run        --key FILE -- <online-deploy.sh arguments>

Instance, zone, project and user come from --instance/--zone/--project/--user or
GLOB2_ONLINE_INSTANCE, GLOB2_ONLINE_ZONE, GLOB2_ONLINE_PROJECT and
GLOB2_ONLINE_SSH_USER. `run` pipes deploy/online-deploy.sh to `sh -s` on the host.
See docs/hosting/README.md, "Automatic deployment".
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'deploy/online-deploy.sh'
# Marks the keys this script adds, so it removes only its own.
MARKER = 'glob2-online-deploy'
COMPUTE = 'https://compute.googleapis.com/compute/v1'


def key_line(user, public_key, expire_on):
    """An instance metadata ssh-keys entry the guest agent drops after expire_on."""
    parts = public_key.split()
    if len(parts) < 2:
        raise ValueError('not an OpenSSH public key')
    expiry = json.dumps({'userName': MARKER, 'expireOn': expire_on.strftime('%Y-%m-%dT%H:%M:%S+0000')},
                        separators=(',', ':'))
    return f'{user}:{parts[0]} {parts[1]} google-ssh {expiry}'


def is_ours(line):
    return f'"userName":"{MARKER}"' in line.replace(' ', '')


def expired(line, now):
    if 'google-ssh' not in line:
        return False
    try:
        data = json.loads(line.split('google-ssh', 1)[1].strip())
        expire = datetime.datetime.strptime(data['expireOn'], '%Y-%m-%dT%H:%M:%S%z')
    except (ValueError, KeyError):
        return False
    return expire <= now


def edit_keys(metadata, add=None, now=None):
    """Return metadata with this script's expired (or all, when add is None) keys
    removed and `add` appended; every other item and key is kept as it is."""
    now = now or datetime.datetime.now(datetime.timezone.utc)
    items = [dict(item) for item in metadata.get('items', [])]
    entry = next((item for item in items if item['key'] == 'ssh-keys'), None)
    lines = entry['value'].splitlines() if entry else []
    kept = [line for line in lines if line.strip() and not (is_ours(line) and (add is None or expired(line, now)))]
    if add:
        kept.append(add)
    if entry is None and kept:
        entry = {'key': 'ssh-keys'}
        items.append(entry)
    if entry is not None:
        if kept:
            entry['value'] = '\n'.join(kept)
        else:
            items.remove(entry)
    result = {'items': items}
    if 'fingerprint' in metadata:
        result['fingerprint'] = metadata['fingerprint']
    return result


def token():
    return subprocess.run(['gcloud', 'auth', 'print-access-token'], check=True, capture_output=True,
                          text=True).stdout.strip()


def request(method, url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={'Authorization': f'Bearer {token()}', 'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.loads(response.read() or b'{}')


def set_keys(args, add=None):
    base = f'{COMPUTE}/projects/{args.project}/zones/{args.zone}/instances/{args.instance}'
    for attempt in range(5):
        instance = request('GET', base)
        metadata = instance.get('metadata', {})
        updated = edit_keys(metadata, add)
        if updated.get('items') == metadata.get('items', []):
            return instance
        try:
            # The fingerprint makes this fail (412) instead of overwriting a
            # concurrent change; then read again and retry.
            request('POST', f'{base}/setMetadata', updated)
            return instance
        except urllib.error.HTTPError as error:
            if error.code != 412 or attempt == 4:
                raise SystemExit(f'online_remote: setMetadata failed: {error.code} {error.read()[:500]!r}')
            time.sleep(2)
    return None


def ssh_command(args, key, remote):
    proxy = (f'gcloud compute start-iap-tunnel {args.instance} 22 --listen-on-stdin '
             f'--project={args.project} --zone={args.zone} --verbosity=error')
    return ['ssh', '-i', key, '-o', f'ProxyCommand={proxy}', '-o', 'IdentitiesOnly=yes',
            # IAP authenticates the tunnel's endpoint as this instance; the host
            # key is not pinned because the runner is new for every run.
            '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR',
            '-o', 'ConnectTimeout=30', '-o', 'ServerAliveInterval=30',
            f'{args.user}@{args.instance}', remote]


def run(args):
    remote = 'sh -s --' + ''.join(' ' + quote(arg) for arg in args.arguments)
    command = ssh_command(args, args.key, remote)
    # The guest agent installs a newly added key within seconds; ssh exits 255
    # for connection and authentication failures, so retry those for a while.
    deadline = time.monotonic() + args.connect_timeout
    while True:
        with DRIVER.open('rb') as script:
            result = subprocess.run(command, stdin=script)
        if result.returncode != 255 or time.monotonic() > deadline:
            return result.returncode
        print('online_remote: ssh could not connect yet; retrying', file=sys.stderr, flush=True)
        time.sleep(10)


def quote(arg):
    if arg and all(c.isalnum() or c in '@%+=:,./-_' for c in arg):
        return arg
    return "'" + arg.replace("'", "'\\''") + "'"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=('add-key', 'remove-key', 'run'))
    parser.add_argument('--project', default=os.environ.get('GLOB2_ONLINE_PROJECT'))
    parser.add_argument('--zone', default=os.environ.get('GLOB2_ONLINE_ZONE'))
    parser.add_argument('--instance', default=os.environ.get('GLOB2_ONLINE_INSTANCE'))
    parser.add_argument('--user', default=os.environ.get('GLOB2_ONLINE_SSH_USER'))
    parser.add_argument('--key', help='add-key: the public key file; run: the private key file')
    parser.add_argument('--hours', type=float, default=3, help='add-key: lifetime of the key (default 3)')
    parser.add_argument('--connect-timeout', type=float, default=120,
                        help='run: seconds to keep retrying a failed connection')
    parser.add_argument('arguments', nargs='*')
    args = parser.parse_args(argv)
    for name in ('project', 'zone', 'instance', 'user'):
        if not getattr(args, name):
            parser.error(f'--{name} (or GLOB2_ONLINE_{name.upper() if name != "user" else "SSH_USER"}) is required')
    if args.command == 'add-key':
        expire = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(hours=args.hours)
        set_keys(args, key_line(args.user, Path(args.key).read_text(), expire))
        print(f'online_remote: added a key for {args.user} on {args.instance}, expiring {expire:%H:%M} UTC')
        return 0
    if args.command == 'remove-key':
        set_keys(args)
        print(f'online_remote: removed this workflow\'s keys from {args.instance}')
        return 0
    if not args.key:
        parser.error('run needs --key')
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
