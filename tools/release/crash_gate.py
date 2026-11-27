#!/usr/bin/env python3
"""Asks glob2-online whether a release may be promoted (docs/multiplayer/crash-reports.md#promotion-gate).

    crash_gate.py --version 0.11.8.4
    crash_gate.py --version 0.11.8.4 --allow-insufficient

The gate passes when the release's crash-free sessions are no more than the
server's tolerance below the previous release with enough sessions, and no
untriaged crash group new in this release is among its five most affecting
installs. It prints the verdict and its reasons, and exits 0 on pass, 1 on
fail, 2 when there are too few opted-in sessions to decide (0 with
--allow-insufficient) and 3 when the gate could not be asked.

CRASH_SYMBOL_UPLOAD_TOKEN, the release workflows' symbol upload token,
authorizes the request. --json prints the server's answer unchanged.
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
import official_instance  # noqa: E402

TOKEN_ENV = 'CRASH_SYMBOL_UPLOAD_TOKEN'
RELEASE = re.compile(r'^[0-9]+(\.[0-9]+){2,3}$')
ATTEMPTS = 3
EXIT = {'pass': 0, 'fail': 1, 'insufficient_data': 2}
ERROR = 3


class GateError(Exception):
    pass


def fetch_gate(origin, token, version, opener=urllib.request.urlopen, sleep=time.sleep):
    url = f'{origin}/api/v1/crash-releases/{urllib.parse.quote(version)}/gate'
    for attempt in range(ATTEMPTS):
        request = urllib.request.Request(url, method='GET', headers={
            'authorization': f'Bearer {token}',
            'accept': 'application/json',
            'user-agent': 'glob2-release-gate',
        })
        try:
            with opener(request, timeout=60) as response:
                return json.loads(response.read().decode())
        except urllib.error.HTTPError as error:
            detail = error.read().decode('utf-8', 'replace')[:300]
            retry = error.code in (408, 429) or error.code >= 500
            if not retry or attempt == ATTEMPTS - 1:
                raise GateError(f'gate refused ({error.code}): {detail}') from error
        except (urllib.error.URLError, OSError, ValueError) as error:
            if attempt == ATTEMPTS - 1:
                raise GateError(f'gate request failed: {error}') from error
        sleep(2 ** (attempt + 1))
    raise AssertionError('unreachable')


def percent(value):
    return '-' if value is None else f'{value * 100:.2f}%'


def summary(gate):
    lines = [f'{gate["version"]}: {gate["verdict"]}']
    lines.append(f'  crash-free sessions {percent(gate.get("crashFreeSessions"))} over {gate["sessions"]} sessions')
    if gate.get('previousVersion'):
        lines.append(f'  previous {gate["previousVersion"]}: {percent(gate.get("previousCrashFreeSessions"))}'
                     f' over {gate.get("previousSessions", 0)} sessions')
    lines.append(f'  tolerance {percent(gate["tolerance"])}, minimum {gate["minimumSessions"]} sessions')
    lines += [f'  - {reason}' for reason in gate.get('reasons', [])]
    return '\n'.join(lines)


def main(argv=None, opener=urllib.request.urlopen, sleep=time.sleep):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--version', required=True, help='the release to check, for example 0.11.8.4')
    parser.add_argument('--origin', default=os.environ.get('GLOB2_CRASH_SYMBOL_ORIGIN', official_instance.DEFAULT_ORIGIN))
    parser.add_argument('--allow-insufficient', action='store_true',
                        help='exit 0 when there are too few sessions to decide')
    parser.add_argument('--json', action='store_true', help="print the server's answer as JSON")
    args = parser.parse_args(argv)
    if not RELEASE.match(args.version):
        parser.error(f'{args.version} is not a release version')
    token = os.environ.get(TOKEN_ENV, '')
    if not token:
        print(f'crash_gate: {TOKEN_ENV} is not set', file=sys.stderr)
        return ERROR
    try:
        gate = fetch_gate(args.origin.rstrip('/'), token, args.version, opener, sleep)
        verdict = gate['verdict']
        if verdict not in EXIT:
            raise GateError(f'unknown verdict {verdict!r}')
        print(json.dumps(gate, indent=2) if args.json else summary(gate))
    except (GateError, KeyError, TypeError) as error:
        print(f'crash_gate: {error}', file=sys.stderr)
        return ERROR
    if verdict == 'insufficient_data' and args.allow_insufficient:
        return 0
    return EXIT[verdict]


if __name__ == '__main__':
    sys.exit(main())
